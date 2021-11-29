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

#include <bluetooth/audio/has.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>

#include "has_internal.h"

#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_HAS)
#define LOG_MODULE_NAME bt_has
#include "common/log.h"

#define IS_HANDLE_VALID(handle) ((handle) != 0x0000)
#define IS_PRESET_VALID(preset) ((preset)->index != BT_HAS_PRESET_INDEX_NONE)

#define FEATURES_MASK_HEARING_AID_TYPE (BIT(0) | BIT(1))
#define FEATURES_MASK_PRESET_SYNCHRONIZATION BIT(2)
#define FEATURES_MASK_INDEPENDENT_PRESETS BIT(3)
#define FEATURES_MASK_DYNAMIC_PRESETS BIT(4)
#define FEATURES_MASK_WRITABLE_PRESETS BIT(5)

static struct bt_has_server has_server;

static ssize_t write_control_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				   const void *data, uint16_t len, uint16_t offset, uint8_t flags);

static ssize_t read_features(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			     uint16_t len, uint16_t offset)
{
	struct bt_has_server *has = attr->user_data;

	BT_DBG("conn %p attr %p offset %d", conn, attr, offset);

	if (offset > 0) {
		return BT_GATT_ERR(BT_ATT_ERR_ATTRIBUTE_NOT_LONG);
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &has->has.features, 1);
}

static ssize_t read_active_preset_index(struct bt_conn *conn, const struct bt_gatt_attr *attr,
					void *buf, uint16_t len, uint16_t offset)
{
	struct bt_has_server *server = attr->user_data;
	uint8_t index;

	BT_DBG("conn %p attr %p offset %d", conn, attr, offset);

	if (offset > 0) {
		return BT_GATT_ERR(BT_ATT_ERR_ATTRIBUTE_NOT_LONG);
	}

	if (server->active_preset != NULL) {
		index = server->active_preset->index;
	} else {
		index = BT_HAS_PRESET_INDEX_NONE;
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &index, 1);
}

/* Hearing Access Service GATT Attributes */
BT_GATT_SERVICE_DEFINE(
	has_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HAS),
	BT_GATT_CHARACTERISTIC(BT_UUID_HAS_FEATURES, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT,
			       read_features, NULL, &has_server),
	BT_GATT_CHARACTERISTIC(BT_UUID_HAS_CONTROL_POINT,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_WRITE_ENCRYPT, NULL, write_control_point, &has_server),
	BT_GATT_CHARACTERISTIC(BT_UUID_HAS_ACTIVE_PRESET_INDEX,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ_ENCRYPT,
			       read_active_preset_index, NULL, &has_server), );

static struct bt_has_preset *server_preset_lookup(struct bt_has_server *server, uint8_t index)
{
	struct bt_has_preset *preset;

	SYS_DLIST_FOR_EACH_CONTAINER (&server->preset_list, preset, node) {
		if (preset->index == index) {
			return preset;
		}
	}

	return NULL;
}

static int server_preset_get(struct bt_has_server *server, uint8_t index)
{
	// TODO

	return 0;
}

static int preset_name_set(struct bt_has_server *server, uint8_t index, const char *name,
			   uint8_t len)
{
	struct bt_has_preset *preset;

	if (len < BT_HAS_PRESET_NAME_MIN || len > BT_HAS_PRESET_NAME_MAX) {
		return BT_GATT_ERR(BT_HAS_ERR_INVALID_PARAM_LEN);
	}

	preset = server_preset_lookup(server, index);
	if (!preset) {
		return BT_GATT_ERR(BT_ATT_ERR_OUT_OF_RANGE);
	}

	if (!(preset->properties & BT_HAS_PROP_WRITABLE)) {
		return BT_GATT_ERR(BT_HAS_ERR_WRITE_NAME_NOT_ALLOWED);
	}

	// TODO: save and emit preset changed

	return 0;
}

static int server_preset_list_get(struct bt_has_server *server)
{
	// TODO

	return 0;
}

static int read_preset_rsp(struct bt_conn *conn, struct bt_has_server *server,
			   struct bt_has_preset *preset, uint8_t is_last)
{
	struct bt_has_cp_hdr *hdr;
	struct bt_has_cp_read_preset_rsp *rsp;
	const uint8_t slen = strlen(preset->name);
	uint8_t buf[sizeof(*hdr) + sizeof(*rsp) + slen];

	hdr = (void *)buf;
	hdr->op = BT_HAS_OP_READ_PRESET_RSP;
	rsp = (void *)hdr->data;
	rsp->is_last = is_last;
	rsp->index = preset->index;
	rsp->properties = preset->properties;
	strncpy(rsp->name, preset->name, slen);

	return bt_gatt_notify_uuid(conn, BT_UUID_HAS_CONTROL_POINT, has_svc.attrs, buf,
				   sizeof(buf));
}

static int read_all_presets(struct bt_conn *conn, struct bt_has_server *server,
			    struct net_buf_simple *buf)
{
	struct bt_has_preset *preset;

	if (sys_dlist_is_empty(&server->preset_list)) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	SYS_DLIST_FOR_EACH_CONTAINER (&server->preset_list, preset, node) {
		uint8_t is_last;
		int err;

		is_last = sys_dlist_peek_next(&server->preset_list, &preset->node) ? 0x00 : 0x01;

		err = read_preset_rsp(conn, server, preset, is_last);
		if (err != 0) {
			return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
		}
	}

	return 0;
}

static int read_preset_by_index(struct bt_conn *conn, struct bt_has_server *server,
				struct net_buf_simple *buf)
{
	const struct bt_has_cp_read_preset_by_index_req *req;
	struct bt_has_preset *preset;
	int err;

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_HAS_ERR_INVALID_PARAM_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	preset = server_preset_lookup(server, req->index);
	if (!preset) {
		return BT_GATT_ERR(BT_ATT_ERR_OUT_OF_RANGE);
	}

	err = read_preset_rsp(conn, server, preset, 0x01);
	if (err != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	return 0;
}

static int write_preset_name(struct bt_conn *conn, struct bt_has_server *server,
			     struct net_buf_simple *buf)
{
	const struct bt_has_cp_write_preset_name_req *req;

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_HAS_ERR_INVALID_PARAM_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	return preset_name_set(server, req->index, req->name, buf->len);
}

static int set_active_preset(struct bt_conn *conn, struct bt_has_server *server,
			     struct net_buf_simple *buf, bool sync)
{
	const struct bt_has_cp_set_active_preset_req *req;
	struct bt_has_preset *preset;
	int err;

	if (!server->preset_ops) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_HAS_ERR_INVALID_PARAM_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	preset = server_preset_lookup(server, req->index);
	if (!preset) {
		return BT_GATT_ERR(BT_ATT_ERR_OUT_OF_RANGE);
	}

	if (!(preset->properties & BT_HAS_PROP_AVAILABLE)) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	err = server->preset_ops->active_set(&server->has, preset->index, sync);
	if (err != 0) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	return 0;
}

typedef struct bt_has_preset *(*preset_peek_func_t)(struct bt_has_server *server,
						    struct bt_has_preset *preset);

static struct bt_has_preset *preset_peek_next(struct bt_has_server *server,
					      struct bt_has_preset *preset)
{
	sys_dnode_t *node;

	if (preset == NULL) {
		node = sys_dlist_peek_head(&server->preset_list);
	} else {
		node = sys_dlist_peek_next_no_check(&server->preset_list, &preset->node);
	}

	if (!node) {
		return NULL;
	}

	return CONTAINER_OF(node, struct bt_has_preset, node);
}

static struct bt_has_preset *preset_peek_prev(struct bt_has_server *server,
					      struct bt_has_preset *preset)
{
	sys_dnode_t *node;

	if (preset == NULL) {
		node = sys_dlist_peek_tail(&server->preset_list);
	} else {
		node = sys_dlist_peek_prev_no_check(&server->preset_list, &preset->node);
	}

	if (!node) {
		return NULL;
	}

	return CONTAINER_OF(node, struct bt_has_preset, node);
}

static struct bt_has_preset *preset_peek_available(struct bt_has_server *server,
						   struct bt_has_preset *curr,
						   preset_peek_func_t func)
{
	struct bt_has_preset *preset;

	for (preset = func(server, curr); preset != curr; preset = func(server, preset)) {
		if (preset && preset->properties & BT_HAS_PROP_AVAILABLE) {
			return preset;
		}
	}

	return NULL;
}

static int set_next_preset(struct bt_conn *conn, struct bt_has_server *server,
			   struct net_buf_simple *buf, bool sync)
{
	struct bt_has_preset *preset;
	int err;

	if (server->preset_ops == NULL) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	preset = preset_peek_available(server, server->active_preset, preset_peek_next);
	if (preset == NULL) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	err = server->preset_ops->active_set(&server->has, preset->index, sync);
	if (err != 0) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	return 0;
}

static int set_prev_preset(struct bt_conn *conn, struct bt_has_server *server,
			   struct net_buf_simple *buf, bool sync)
{
	struct bt_has_preset *preset;
	int err;

	if (!server->preset_ops) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	preset = preset_peek_available(server, server->active_preset, preset_peek_prev);
	if (preset == NULL) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	err = server->preset_ops->active_set(&server->has, preset->index, sync);
	if (err != 0) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	return 0;
}

static ssize_t write_control_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				   const void *data, uint16_t len, uint16_t offset, uint8_t flags)
{
	struct bt_has_server *server = attr->user_data;
	const struct bt_has_cp_hdr *hdr;
	struct net_buf_simple buf;
	ssize_t ret;

	if (offset > 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len < sizeof(*hdr)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	net_buf_simple_init_with_data(&buf, (void *)data, len);

	hdr = net_buf_simple_pull_mem(&buf, sizeof(*hdr));

	BT_DBG("conn %p attr %p buf %p len %u op %s (0x%02x)", conn, attr, data, len,
	       bt_has_op_str(hdr->op), hdr->op);

	switch (hdr->op) {
	case BT_HAS_OP_READ_ALL_PRESETS:
		ret = read_all_presets(conn, server, &buf);
		break;

	case BT_HAS_OP_READ_PRESET_BY_INDEX:
		ret = read_preset_by_index(conn, server, &buf);
		break;

	case BT_HAS_OP_WRITE_PRESET_NAME:
		ret = write_preset_name(conn, server, &buf);
		break;

	case BT_HAS_OP_SET_ACTIVE_PRESET:
		ret = set_active_preset(conn, server, &buf, false);
		break;

	case BT_HAS_OP_SET_NEXT_PRESET:
		ret = set_next_preset(conn, server, &buf, false);
		break;

	case BT_HAS_OP_SET_PREV_PRESET:
		ret = set_prev_preset(conn, server, &buf, false);
		break;

#if defined(CONFIG_BT_HAS_HA_PRESET_SYNC_SUPPORT)
	case BT_HAS_OP_SET_ACTIVE_PRESET_SYNC:
		ret = set_active_preset(conn, server, &buf, true);
		break;

	case BT_HAS_OP_SET_NEXT_PRESET_SYNC:
		ret = set_next_preset(conn, server, &buf, true);
		break;

	case BT_HAS_OP_SET_PREV_PRESET_SYNC:
		ret = set_prev_preset(conn, server, &buf, true);
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

	if (ret < 0) {
		return ret;
	}

	return len;
}

static inline bool is_server(const struct bt_has *has)
{
#if defined(CONFIG_BT_HAS)
	return has == &has_server.has;
#endif
	return false;
}

static void has_preset_insert(struct bt_has_server *server, struct bt_has_preset *preset)
{
	struct bt_has_preset *tmp, *prev = NULL;

	/* Preset record list shall always have its entries in ascending order */
	SYS_DLIST_FOR_EACH_CONTAINER (&server->preset_list, tmp, node) {
		if (tmp->index > preset->index) {
			if (prev) {
				sys_dlist_insert(&prev->node, &preset->node);
			} else {
				sys_dlist_prepend(&server->preset_list, &preset->node);
			}

			return;
		}

		prev = tmp;
	}
}

int bt_has_register(struct bt_has_preset_ops *ops, struct bt_has **has)
{
	int err;

	CHECKIF(ops == NULL || ops->active_set == NULL)
	{
		return -EINVAL;
	}

	/* Check if already registered */
	err = has_server.preset_ops ? -EALREADY : 0;
	if (err == 0) {
		has_server.preset_ops = ops;
	}

	*has = &has_server.has;

	return err;
}

int bt_has_preset_active_get(struct bt_has *has)
{
	CHECKIF(has == NULL)
	{
		return -EINVAL;
	}

	if (is_server(has)) {
		return -EOPNOTSUPP;
	}

	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT)) {
		return bt_has_client_preset_active_get(has);
	}

	return -EOPNOTSUPP;
}

int bt_has_preset_active_set(struct bt_has *has, uint8_t index)
{
	CHECKIF(has == NULL)
	{
		return -EINVAL;
	}

	if (is_server(has)) {
		struct bt_has_server *server = BT_HAS_SERVER(has);
		struct bt_has_preset *preset;

		preset = server_preset_lookup(server, index);
		if (!preset) {
			return -ENOENT;
		}

		server->active_preset = preset;

		return bt_gatt_notify_uuid(NULL, BT_UUID_HAS_ACTIVE_PRESET_INDEX, has_svc.attrs,
					   &server->active_preset->index, 1);
	}

	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT)) {
		return bt_has_client_preset_active_set(has, index);
	}

	return -EOPNOTSUPP;
}

int bt_has_preset_active_set_next(struct bt_has *has)
{
	CHECKIF(has == NULL)
	{
		return -EINVAL;
	}

	if (is_server(has)) {
		return -EOPNOTSUPP;
	}

	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT)) {
		return bt_has_client_preset_active_set_next(has);
	}

	return -EOPNOTSUPP;
}

int bt_has_preset_active_set_prev(struct bt_has *has)
{
	CHECKIF(has == NULL)
	{
		return -EINVAL;
	}

	if (is_server(has)) {
		return -EOPNOTSUPP;
	}

	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT)) {
		return bt_has_client_preset_active_set_prev(has);
	}

	return -EOPNOTSUPP;
}

int bt_has_preset_get(struct bt_has *has, uint8_t index)
{
	CHECKIF(has == NULL)
	{
		return -EINVAL;
	}

	if (is_server(has)) {
		return server_preset_get(BT_HAS_SERVER(has), index);
	}

	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT)) {
		return bt_has_client_preset_get(has, index);
	}

	return -EOPNOTSUPP;
}

int bt_has_preset_name_set(struct bt_has *has, uint8_t index, const char *name)
{
	CHECKIF(has == NULL || name == NULL)
	{
		return -EINVAL;
	}

	if (is_server(has)) {
		return preset_name_set(BT_HAS_SERVER(has), index, name, strlen(name));
	}

	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT)) {
		return bt_has_client_preset_name_set(has, index, name, strlen(name));
	}

	return -EOPNOTSUPP;
}

int bt_has_preset_list_get(struct bt_has *has)
{
	CHECKIF(has == NULL)
	{
		return -EINVAL;
	}

	if (is_server(has)) {
		return server_preset_list_get(BT_HAS_SERVER(has));
	}

	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT)) {
		return bt_has_client_preset_list_get(has);
	}

	return -EOPNOTSUPP;
}

static int has_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	if (IS_ENABLED(CONFIG_BT_HAS)) {
		struct bt_has_server *server = &has_server;

		sys_dlist_init(&server->preset_list);

		/* Sort the preset records */
		STRUCT_SECTION_FOREACH(bt_has_preset, preset)
		{
			has_preset_insert(server, preset);
		}
	}

	return 0;
}

SYS_INIT(has_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
