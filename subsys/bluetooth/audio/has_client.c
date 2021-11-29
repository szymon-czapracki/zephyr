/*
 * Copyright (c) 2022 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/byteorder.h>
#include <sys/dlist.h>
#include <sys/check.h>

#include <device.h>
#include <init.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>
#include <bluetooth/audio/has.h>

#include "../host/conn_internal.h"

#include "has_internal.h"

#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_HAS)
#define LOG_MODULE_NAME bt_has_client
#include "common/log.h"

static struct bt_has_client has_clients[CONFIG_BT_MAX_CONN];
#define IS_HANDLE_VALID(handle) ((handle) != 0x0000)

#define FEATURES_MASK_HEARING_AID_TYPE (BIT(0) | BIT(1))
#define FEATURES_MASK_PRESET_SYNCHRONIZATION BIT(2)
#define FEATURES_MASK_INDEPENDENT_PRESETS BIT(3)
#define FEATURES_MASK_DYNAMIC_PRESETS BIT(4)
#define FEATURES_MASK_WRITABLE_PRESETS BIT(5)

static inline struct bt_has_client *client_by_conn(struct bt_conn *conn)
{
	return &has_clients[bt_conn_index(conn)];
}

static uint8_t client_preset_active_get_cb(struct bt_conn *conn, uint8_t err,
					   struct bt_gatt_read_params *params, const void *data,
					   uint16_t length)
{
	struct bt_has_client *client = client_by_conn(conn);

	client->busy = false;

	return BT_GATT_ITER_STOP;
}

static void client_preset_active_set_cb(struct bt_conn *conn, uint8_t err,
					struct bt_gatt_write_params *params)
{
	struct bt_has_client *client = client_by_conn(conn);

	client->busy = false;
}

static int client_control_point_write(struct bt_has_client *client, bt_gatt_write_func_t func,
				      const void *data, uint16_t length)
{
	uint16_t value_handle;
	int err;

	CHECKIF(client->conn == NULL)
	{
		return -ENOTCONN;
	}

	value_handle = client->control_point_subscription.value_handle;
	if (!IS_HANDLE_VALID(value_handle)) {
		return -ENOTSUP;
	}

	if (client->busy) {
		return -EBUSY;
	}

	client->write_params.func = func;
	client->write_params.handle = value_handle;
	client->write_params.offset = 0U;
	client->write_params.data = data;
	client->write_params.length = length;

	err = bt_gatt_write(client->conn, &client->write_params);
	if (err == 0) {
		return err;
	}

	client->busy = true;

	return 0;
}

static void client_preset_get_cb(struct bt_conn *conn, uint8_t err,
				 struct bt_gatt_write_params *params)
{
	struct bt_has_client *client = client_by_conn(conn);

	client->busy = false;
}

static void disc_complete(struct bt_has_client *client, bool success)
{
	struct bt_has_discover_params *params = client->discover;

	client->busy = false;
	client->discover = NULL;

	if (success) {
		params->func(client->conn, &client->has, params);
	} else {
		params->func(client->conn, NULL, params);
	}
}

static uint8_t active_preset_index_ntf_cb(struct bt_conn *conn,
					  struct bt_gatt_subscribe_params *params, const void *data,
					  uint16_t length)
{
	struct bt_has_client *client = client_by_conn(conn);

	ARG_UNUSED(client);

	return BT_GATT_ITER_CONTINUE;
}

static void cfg_active_preset_index_ntf_cb(struct bt_conn *conn, uint8_t err,
					   struct bt_gatt_write_params *write)
{
	struct bt_has_client *client = client_by_conn(conn);
	struct bt_has_discover_params *params = client->discover;

	BT_DBG("conn %p params %p err 0x%02x", client->conn, params, err);

	disc_complete(client, err == 0);
}

static int cfg_active_preset_index_ntf(struct bt_has_client *client, uint16_t handle)
{
	struct bt_has_discover_params *params = client->discover;

	BT_DBG("conn %p params %p handle 0x%04x", client->conn, params, handle);

	client->active_preset_subscription.notify = active_preset_index_ntf_cb;
	client->active_preset_subscription.write = cfg_active_preset_index_ntf_cb;
	client->active_preset_subscription.value_handle = handle;
	client->active_preset_subscription.ccc_handle = 0x0000;
	client->active_preset_subscription.end_handle = 0xffff;
	client->active_preset_subscription.disc_params = &params->discover;
	client->active_preset_subscription.value = BT_GATT_CCC_NOTIFY;
	atomic_set_bit(client->active_preset_subscription.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	return bt_gatt_subscribe(client->conn, &client->active_preset_subscription);
}

static uint8_t disc_active_preset_index_cb(struct bt_conn *conn, uint8_t err,
					   struct bt_gatt_read_params *read, const void *data,
					   uint16_t length)
{
	struct bt_has_client *client = client_by_conn(conn);
	struct net_buf_simple buf;

	BT_DBG("conn %p err 0x%02x len %u", conn, err, length);

	if (err || length == 0) {
		disc_complete(client, false);
		return BT_GATT_ITER_STOP;
	}

	BT_DBG("handle 0x%04x", read->by_uuid.start_handle);

	net_buf_simple_init_with_data(&buf, (void *)data, length);

	client->active_preset_index = net_buf_simple_pull_u8(&buf);

	if (cfg_active_preset_index_ntf(client, read->by_uuid.start_handle) < 0) {
		disc_complete(client, false);
	}

	return BT_GATT_ITER_STOP;
}

static int disc_active_preset_index(struct bt_has_client *client)
{
	struct bt_has_discover_params *params = client->discover;

	BT_DBG("conn %p params %p", client->conn, params);

	params->read.func = disc_active_preset_index_cb;
	params->read.handle_count = 0u;
	memcpy(&params->uuid, BT_UUID_HAS_ACTIVE_PRESET_INDEX, sizeof(params->uuid));
	params->read.by_uuid.uuid = &params->uuid.uuid;
	params->read.by_uuid.start_handle = 0x0001;
	params->read.by_uuid.end_handle = 0xffff;

	return bt_gatt_read(client->conn, &client->discover->read);
}

static uint8_t preset_control_point_ntf_cb(struct bt_conn *conn,
					   struct bt_gatt_subscribe_params *params,
					   const void *data, uint16_t length)
{
	struct bt_has_client *client = client_by_conn(conn);

	ARG_UNUSED(client);

	return BT_GATT_ITER_CONTINUE;
}

static void cfg_preset_control_point_ntf_cb(struct bt_conn *conn, uint8_t err,
					    struct bt_gatt_write_params *write)
{
	struct bt_has_client *client = client_by_conn(conn);
	struct bt_has_discover_params *params = client->discover;

	BT_DBG("conn %p params %p err 0x%02x", client->conn, params, err);

	if (err || disc_active_preset_index(client) < 0) {
		disc_complete(client, false);
	}
}

static int cfg_preset_control_point_ntf(struct bt_has_client *client, uint16_t handle)
{
	struct bt_has_discover_params *params = client->discover;

	BT_DBG("conn %p params %p handle 0x%04x", client->conn, params, handle);

	client->control_point_subscription.notify = preset_control_point_ntf_cb;
	client->control_point_subscription.write = cfg_preset_control_point_ntf_cb;
	client->control_point_subscription.value_handle = handle;
	client->control_point_subscription.ccc_handle = 0x0000;
	client->control_point_subscription.end_handle = 0xffff;
	client->control_point_subscription.disc_params = &params->discover;
	client->control_point_subscription.value = BT_GATT_CCC_INDICATE;
	atomic_set_bit(client->control_point_subscription.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	return bt_gatt_subscribe(client->conn, &client->control_point_subscription);
}

static uint8_t disc_preset_control_point_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
					    struct bt_gatt_discover_params *discover)
{
	struct bt_has_client *client = client_by_conn(conn);
	struct bt_has_discover_params *params = client->discover;
	struct bt_gatt_chrc *chrc;

	BT_DBG("conn %p params %p attr %p", client->conn, params, attr);

	if (!attr) {
		BT_INFO("HAS Control Point not found");
		disc_complete(client, true);
		return BT_GATT_ITER_STOP;
	}

	chrc = attr->user_data;

	if (cfg_preset_control_point_ntf(client, chrc->value_handle) < 0) {
		disc_complete(client, false);
	}

	return BT_GATT_ITER_STOP;
}

static int disc_preset_control_point(struct bt_has_client *client)
{
	struct bt_has_discover_params *params = client->discover;

	BT_DBG("conn %p params %p", client->conn, params);

	memcpy(&params->uuid, BT_UUID_HAS_CONTROL_POINT, sizeof(params->uuid));
	params->discover.uuid = &params->uuid.uuid;
	params->discover.func = disc_preset_control_point_cb;
	params->discover.start_handle = 0x0001;
	params->discover.end_handle = 0xffff;
	params->discover.type = BT_GATT_DISCOVER_CHARACTERISTIC;

	return bt_gatt_discover(client->conn, &client->discover->discover);
}

static uint8_t hearing_aid_features_ntf_cb(struct bt_conn *conn,
					   struct bt_gatt_subscribe_params *params,
					   const void *data, uint16_t length)
{
	struct bt_has_client *client = client_by_conn(conn);

	ARG_UNUSED(client);

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t read_hearing_aid_features_cb(struct bt_conn *conn, uint8_t err,
					    struct bt_gatt_read_params *read, const void *data,
					    uint16_t len)
{
	struct bt_has_client *client = client_by_conn(conn);
	struct bt_has_discover_params *params = client->discover;
	struct net_buf_simple buf;

	BT_DBG("conn %p params %p err 0x%02x len %u", conn, params, err, len);

	if (err || len == 0) {
		disc_complete(client, false);
		return BT_GATT_ITER_STOP;
	}

	net_buf_simple_init_with_data(&buf, (void *)data, len);

	client->has.features = net_buf_simple_pull_u8(&buf);

	BT_DBG("handle 0x%04x", read->by_uuid.start_handle);

	if (disc_preset_control_point(client) < 0) {
		disc_complete(client, false);
	}

	return BT_GATT_ITER_STOP;
}

static int read_hearing_aid_features(struct bt_has_client *client, uint16_t value_handle)
{
	struct bt_has_discover_params *params = client->discover;

	BT_DBG("conn %p params %p handle 0x%04x", client->conn, client->discover, value_handle);

	params->read.func = read_hearing_aid_features_cb;
	params->read.handle_count = 1u;
	params->read.single.handle = value_handle;
	params->read.single.offset = 0u;

	return bt_gatt_read(client->conn, &params->read);
}

static void cfg_hearing_aid_features_ntf_cb(struct bt_conn *conn, uint8_t err,
					    struct bt_gatt_write_params *write)
{
	struct bt_has_client *client = client_by_conn(conn);
	struct bt_has_discover_params *params = client->discover;
	uint16_t handle = client->features_subscription.value_handle;

	BT_DBG("conn %p params %p err 0x%02x", client->conn, params, err);

	if (err || read_hearing_aid_features(client, handle) < 0) {
		disc_complete(client, false);
	}
}

static int cfg_hearing_aid_features_ntf(struct bt_has_client *client, uint16_t handle)
{
	struct bt_has_discover_params *params = client->discover;

	BT_DBG("conn %p params %p handle 0x%04x", client->conn, params, handle);

	client->features_subscription.notify = hearing_aid_features_ntf_cb;
	client->features_subscription.write = cfg_hearing_aid_features_ntf_cb;
	client->features_subscription.value_handle = handle;
	client->features_subscription.ccc_handle = 0x0000;
	client->features_subscription.end_handle = 0xffff;
	client->features_subscription.disc_params = &params->discover;
	client->features_subscription.value = BT_GATT_CCC_NOTIFY;
	atomic_set_bit(client->features_subscription.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	return bt_gatt_subscribe(client->conn, &client->features_subscription);
}

static uint8_t disc_hearing_aid_features_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
					    struct bt_gatt_discover_params *discover)
{
	struct bt_has_client *client = client_by_conn(conn);
	struct bt_has_discover_params *params = client->discover;
	struct bt_gatt_chrc *chrc;
	int err;

	BT_DBG("conn %p params %p attr %p", conn, params, attr);

	if (!attr) {
		disc_complete(client, false);
		return BT_GATT_ITER_STOP;
	}

	chrc = attr->user_data;

	if (chrc->properties & BT_GATT_CHRC_NOTIFY) {
		err = cfg_hearing_aid_features_ntf(client, chrc->value_handle);
	} else {
		err = read_hearing_aid_features(client, chrc->value_handle);
	}

	if (err < 0) {
		disc_complete(client, false);
	}

	return BT_GATT_ITER_STOP;
}

static int disc_hearing_aid_features(struct bt_has_client *client)
{
	struct bt_has_discover_params *params = client->discover;

	BT_DBG("conn %p params %p", client->conn, client->discover);

	memcpy(&params->uuid, BT_UUID_HAS_FEATURES, sizeof(params->uuid));
	params->discover.uuid = &params->uuid.uuid;
	params->discover.func = disc_hearing_aid_features_cb;
	params->discover.start_handle = 0x0001;
	params->discover.end_handle = 0xffff;
	params->discover.type = BT_GATT_DISCOVER_CHARACTERISTIC;

	return bt_gatt_discover(client->conn, &params->discover);
}

int bt_has_client_preset_active_get(struct bt_has *has)
{
	struct bt_has_client *client = BT_HAS_CLIENT(has);
	uint16_t value_handle;
	int err;

	CHECKIF(client->conn == NULL)
	{
		return -ENOTCONN;
	}

	value_handle = client->active_preset_subscription.value_handle;
	if (!IS_HANDLE_VALID(value_handle)) {
		return -ENOTSUP;
	}

	if (client->busy) {
		return -EBUSY;
	}

	client->read_params.func = client_preset_active_get_cb;
	client->read_params.handle_count = 1u;
	client->read_params.single.handle = value_handle;
	client->read_params.single.offset = 0u;

	err = bt_gatt_read(client->conn, &client->read_params);
	if (err) {
		return err;
	}

	client->busy = true;

	return 0;
}

int bt_has_client_preset_active_set(struct bt_has *has, uint8_t index)
{
	struct bt_has_client *client = BT_HAS_CLIENT(has);
	struct bt_has_cp_hdr *hdr;
	struct bt_has_cp_set_active_preset_req *req;
	uint8_t buf[sizeof(*hdr) + sizeof(*req)];

	hdr = (void *)buf;
	hdr->op = BT_HAS_OP_SET_ACTIVE_PRESET;
	req = (void *)hdr->data;
	req->index = index;

	return client_control_point_write(client, client_preset_active_set_cb, buf, sizeof(buf));
}

int bt_has_client_preset_active_set_next(struct bt_has *has)
{
	struct bt_has_client *client = BT_HAS_CLIENT(has);
	struct bt_has_cp_hdr hdr = {
		.op = BT_HAS_OP_SET_NEXT_PRESET,
	};

	return client_control_point_write(client, client_preset_active_set_cb, &hdr, sizeof(hdr));
}

int bt_has_client_preset_active_set_prev(struct bt_has *has)
{
	struct bt_has_client *client = BT_HAS_CLIENT(has);
	struct bt_has_cp_hdr hdr = {
		.op = BT_HAS_OP_SET_PREV_PRESET,
	};

	return client_control_point_write(client, client_preset_active_set_cb, &hdr, sizeof(hdr));
}

int bt_has_client_preset_get(struct bt_has *has, uint8_t index)
{
	struct bt_has_client *client = BT_HAS_CLIENT(has);
	struct bt_has_cp_hdr *hdr;
	struct bt_has_cp_read_preset_by_index_req *req;
	uint8_t buf[sizeof(*hdr) + sizeof(*req)];

	hdr = (void *)buf;
	hdr->op = BT_HAS_OP_READ_PRESET_BY_INDEX;
	req = (void *)hdr->data;
	req->index = index;

	return client_control_point_write(client, client_preset_get_cb, buf, sizeof(buf));
}

int bt_has_client_preset_name_set(struct bt_has *has, uint8_t index, const char *name, ssize_t len)
{
	struct bt_has_client *client = BT_HAS_CLIENT(has);
	struct bt_has_cp_hdr *hdr;
	struct bt_has_cp_write_preset_name_req *req;
	uint8_t buf[sizeof(*hdr) + sizeof(*req) + len];

	if (len < BT_HAS_PRESET_NAME_MIN || len > BT_HAS_PRESET_NAME_MAX) {
		return -EINVAL;
	}

	hdr = (void *)buf;
	hdr->op = BT_HAS_OP_WRITE_PRESET_NAME;
	req = (void *)hdr->data;
	req->index = index;
	strncpy(req->name, name, len);

	return client_control_point_write(client, client_preset_active_set_cb, buf, sizeof(buf));
}

int bt_has_client_preset_list_get(struct bt_has *has)
{
	struct bt_has_client *client = BT_HAS_CLIENT(has);
	struct bt_has_cp_hdr hdr = {
		.op = BT_HAS_OP_SET_PREV_PRESET,
	};

	return client_control_point_write(client, client_preset_active_set_cb, &hdr, sizeof(hdr));
}

int bt_has_discover(struct bt_conn *conn, struct bt_has_discover_params *params)
{
	struct bt_has_client *client = client_by_conn(conn);
	int err;

	BT_DBG("conn %p params %p", conn, params);

	if (!conn || conn->state != BT_CONN_CONNECTED) {
		return -ENOTCONN;
	}

	if (client->busy) {
		return -EBUSY;
	}

	client->busy = true;
	client->conn = conn;
	client->discover = params;

	err = disc_hearing_aid_features(client);
	if (err) {
		client->busy = false;
		client->conn = NULL;
		client->discover = NULL;
	}

	return err;
}
