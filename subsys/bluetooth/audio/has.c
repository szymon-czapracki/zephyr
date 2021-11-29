/*
 * Copyright (c) 2022 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>

#include <device.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <net/buf.h>
#include <sys/atomic.h>
#include <sys/byteorder.h>
#include <sys/check.h>
#include <sys/math_extras.h>

#include <bluetooth/audio/has.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>

#include "has_internal.h"
#include "../host/conn_internal.h"
#include "../host/hci_core.h"

#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_HAS)
#define LOG_MODULE_NAME bt_has
#include "common/log.h"

#define CP_WORK_TIMEOUT K_MSEC(10)
#define BT_HAS_MAX_CLIENT MIN(CONFIG_BT_MAX_CONN, CONFIG_BT_MAX_PAIRED)

enum {
	CLIENT_FLAG_ENCRYPTED, /* if ACL is encrypted */
	CLIENT_FLAG_ATT_MTU_VALID, /* if ATT MTU meet HAS specification requirements */
	CLIENT_FLAG_CP_IND_ENABLED, /* if Control Point indications are enabled */
	CLIENT_FLAG_CP_NFY_ENABLED, /* if Control Point notifications are enabled */
	CLIENT_FLAG_CP_BUSY, /* if Control Point operation is in progress */

	/* Total number of flags - must be at the end */
	CLIENT_NUM_FLAGS,
};

#if defined(CONFIG_BT_HAS)
static struct preset {
	uint8_t id;
	uint8_t properties;
#if defined(CONFIG_BT_HAS_PRESET_NAME_DYNAMIC)
	char name[BT_HAS_PRESET_NAME_MAX];
#else
	const char *name;
#endif /* CONFIG_BT_HAS_PRESET_NAME_DYNAMIC */
	bool hidden;
	/** Preset current name awareness */
	ATOMIC_DEFINE(is_client_name_aware, BT_HAS_MAX_CLIENT);
} preset_list[CONFIG_BT_HAS_PRESET_CNT];

/* Separate Preset Control Point configuration for each bonded device */
static struct client {
	/** Connection object */
	struct bt_conn *conn;
	/** Pending preset changed operation per each preset */
	struct {
		/* Preset changed pending if flag set */
		ATOMIC_DEFINE(pending, CONFIG_BT_HAS_PRESET_CNT);
		/* ChangeId to be sent */
		uint8_t change_id[CONFIG_BT_HAS_PRESET_CNT];
	} preset_changed;
	/** Read preset operation data */
	struct {
		/** Next preset to be sent */
		struct preset *preset_pending;
		/** Number of remaining presets to send */
		uint8_t num_presets;
	} read_preset_rsp;
	union {
		/** Control Point Indicate parameters */
		struct bt_gatt_indicate_params ind;
		/** Control Point Notify parameters */
		struct bt_gatt_notify_params nfy;
	} cp_tx_params;
	/* Control Point transmit work */
	struct k_work_delayable cp_tx_work;
	struct k_work_sync cp_tx_sync;
	ATOMIC_DEFINE(flags, CLIENT_NUM_FLAGS);
} client_list[BT_HAS_MAX_CLIENT];

static struct bt_has has_local;
static struct bt_has_preset_ops *preset_ops;
static uint8_t last_preset_id;
static struct k_work active_preset_work;

static uint8_t client_index(struct client *client)
{
	ptrdiff_t index = client - client_list;

	__ASSERT(index >= 0 && index < ARRAY_SIZE(client_list), "Invalid client pointer");

	return (uint8_t)index;
}

static void client_free(struct client *client)
{
	BT_DBG("%p", client);

	for (int i = 0; i < ARRAY_SIZE(preset_list); i++) {
		struct preset *preset = &preset_list[i];

		atomic_clear_bit(preset->is_client_name_aware, client_index(client));
	}

	/* Cancel ongoing work. Since the client can be re-used after this
	 * we need to sync to make sure that the kernel does not have it
	 * in its queue anymore.
	 */
	k_work_cancel_delayable_sync(&client->cp_tx_work, &client->cp_tx_sync);

	bt_conn_unref(client->conn);

	memset(client, 0, sizeof(*client));
}

static struct client *client_find(struct bt_conn *conn)
{
	for (int i = 0; i < ARRAY_SIZE(client_list); i++) {
		if (conn == client_list[i].conn) {
			return &client_list[i];
		}
	}

	return NULL;
}

static ssize_t control_point_rx(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				const void *data, uint16_t len, uint16_t offset, uint8_t flags);

static ssize_t read_features(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			     uint16_t len, uint16_t offset)
{
	BT_DBG("conn %p attr %p offset %d", conn, attr, offset);

	if (offset > 0) {
		return BT_GATT_ERR(BT_ATT_ERR_ATTRIBUTE_NOT_LONG);
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &has_local.features, 1);
}

static ssize_t read_active_preset_id(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				     void *buf, uint16_t len, uint16_t offset)
{
	BT_DBG("conn %p attr %p offset %d", conn, attr, offset);

	if (offset > 0) {
		return BT_GATT_ERR(BT_ATT_ERR_ATTRIBUTE_NOT_LONG);
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &has_local.active_id, 1);
}

static void ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	BT_DBG("attr %p value 0x%04x", attr, value);
}

static ssize_t cp_ccc_cfg_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				uint16_t value)
{
	struct client *client;

	BT_DBG("conn %p, value 0x%04x", conn, value);

	if (value == 0x0000) {
		client = client_find(conn);
		if (client) {
			atomic_clear_bit(client->flags, CLIENT_FLAG_CP_IND_ENABLED);
			atomic_clear_bit(client->flags, CLIENT_FLAG_CP_NFY_ENABLED);
		}
	} else if (value == BT_GATT_CCC_INDICATE) {
		client = client_find(conn);
		if (client) {
			atomic_set_bit(client->flags, CLIENT_FLAG_CP_IND_ENABLED);
		} else {
			return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
		}
	} else if (value == BT_GATT_CCC_NOTIFY) {
		client = client_find(conn);
		if (client) {
			atomic_set_bit(client->flags, CLIENT_FLAG_CP_NFY_ENABLED);
		} else {
			return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
		}
	} else {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	return sizeof(value);
}

/* Preset Control Point CCC */
static struct _bt_gatt_ccc cp_ccc_cfg =
	BT_GATT_CCC_INITIALIZER(ccc_cfg_changed, cp_ccc_cfg_write, NULL);

/* Hearing Access Service GATT Attributes */
BT_GATT_SERVICE_DEFINE(
	has_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HAS),
	BT_GATT_CHARACTERISTIC(BT_UUID_HAS_HEARING_AID_FEATURES, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ_ENCRYPT, read_features, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_HAS_PRESET_CONTROL_POINT,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_WRITE_ENCRYPT, NULL, control_point_rx, NULL),
	BT_GATT_CCC_MANAGED(&cp_ccc_cfg, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
	BT_GATT_CHARACTERISTIC(BT_UUID_HAS_ACTIVE_PRESET_INDEX,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ_ENCRYPT,
			       read_active_preset_id, NULL, NULL),
	BT_GATT_CCC(ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT));

static void process_control_point_tx_work(struct k_work *work);

static struct client *client_get(struct bt_conn *conn)
{
	struct client *client = NULL;

	BT_DBG("conn %p", conn);

	for (int i = 0; i < ARRAY_SIZE(client_list); i++) {
		if (conn == client_list[i].conn) {
			return &client_list[i];
		}

		/* first free slot */
		if (!client && !client_list[i].conn) {
			client = &client_list[i];
		}
	}

	if (client) {
		client->conn = bt_conn_ref(conn);
		k_work_init_delayable(&client->cp_tx_work, process_control_point_tx_work);
	}

	BT_DBG("new client %p", client);

	return client;
}

static inline int preset_changed_popcount(struct client *client)
{
#if CONFIG_BT_HAS_PRESET_CNT > 32
	int i, count = 0;

	for (i = 0; i < ARRAY_SIZE(client->preset_changed.pending); i++) {
		count += popcount(atomic_get(&client->preset_changed.pending[i]));
	}
#else
	return popcount(atomic_get(client->preset_changed.pending));
#endif
}

static bool is_preset_changed_pending(struct client *client)
{
	return preset_changed_popcount(client) > 0;
}

typedef bool (*preset_func_t)(struct preset *preset, void *user_data);

static void preset_foreach(uint8_t start_index, uint8_t end_index, uint8_t num_matches,
			   preset_func_t func, void *user_data)
{
	if (!num_matches) {
		num_matches = UINT8_MAX;
	}

	if (start_index <= last_preset_id) {
		for (int i = 0; i < ARRAY_SIZE(preset_list); i++) {
			struct preset *preset = &preset_list[i];
			bool match;

			/* Skip ahead if index is not within requested range */
			if (preset->id < start_index) {
				continue;
			}

			if (preset->id > end_index) {
				return;
			}

			match = func(preset, user_data);
			if (!match) {
				continue;
			}

			if (--num_matches > 0) {
				continue;
			}

			return;
		}
	}
}

static uint8_t preset_index(struct preset *preset)
{
	ptrdiff_t index = preset - preset_list;

	__ASSERT(index >= 0 && index < ARRAY_SIZE(preset_list), "Invalid preset pointer");

	return (uint8_t)index;
}

static struct preset *preset_get(uint8_t index)
{
	for (int i = 0; i < ARRAY_SIZE(preset_list); i++) {
		struct preset *preset = &preset_list[i];
		if (preset->id == index) {
			return preset;
		}
	}

	return NULL;
}

static void active_preset_work_process(struct k_work *work)
{
	bt_gatt_notify_uuid(NULL, BT_UUID_HAS_ACTIVE_PRESET_INDEX, has_svc.attrs,
			    &has_local.active_id, 1);
}

static bool is_read_preset_rsp_pending(struct client *client)
{
	return client->read_preset_rsp.preset_pending != NULL &&
	       client->read_preset_rsp.num_presets > 0;
}

static void control_point_tx_work_submit(struct client *client, k_timeout_t delay)
{
	if (!atomic_test_and_set_bit(client->flags, CLIENT_FLAG_CP_BUSY)) {
		k_work_reschedule(&client->cp_tx_work, delay);
	}
}

static void control_point_tx_done(struct bt_conn *conn, void *user_data)
{
	struct client *client;

	BT_DBG("conn %p\n", conn);

	client = client_find(conn);
	if (!client) {
		return;
	}

	atomic_clear_bit(client->flags, CLIENT_FLAG_CP_BUSY);

	/* Resubmit if needed */
	if (is_preset_changed_pending(client) || is_read_preset_rsp_pending(client)) {
		control_point_tx_work_submit(client, K_NO_WAIT);
	}
}

static void control_point_ind_done(struct bt_conn *conn, struct bt_gatt_indicate_params *params,
				   uint8_t err)
{
	/* TODO: handle error somehow */
	control_point_tx_done(conn, NULL);
}

static int control_point_tx(struct client *client, struct net_buf_simple *buf)
{
	if (atomic_test_bit(client->flags, CLIENT_FLAG_CP_NFY_ENABLED)) {
		client->cp_tx_params.nfy.attr = &has_svc.attrs[4];
		client->cp_tx_params.nfy.func = control_point_tx_done;
		client->cp_tx_params.nfy.data = buf->data;
		client->cp_tx_params.nfy.len = buf->len;

		return bt_gatt_indicate(client->conn, &client->cp_tx_params.ind);
	}

	if (atomic_test_bit(client->flags, CLIENT_FLAG_CP_IND_ENABLED)) {
		client->cp_tx_params.ind.attr = &has_svc.attrs[4];
		client->cp_tx_params.ind.func = control_point_ind_done;
		client->cp_tx_params.ind.destroy = NULL;
		client->cp_tx_params.ind.data = buf->data;
		client->cp_tx_params.ind.len = buf->len;

		return bt_gatt_indicate(client->conn, &client->cp_tx_params.ind);
	}

	return -ECANCELED;
}

/** Returns the first found preset index pending preset changed indication  */
static inline int preset_changed_index(struct client *client)
{
	int index = 0;
#if CONFIG_BT_HAS_PRESET_CNT > 32
	int i;

	for (i = 0; i < ARRAY_SIZE(client->preset_changed.pending); i++) {
		if (popcount(atomic_get(&client->preset_changed.pending[i])) > 0) {
			index += u32_count_trailing_zeros(
				atomic_get(client->preset_changed.pending));
			break;
		}

		index += 32;
	}
#else
	index = u32_count_trailing_zeros(atomic_get(client->preset_changed.pending));
#endif
	__ASSERT(index >= 0 && index < ARRAY_SIZE(preset_list), "Invalid client pointer");

	return index;
}

static int bt_has_cp_read_preset_rsp(struct client *client, struct preset *preset, bool is_last)
{
	struct bt_has_cp_hdr *hdr;
	struct bt_has_cp_read_preset_rsp *rsp;
	const uint8_t slen = strlen(preset->name);
	NET_BUF_SIMPLE_DEFINE(buf, sizeof(*hdr) + sizeof(*rsp) + slen);

	BT_DBG("client %p preset %p is_last %d", client, preset, is_last);

	hdr = net_buf_simple_add(&buf, sizeof(*hdr));
	hdr->op = BT_HAS_OP_READ_PRESET_RSP;
	rsp = net_buf_simple_add(&buf, sizeof(*rsp));
	rsp->is_last = is_last ? 0x01 : 0x00;
	rsp->id = preset->id;
	rsp->properties = preset->properties;
	net_buf_simple_add_mem(&buf, preset->name, slen);

	return control_point_tx(client, &buf);
}

static int bt_has_cp_preset_changed(struct client *client, struct preset *preset, bool is_last)
{
	struct bt_has_cp_hdr *hdr;
	struct bt_has_cp_preset_changed *pc;
	NET_BUF_SIMPLE_DEFINE(buf, 46);

	hdr = net_buf_simple_add(&buf, sizeof(*hdr));
	hdr->op = BT_HAS_OP_PRESET_CHANGED;
	pc = net_buf_simple_add(&buf, sizeof(*pc));
	pc->change_id = client->preset_changed.change_id[preset_index(preset)];
	pc->is_last = is_last ? 0x01 : 0x00;

	BT_DBG("client %p preset %p changeId 0x%02x is_last %d", client, preset, pc->change_id,
	       pc->is_last);

	switch (pc->change_id) {
	case BT_HAS_CHANGE_ID_GENERIC_UPDATE: {
		struct bt_has_cp_generic_update *gu;
		size_t name_len = MIN(sizeof(preset->name) - 1, BT_HAS_PRESET_NAME_MAX);

		gu = net_buf_simple_add(&buf, sizeof(*gu));
		gu->prev_id = 0x01; /* TODO */
		gu->id = preset->id;
		gu->properties = preset->properties;
		net_buf_simple_add_mem(&buf, preset->name, name_len);
	} break;
	case BT_HAS_CHANGE_ID_PRESET_DELETED:
	case BT_HAS_CHANGE_ID_PRESET_AVAILABLE:
	case BT_HAS_CHANGE_ID_PRESET_UNAVAILABLE:
		net_buf_simple_add_u8(&buf, preset->id);
		break;
	default:
		return -EINVAL;
	}

	return control_point_tx(client, &buf);
}

static void preset_changed_set(struct client *client, uint8_t index, uint8_t change_id)
{
	client->preset_changed.change_id[index] = change_id;
	atomic_set_bit(client->preset_changed.pending, index);
}

static inline void preset_changed_clear(struct client *client, uint8_t index)
{
	atomic_clear_bit(client->preset_changed.pending, index);
}

static inline void preset_changed_clear_all(struct client *client)
{
	atomic_clear(client->preset_changed.pending);
}

static bool find_visible(struct preset *preset, void *user_data)
{
	struct preset **found = user_data;

	if (!preset->hidden) {
		*found = preset;
		return true;
	}

	return false;
}

static void process_control_point_tx_work(struct k_work *work)
{
	struct client *client = CONTAINER_OF(work, struct client, cp_tx_work);
	struct preset *preset;
	bool is_last;
	int err;

	if (!client->conn || client->conn->state != BT_CONN_CONNECTED) {
		err = -ENOTCONN;
	} else if (is_read_preset_rsp_pending(client)) {
		preset = client->read_preset_rsp.preset_pending;

		client->read_preset_rsp.preset_pending = NULL;
		if (client->read_preset_rsp.num_presets > 1) {
			preset_foreach(preset->id + 1, last_preset_id, 1, find_visible,
				       &client->read_preset_rsp.preset_pending);
		}

		is_last = !client->read_preset_rsp.preset_pending;

		client->read_preset_rsp.num_presets--;

		err = bt_has_cp_read_preset_rsp(client, preset, is_last);
	} else if (is_preset_changed_pending(client)) {
		preset = &preset_list[preset_changed_index(client)];
		is_last = preset_changed_popcount(client) == 1;

		err = bt_has_cp_preset_changed(client, preset, is_last);
	} else {
		err = -ENODATA;
	}

	if (err) {
		atomic_clear_bit(client->flags, CLIENT_FLAG_CP_BUSY);
	} else {
		/* Clear pending preset changed for this preset and set name awarness */
		atomic_set_bit(preset->is_client_name_aware, client_index(client));
		preset_changed_clear(client, preset_index(preset));
	}
}

static int handle_read_preset_req(struct client *client, struct net_buf_simple *buf)
{
	const struct bt_has_cp_read_preset_req *req;

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_HAS_ERR_INVALID_PARAM_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	BT_DBG("start_id %d num_presets %d", req->start_id, req->num_presets);

	/* Reject if already in progress */
	if (is_read_preset_rsp_pending(client)) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	client->read_preset_rsp.preset_pending = NULL;
	if (req->num_presets > 0) {
		preset_foreach(req->start_id, last_preset_id, 1, find_visible,
			       &client->read_preset_rsp.preset_pending);
	}

	if (!client->read_preset_rsp.preset_pending) {
		return BT_GATT_ERR(BT_ATT_ERR_OUT_OF_RANGE);
	}

	client->read_preset_rsp.num_presets = req->num_presets;

	control_point_tx_work_submit(client, CP_WORK_TIMEOUT);

	return 0;
}

static void preset_changed(struct preset *preset, uint8_t change_id)
{
	uint8_t index = preset_index(preset);

	BT_DBG("preset %p %s", preset, bt_has_change_id_str(change_id));

	/* update the pending changeId flags */
	for (int i = 0; i < ARRAY_SIZE(client_list); i++) {
		struct client *client = &client_list[i];

		if (!atomic_test_bit(client->flags, CLIENT_FLAG_CP_IND_ENABLED) &&
		    !atomic_test_bit(client->flags, CLIENT_FLAG_CP_NFY_ENABLED)) {
			continue;
		}

		if (!client->conn || client->conn->state != BT_CONN_CONNECTED) {
			continue;
		}

		if (atomic_test_bit(client->preset_changed.pending, index)) {
			uint8_t change_id_pending;

			change_id_pending = client->preset_changed.change_id[index];

			switch (change_id) {
			case BT_HAS_CHANGE_ID_GENERIC_UPDATE:
				if (change_id_pending == BT_HAS_CHANGE_ID_PRESET_DELETED &&
				    atomic_test_bit(preset->is_client_name_aware, i)) {
					/* skip visibility toggle */
					preset_changed_clear(client, index);
				} else {
					preset_changed_set(client, index, change_id);
				}

				break;
			case BT_HAS_CHANGE_ID_PRESET_DELETED:
				if (change_id_pending == BT_HAS_CHANGE_ID_GENERIC_UPDATE &&
				    atomic_test_bit(preset->is_client_name_aware, i)) {
					/* skip visibility toggle */
					preset_changed_clear(client, index);
				} else {
					preset_changed_set(client, index, change_id);
				}

				break;
			case BT_HAS_CHANGE_ID_PRESET_AVAILABLE:
				if (change_id_pending == BT_HAS_CHANGE_ID_PRESET_UNAVAILABLE) {
					/* skip visibility toggle */
					preset_changed_clear(client, index);
				}

				break;
			case BT_HAS_CHANGE_ID_PRESET_UNAVAILABLE:
				if (change_id_pending == BT_HAS_CHANGE_ID_PRESET_AVAILABLE) {
					/* skip visibility toggle */
					preset_changed_clear(client, index);
				}

				break;
			}
		} else {
			preset_changed_set(client, index, change_id);
		}

		if (is_preset_changed_pending(client)) {
			control_point_tx_work_submit(client, CP_WORK_TIMEOUT);
		} else if (!is_preset_changed_pending(client) ||
			   !is_read_preset_rsp_pending(client)) {
			k_work_cancel_delayable_sync(&client->cp_tx_work, &client->cp_tx_sync);
			atomic_clear_bit(client->flags, CLIENT_FLAG_CP_BUSY);
		}
	}
}

static int preset_name_set(uint8_t id, const char *name, uint8_t len)
{
#if defined(CONFIG_BT_HAS_PRESET_NAME_DYNAMIC)
	struct preset *preset;

	if (len < BT_HAS_PRESET_NAME_MIN || len > BT_HAS_PRESET_NAME_MAX) {
		return BT_GATT_ERR(BT_HAS_ERR_INVALID_PARAM_LEN);
	}

	preset = preset_get(id);
	if (!preset) {
		return BT_GATT_ERR(BT_ATT_ERR_OUT_OF_RANGE);
	}

	if (!(preset->properties & BT_HAS_PROP_WRITABLE)) {
		return BT_GATT_ERR(BT_HAS_ERR_WRITE_NAME_NOT_ALLOWED);
	}

	strncpy(preset->name, name, len);
	preset->name[len] = '\0';

	/* Do not send preset changed notification if the preset is hidden */
	if (!preset->hidden) {
		atomic_clear(preset->is_client_name_aware);
		preset_changed(preset, BT_HAS_CHANGE_ID_GENERIC_UPDATE);
	}

	if (preset_ops->name_changed) {
		preset_ops->name_changed(&has_local, id, preset->name);
	}

	return 0;
}

static int handle_write_preset_name(struct client *client, struct net_buf_simple *buf)
{
	const struct bt_has_cp_write_preset_name_req *req;

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_HAS_ERR_INVALID_PARAM_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	return preset_name_set(req->id, req->name, buf->len);
}
#else
	return -EOPNOTSUPP;
}
#endif /* CONFIG_BT_HAS_PRESET_NAME_DYNAMIC */

static int call_ops_set_active_preset(uint8_t id, bool sync)
{
	if (preset_ops == NULL) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	if (preset_ops->active_set(&has_local, id, sync) != 0) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	return 0;
}

static int handle_set_active_preset(struct client *client, struct net_buf_simple *buf, bool sync)
{
	const struct bt_has_cp_set_active_preset_req *req;
	struct preset *preset;

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_HAS_ERR_INVALID_PARAM_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	preset = preset_get(req->id);
	if (!preset) {
		return BT_GATT_ERR(BT_ATT_ERR_OUT_OF_RANGE);
	}

	if (!(preset->properties & BT_HAS_PROP_AVAILABLE)) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	return call_ops_set_active_preset(preset->id, sync);
}

static bool find_available(struct preset *preset, void *user_data)
{
	struct preset **found = user_data;

	if ((preset->properties & BT_HAS_PROP_AVAILABLE) && !preset->hidden) {
		*found = preset;
		return true;
	}

	return false;
}

static int handle_set_next_preset(struct client *client, struct net_buf_simple *buf, bool sync)
{
	struct preset *preset = NULL;

	preset_foreach(has_local.active_id + 1, last_preset_id, 1, find_available, &preset);
	if (preset) {
		return call_ops_set_active_preset(preset->id, sync);
	}

	preset_foreach(0x01, has_local.active_id - 1, 1, find_available, &preset);
	if (preset) {
		return call_ops_set_active_preset(preset->id, sync);
	}

	return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
}

static int handle_set_prev_preset(struct client *client, struct net_buf_simple *buf, bool sync)
{
	struct preset *preset = NULL;

	preset_foreach(0x01, has_local.active_id - 1, 1, find_available, &preset);
	if (preset) {
		return call_ops_set_active_preset(preset->id, sync);
	}

	preset_foreach(has_local.active_id + 1, last_preset_id, 1, find_available, &preset);
	if (preset) {
		return call_ops_set_active_preset(preset->id, sync);
	}

	return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
}

static ssize_t control_point_rx(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				const void *data, uint16_t len, uint16_t offset, uint8_t flags)
{
	const struct bt_has_cp_hdr *hdr;
	struct client *client;
	struct net_buf_simple buf;
	ssize_t ret;

	client = client_find(conn);
	if (!client) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	if (offset > 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len < sizeof(*hdr)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	net_buf_simple_init_with_data(&buf, (void *)data, len);

	hdr = net_buf_simple_pull_mem(&buf, sizeof(*hdr));

	BT_DBG("conn %p op %s (0x%02x)", conn, bt_has_op_str(hdr->op), hdr->op);

	switch (hdr->op) {
	case BT_HAS_OP_READ_PRESET_REQ:
		if (!atomic_test_bit(client->flags, CLIENT_FLAG_CP_IND_ENABLED)) {
			return BT_GATT_ERR(BT_ATT_ERR_CCC_IMPROPER_CONF);
		}

		/* Fail if ATT MTU is potentially insufficient to complete the operation */
		if (!atomic_test_bit(client->flags, CLIENT_FLAG_ATT_MTU_VALID)) {
			return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
		}

		ret = handle_read_preset_req(client, &buf);
		break;

#if defined(CONFIG_BT_HAS_PRESET_NAME_DYNAMIC)
	case BT_HAS_OP_WRITE_PRESET_NAME:
		if (!atomic_test_bit(client->flags, CLIENT_FLAG_CP_IND_ENABLED)) {
			return BT_GATT_ERR(BT_ATT_ERR_CCC_IMPROPER_CONF);
		}

		/* Fail if ATT MTU is potentially insufficient to complete the operation */
		if (!atomic_test_bit(client->flags, CLIENT_FLAG_ATT_MTU_VALID)) {
			return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
		}

		ret = handle_write_preset_name(client, &buf);
		break;
#endif /* CONFIG_BT_HAS_PRESET_NAME_DYNAMIC */

	case BT_HAS_OP_SET_ACTIVE_PRESET:
		if (!atomic_test_bit(client->flags, CLIENT_FLAG_CP_IND_ENABLED)) {
			return BT_GATT_ERR(BT_ATT_ERR_CCC_IMPROPER_CONF);
		}

		ret = handle_set_active_preset(client, &buf, false);
		break;

	case BT_HAS_OP_SET_NEXT_PRESET:
		ret = handle_set_next_preset(client, &buf, false);
		break;

	case BT_HAS_OP_SET_PREV_PRESET:
		ret = handle_set_prev_preset(client, &buf, false);
		break;

#if defined(CONFIG_BT_HAS_HA_PRESET_SYNC_SUPPORT)
	case BT_HAS_OP_SET_ACTIVE_PRESET_SYNC:
		if (!atomic_test_bit(client->flags, CLIENT_FLAG_CP_IND_ENABLED)) {
			return BT_GATT_ERR(BT_ATT_ERR_CCC_IMPROPER_CONF);
		}

		ret = handle_set_active_preset(client, &buf, true);
		break;

	case BT_HAS_OP_SET_NEXT_PRESET_SYNC:
		ret = handle_set_next_preset(client, &buf, true);
		break;

	case BT_HAS_OP_SET_PREV_PRESET_SYNC:
		ret = handle_set_prev_preset(client, &buf, true);
		break;
#else
	case BT_HAS_OP_SET_ACTIVE_PRESET_SYNC:
	case BT_HAS_OP_SET_NEXT_PRESET_SYNC:
	case BT_HAS_OP_SET_PREV_PRESET_SYNC:
		ret = BT_GATT_ERR(BT_HAS_ERR_PRESET_SYNC_NOT_SUPP);
		break;
#endif /* BT_HAS_HA_PRESET_SYNC_SUPPORT */

	default:
		ret = BT_GATT_ERR(BT_HAS_ERR_INVALID_OP);
	}

	return (ret < 0) ? ret : len;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	struct client *client;

	BT_DBG("conn %p err %d", conn, err);

	if (err != 0 || !bt_addr_le_is_bonded(conn->id, &conn->le.dst)) {
		return;
	}

	client = client_get(conn);
	if (unlikely(!client)) {
		BT_ERR("Failed to allocate client");
		return;
	}

	/* Mark all non-hidden presets to be sent via Preset Changed for bonded device.
	 *
	 * XXX: At this point stored GATT CCC configurations are not loaded yet,
	 *	thus we postpone bt_gatt_is_subscribed check on Control Point CCC to be
	 *	done in security_changed().
	 */
	for (int index = 0; index < ARRAY_SIZE(preset_list); index++) {
		if (preset_list[index].hidden) {
			continue;
		}

		preset_changed_set(client, index, BT_HAS_CHANGE_ID_GENERIC_UPDATE);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct client *client;

	BT_DBG("conn %p reason %d", conn, reason);

	client = client_find(conn);
	if (client) {
		client_free(client);
	}
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	struct client *client;

	BT_DBG("conn %p level %d err %d", conn, level, err);

	if (err != BT_SECURITY_ERR_SUCCESS) {
		return;
	}

	client = client_get(conn);
	if (unlikely(!client)) {
		BT_ERR("Failed to allocate client");
		return;
	}

	/* Check encryption is enabled */
	if (level < BT_SECURITY_L2) {
		return;
	}

	if (!atomic_test_and_set_bit(client->flags, CLIENT_FLAG_ENCRYPTED)) {
		if (!atomic_test_bit(client->flags, CLIENT_FLAG_ATT_MTU_VALID) &&
		    bt_gatt_get_mtu(client->conn) >= BT_HAS_ATT_MTU_MIN) {
			atomic_set_bit(client->flags, CLIENT_FLAG_ATT_MTU_VALID);
		}

		if (bt_gatt_is_subscribed(client->conn, &has_svc.attrs[4], BT_GATT_CCC_INDICATE)) {
			atomic_set_bit(client->flags, CLIENT_FLAG_CP_IND_ENABLED);
		}

		if (bt_gatt_is_subscribed(client->conn, &has_svc.attrs[4], BT_GATT_CCC_NOTIFY)) {
			atomic_set_bit(client->flags, CLIENT_FLAG_CP_NFY_ENABLED);
		}

		/* If peer is not subscribed for Control Point messages, unmark the pending
		 * Preset Changed messages marked to be sent in connected().
		 * Otherwise send pending Preset Changed if any.
		 */
		if (!atomic_test_bit(client->flags, CLIENT_FLAG_CP_IND_ENABLED) &&
		    !atomic_test_bit(client->flags, CLIENT_FLAG_CP_NFY_ENABLED)) {
			preset_changed_clear_all(client);
		} else if (is_preset_changed_pending(client) &&
			   atomic_test_bit(client->flags, CLIENT_FLAG_ATT_MTU_VALID)) {
			control_point_tx_work_submit(client, CP_WORK_TIMEOUT);
		}
	}
}

BT_CONN_CB_DEFINE(has_conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void att_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	struct client *client;

	BT_DBG("conn %p tx %u rx %u", conn, tx, rx);

	client = client_find(conn);
	if (!client) {
		return;
	}

	/* HearingAidProfile_validation_r01
	 *
	 * If the HARC supports the Read All Presets procedure or the Read Preset by Index
	 * procedure, then the HARC shall support an ATT_MTU value no less than 49.
	 */
	if (tx < BT_HAS_ATT_MTU_MIN) {
		return;
	}

	/* Send pending Preset Changed if any. */
	if (!atomic_test_and_set_bit(client->flags, CLIENT_FLAG_ATT_MTU_VALID) &&
	    is_preset_changed_pending(client) &&
	    atomic_test_bit(client->flags, CLIENT_FLAG_ENCRYPTED) &&
	    atomic_test_bit(client->flags, CLIENT_FLAG_CP_IND_ENABLED)) {
		control_point_tx_work_submit(client, CP_WORK_TIMEOUT);
	}
}

static struct bt_gatt_cb gatt_cb = {
	.att_mtu_updated = att_mtu_updated,
};

static int has_init(const struct device *dev)
{
	bt_has_hearing_aid_type_t type;

	ARG_UNUSED(dev);

	if (IS_ENABLED(CONFIG_BT_HAS_HA_TYPE_MONAURAL)) {
		type = BT_HAS_MONAURAL_HEARING_AID;
	} else if (IS_ENABLED(CONFIG_BT_HAS_HA_TYPE_BANDED)) {
		type = BT_HAS_BANDED_HEARING_AID;
	} else {
		type = BT_HAS_BINAURAL_HEARING_AID;
	}

	/* Initialize the supported features characteristic value */
	has_local.features = BT_HAS_FEAT_HEARING_AID_TYPE_MASK & type;
	if (IS_ENABLED(CONFIG_BT_HAS_PRESET_SYNC_SUPPORT)) {
		has_local.features |= BT_HAS_FEAT_BIT_PRESET_SYNC;
	}
	if (!IS_ENABLED(CONFIG_BT_HAS_IDENTICAL_PRESET_RECORDS)) {
		has_local.features |= BT_HAS_FEAT_BIT_INDEPENDENT_PRESETS;
	}

	bt_gatt_cb_register(&gatt_cb);

	k_work_init(&active_preset_work, active_preset_work_process);

	return 0;
}

SYS_INIT(has_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#define BT_HAS_IS_LOCAL(_has) ((_has) == &has_local)
#endif /* CONFIG_BT_HAS */

int bt_has_register(struct bt_has_register_param *param, struct bt_has **out)
{
#if defined(CONFIG_BT_HAS)
	bool writable_presets_support = false;

	*out = &has_local;

	CHECKIF(param == NULL || param->ops == NULL || param->ops->active_set == NULL)
	{
		return -EINVAL;
	}

	/* Check if already registered */
	if (preset_ops) {
		return -EALREADY;
	}

	for (int i = 0; i < ARRAY_SIZE(preset_list); i++) {
		struct bt_has_preset_register_param *preset_param = NULL;
		struct preset *preset = &preset_list[i];

		/* Sort the presets in order of increasing ID */
		for (int j = 0; j < ARRAY_SIZE(param->preset_param); j++) {
			struct bt_has_preset_register_param *tmp = &param->preset_param[j];
			if ((!preset_param || tmp->id < preset_param->id) &&
			    tmp->id > last_preset_id) {
				preset_param = tmp;
			}
		}

		if (!preset_param) {
			break;
		}

		preset->id = preset_param->id;
		preset->properties = preset_param->properties;
#if defined(CONFIG_BT_HAS_PRESET_NAME_DYNAMIC)
		strncpy(preset->name, preset_param->name, ARRAY_SIZE(preset->name) - 1);
		preset->name[ARRAY_SIZE(preset->name) - 1] = '\0';
#else
		preset->name = preset_param->name;
#endif /* CONFIG_BT_HAS_PRESET_NAME_DYNAMIC */

		if (IS_ENABLED(CONFIG_BT_HAS_PRESET_NAME_DYNAMIC)) {
			/* If the server exposes at least one preset record with the Writable flag set,
				 * then the server shall set the Writable Presets Support flag. */
			writable_presets_support |= preset_param->properties & BT_HAS_PROP_WRITABLE;
		}

		last_preset_id = preset->id;
	}

	if (writable_presets_support) {
		has_local.features |= BT_HAS_FEAT_BIT_WRITABLE_PRESETS;
	}

	preset_ops = param->ops;

	return 0;
#else
	return -EOPNOTSUPP;
#endif /* CONFIG_BT_HAS */
}

int bt_has_preset_active_get(struct bt_has *has)
{
	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT) && !BT_HAS_IS_LOCAL(has)) {
		CHECKIF(has == NULL)
		{
			return -EINVAL;
		}

		return bt_has_client_preset_active_get(has);
	}

	return -EOPNOTSUPP;
}

int bt_has_preset_active_set(struct bt_has *has, uint8_t id)
{
	CHECKIF(has == NULL)
	{
		return -EINVAL;
	}

#if defined(CONFIG_BT_HAS)
	if (BT_HAS_IS_LOCAL(has)) {
		if (id == has_local.active_id) {
			/* no change */
			return 0;
		}

		if (id != BT_HAS_PRESET_INDEX_NONE) {
			struct preset *preset = preset_get(id);
			if (!preset) {
				return -ENOENT;
			}
		}

		has_local.active_id = id;

		k_work_submit(&active_preset_work);

		return 0;
	}
#endif /* CONFIG_BT_HAS */

	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT)) {
		return bt_has_client_preset_active_set(has, id);
	}

	return -EOPNOTSUPP;
}

int bt_has_preset_active_clear(struct bt_has *has)
{
	return bt_has_preset_active_set(has, BT_HAS_PRESET_INDEX_NONE);
}

int bt_has_preset_active_set_next(struct bt_has *has)
{
	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT) && !BT_HAS_IS_LOCAL(has)) {
		CHECKIF(has == NULL)
		{
			return -EINVAL;
		}

		return bt_has_client_preset_active_set_next(has);
	}

	return -EOPNOTSUPP;
}

int bt_has_preset_active_set_prev(struct bt_has *has)
{
	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT) && !BT_HAS_IS_LOCAL(has)) {
		CHECKIF(has == NULL)
		{
			return -EINVAL;
		}

		return bt_has_client_preset_active_set_prev(has);
	}

	return -EOPNOTSUPP;
}

int bt_has_preset_read(struct bt_has *has, struct bt_has_preset_read_params *params)
{
	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT) && !BT_HAS_IS_LOCAL(has)) {
		CHECKIF(has == NULL || params == NULL)
		{
			return -EINVAL;
		}

		return bt_has_client_preset_read(has, params);
	}

	return -EOPNOTSUPP;
}

int bt_has_preset_visibility_set(struct bt_has *has, uint8_t id, bool visible)
{
#if defined(CONFIG_BT_HAS)
	CHECKIF(has == NULL)
	{
		return -EINVAL;
	}

	if (BT_HAS_IS_LOCAL(has)) {
		struct preset *preset;

		preset = preset_get(id);
		if (!preset) {
			return -ENOENT;
		}

		if (preset->hidden == visible) {
			preset->hidden = !visible;
			preset_changed(preset, visible ? BT_HAS_CHANGE_ID_GENERIC_UPDATE :
							       BT_HAS_CHANGE_ID_PRESET_DELETED);
		}

		return 0;
	}
#endif /* CONFIG_BT_HAS */

	return -EOPNOTSUPP;
}

int bt_has_preset_availability_set(struct bt_has *has, uint8_t id, bool available)
{
#if defined(CONFIG_BT_HAS)
	CHECKIF(has == NULL)
	{
		return -EINVAL;
	}

	if (BT_HAS_IS_LOCAL(has)) {
		struct preset *preset;

		preset = preset_get(id);
		if (!preset) {
			return -ENOENT;
		}

		if (((preset->properties & BT_HAS_PROP_AVAILABLE) > 0) != available) {
			preset->properties ^= BT_HAS_PROP_AVAILABLE;

			/* Do not send preset changed notification if the preset is hidden */
			if (!preset->hidden) {
				preset_changed(preset, available ?
								     BT_HAS_CHANGE_ID_PRESET_AVAILABLE :
								     BT_HAS_CHANGE_ID_PRESET_UNAVAILABLE);
			}
		}

		return 0;
	}
#endif /* CONFIG_BT_HAS */

	return -EOPNOTSUPP;
}

int bt_has_preset_name_set(struct bt_has *has, uint8_t id, const char *name)
{
	CHECKIF(has == NULL || name == NULL)
	{
		return -EINVAL;
	}

#if defined(CONFIG_BT_HAS)
	if (BT_HAS_IS_LOCAL(has)) {
		return preset_name_set(id, name, strlen(name));
	}
#endif /* CONFIG_BT_HAS */

	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT)) {
		return bt_has_client_preset_name_set(has, id, name);
	}

	return -EOPNOTSUPP;
}
