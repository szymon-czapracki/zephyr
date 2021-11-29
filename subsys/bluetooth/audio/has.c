/*
 * Copyright (c) 2022 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/byteorder.h>
#include <sys/check.h>

#include <device.h>
#include <init.h>

#include <bluetooth/audio/has.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>

#include "has_internal.h"
#include "../host/conn_internal.h"

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

#if defined(CONFIG_BT_HAS)
static struct bt_has has;
static struct bt_has_preset_ops *preset_ops;
static struct bt_has_preset *preset_first;
static struct bt_has_preset *preset_last;
static struct k_work active_preset_work;
static struct bt_has_read_preset_work_ctx {
	struct bt_conn *conn;
	uint8_t start_index;
	uint8_t num_presets;
	struct k_work work;
} read_preset_ctx[CONFIG_BT_MAX_CONN];

static ssize_t cp_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *data,
			uint16_t len, uint16_t offset, uint8_t flags);

static ssize_t read_features(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			     uint16_t len, uint16_t offset)
{
	BT_DBG("conn %p attr %p offset %d", conn, attr, offset);

	if (offset > 0) {
		return BT_GATT_ERR(BT_ATT_ERR_ATTRIBUTE_NOT_LONG);
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &has.features, 1);
}

static ssize_t read_active_preset_index(struct bt_conn *conn, const struct bt_gatt_attr *attr,
					void *buf, uint16_t len, uint16_t offset)
{
	BT_DBG("conn %p attr %p offset %d", conn, attr, offset);

	if (offset > 0) {
		return BT_GATT_ERR(BT_ATT_ERR_ATTRIBUTE_NOT_LONG);
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &has.active_preset_index, 1);
}

static void ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	BT_DBG("attr %p value 0x%04x", attr, value);
}

/* Hearing Access Service GATT Attributes */
BT_GATT_SERVICE_DEFINE(
	has_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HAS),
	BT_GATT_CHARACTERISTIC(BT_UUID_HAS_FEATURES, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT,
			       read_features, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_HAS_CONTROL_POINT,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_WRITE_ENCRYPT, NULL, cp_write, NULL),
	BT_GATT_CCC(ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
	BT_GATT_CHARACTERISTIC(BT_UUID_HAS_ACTIVE_PRESET_INDEX,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ_ENCRYPT,
			       read_active_preset_index, NULL, NULL),
	BT_GATT_CCC(ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT));

typedef struct bt_has_preset *(*preset_get_func_t)(uint8_t index);

static struct bt_has_preset *preset_get(uint8_t index)
{
	STRUCT_SECTION_FOREACH(bt_has_preset, iter)
	{
		if (iter->index == index) {
			return iter;
		}
	}

	return NULL;
}

static struct bt_has_preset *preset_get_next(uint8_t index)
{
	struct bt_has_preset *next = preset_last;

	if (preset_last && index == preset_last->index) {
		return NULL;
	}

	STRUCT_SECTION_FOREACH(bt_has_preset, iter)
	{
		if (iter->index > index && iter->index < next->index) {
			next = iter;
		}
	}

	return next;
}

static struct bt_has_preset *preset_get_prev(uint8_t index)
{
	struct bt_has_preset *prev = preset_first;

	if (preset_first && index == preset_first->index) {
		return NULL;
	}

	STRUCT_SECTION_FOREACH(bt_has_preset, iter)
	{
		if (iter->index < index && iter->index > prev->index) {
			prev = iter;
		}
	}

	return prev;
}

static inline uint8_t preset_get_index(const struct bt_has_preset *preset)
{
	if (preset) {
		return preset->index;
	} else {
		return BT_HAS_PRESET_INDEX_NONE;
	}
}

static const struct bt_has_preset *preset_get_available(preset_get_func_t func)
{
	const struct bt_has_preset *active = preset_get(has.active_preset_index);
	const struct bt_has_preset *preset = active;

	do {
		preset = func(preset_get_index(preset));
		if (preset && preset->visible && (preset->properties & BT_HAS_PROP_AVAILABLE)) {
			return preset;
		}

	} while (preset != active);

	return active;
}

static void active_preset_work_process(struct k_work *work)
{
	bt_gatt_notify_uuid(NULL, BT_UUID_HAS_ACTIVE_PRESET_INDEX, has_svc.attrs,
			    &has.active_preset_index, 1);
}

static int preset_active_set(uint8_t index)
{
	if (index == has.active_preset_index) {
		/* no change */
		return 0;
	}

	if (index != BT_HAS_PRESET_INDEX_NONE) {
		struct bt_has_preset *preset;

		preset = preset_get(index);
		if (!preset) {
			return -ENOENT;
		}
	}

	has.active_preset_index = index;

	k_work_submit(&active_preset_work);

	return 0;
}

static int preset_visibility_set(uint8_t index, bool visible)
{
	struct bt_has_preset *preset;

	preset = preset_get(index);
	if (!preset) {
		return -ENOENT;
	}

	preset->visible = visible;

	// TODO: save and emit preset deleted/added

	return 0;
}

static void read_preset_rsp_sent(struct bt_conn *conn, void *user_data)
{
	struct bt_has_read_preset_work_ctx *ctx = &read_preset_ctx[bt_conn_index(conn)];

	if (ctx->num_presets > 0) {
		k_work_submit(&ctx->work);
	} else {
		bt_conn_unref(ctx->conn);
	}
}

static int read_preset_rsp(struct bt_conn *conn, struct bt_has_preset *preset, bool is_last)
{
	struct bt_gatt_notify_params params;
	struct bt_has_cp_hdr *hdr;
	struct bt_has_cp_read_preset_rsp *rsp;
	const uint8_t slen = strlen(preset->name);
	uint8_t buf[sizeof(*hdr) + sizeof(*rsp) + slen];

	BT_DBG("conn %p index %d is_last %d", conn, preset->index, is_last);

	hdr = (void *)buf;
	hdr->op = BT_HAS_OP_READ_PRESET_RSP;
	rsp = (void *)hdr->data;
	rsp->is_last = is_last ? 0x01 : 0x00;
	rsp->index = preset->index;
	rsp->properties = preset->properties;
	strncpy(rsp->name, preset->name, slen);

	memset(&params, 0, sizeof(params));

	params.uuid = BT_UUID_HAS_CONTROL_POINT;
	params.attr = has_svc.attrs;
	params.data = buf;
	params.len = sizeof(buf);
	params.func = read_preset_rsp_sent;

	return bt_gatt_notify_cb(conn, &params);
}

static void read_preset_work_process(struct k_work *work)
{
	struct bt_has_read_preset_work_ctx *ctx =
		CONTAINER_OF(work, struct bt_has_read_preset_work_ctx, work);
	struct bt_has_preset *preset, *next;
	int err;

	if (ctx->conn->state != BT_CONN_CONNECTED) {
		goto fail;
	}

	preset = preset_get_next(ctx->start_index);
	next = preset_get_next(preset->index);
	if (!next) {
		ctx->num_presets = 0;
	} else {
		ctx->start_index = next->index - 1; // -1 to include the index in search
		ctx->num_presets--;
	}

	err = read_preset_rsp(ctx->conn, preset, ctx->num_presets == 0);
	if (err < 0) {
		BT_ERR("failed (err %d)", err);
		goto fail;
	}

	return;
fail:
	bt_conn_unref(ctx->conn);
}

static int cp_read_preset_req(struct bt_conn *conn, struct net_buf_simple *buf)
{
	const struct bt_has_cp_read_preset_req *req;
	struct bt_has_read_preset_work_ctx *ctx;

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_HAS_ERR_INVALID_PARAM_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	if (!req->start_index || req->start_index > preset_last->index || req->num_presets == 0) {
		return BT_GATT_ERR(BT_ATT_ERR_OUT_OF_RANGE);
	}

	ctx = &read_preset_ctx[bt_conn_index(conn)];
	if (k_work_is_pending(&ctx->work)) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	ctx->conn = bt_conn_ref(conn);
	ctx->start_index = req->start_index - 1; // -1 to include the index in search
	ctx->num_presets = req->num_presets;

	k_work_submit(&ctx->work);

	return 0;
}

static int preset_name_set(uint8_t index, const char *name, uint8_t len)
{
#if defined(CONFIG_BT_HAS_PRESET_NAME_DYNAMIC)
	struct bt_has_preset *preset;
	char *dst;

	if (len < BT_HAS_PRESET_NAME_MIN || len > BT_HAS_PRESET_NAME_MAX) {
		return BT_GATT_ERR(BT_HAS_ERR_INVALID_PARAM_LEN);
	}

	preset = preset_get(index);
	if (!preset) {
		return BT_GATT_ERR(BT_ATT_ERR_OUT_OF_RANGE);
	}

	if (!(preset->properties & BT_HAS_PROP_WRITABLE)) {
		return BT_GATT_ERR(BT_HAS_ERR_WRITE_NAME_NOT_ALLOWED);
	}

	dst = (char *)preset->name;
	strncpy(dst, name, len);
	dst[len] = '\0';

	BT_DBG("index %d name %s", index, preset->name);

	// TODO: emit preset changed

	return 0;
}

static int cp_write_preset_name(struct bt_conn *conn, struct net_buf_simple *buf)
{
	const struct bt_has_cp_write_preset_name_req *req;

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_HAS_ERR_INVALID_PARAM_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	return preset_name_set(req->index, req->name, buf->len);
}
#else
	return -EOPNOTSUPP;
}
#endif /* CONFIG_BT_HAS_PRESET_NAME_DYNAMIC */

static int cp_set_active_preset(struct bt_conn *conn, struct net_buf_simple *buf, bool sync)
{
	const struct bt_has_cp_set_active_preset_req *req;
	struct bt_has_preset *preset;
	int err;

	if (!preset_ops) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	if (buf->len < sizeof(*req)) {
		return BT_GATT_ERR(BT_HAS_ERR_INVALID_PARAM_LEN);
	}

	req = net_buf_simple_pull_mem(buf, sizeof(*req));

	BT_DBG("conn %p, index 0x%02x sync %d", conn, req->index, sync);

	preset = preset_get(req->index);
	if (!preset) {
		return BT_GATT_ERR(BT_ATT_ERR_OUT_OF_RANGE);
	}

	if (!(preset->properties & BT_HAS_PROP_AVAILABLE)) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	err = preset_ops->active_set(&has, preset->index, sync);
	if (err != 0) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	return 0;
}

static int cp_set_next_preset(struct bt_conn *conn, struct net_buf_simple *buf, bool sync)
{
	const struct bt_has_preset *preset;
	int err;

	BT_DBG("conn %p sync %d", conn, sync);

	if (preset_ops == NULL) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	preset = preset_get_available(preset_get_next);
	if (preset == NULL) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	err = preset_ops->active_set(&has, preset->index, sync);
	if (err != 0) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	return 0;
}

static int cp_set_prev_preset(struct bt_conn *conn, struct net_buf_simple *buf, bool sync)
{
	const struct bt_has_preset *preset;
	int err;

	BT_DBG("conn %p sync %d", conn, sync);

	if (preset_ops == NULL) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	preset = preset_get_available(preset_get_prev);
	if (preset == NULL) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	err = preset_ops->active_set(&has, preset->index, sync);
	if (err != 0) {
		return BT_GATT_ERR(BT_HAS_ERR_OPERATION_NOT_POSSIBLE);
	}

	return 0;
}

static ssize_t cp_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *data,
			uint16_t len, uint16_t offset, uint8_t flags)
{
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
	case BT_HAS_OP_READ_PRESET_REQ:
		ret = cp_read_preset_req(conn, &buf);
		break;

#if defined(CONFIG_BT_HAS_PRESET_NAME_DYNAMIC)
	case BT_HAS_OP_WRITE_PRESET_NAME:
		ret = cp_write_preset_name(conn, &buf);
		break;
#endif /* CONFIG_BT_HAS_PRESET_NAME_DYNAMIC */

	case BT_HAS_OP_SET_ACTIVE_PRESET:
		ret = cp_set_active_preset(conn, &buf, false);
		break;

	case BT_HAS_OP_SET_NEXT_PRESET:
		ret = cp_set_next_preset(conn, &buf, false);
		break;

	case BT_HAS_OP_SET_PREV_PRESET:
		ret = cp_set_prev_preset(conn, &buf, false);
		break;

#if defined(CONFIG_BT_HAS_HA_PRESET_SYNC_SUPPORT)
	case BT_HAS_OP_SET_ACTIVE_PRESET_SYNC:
		ret = cp_set_active_preset(conn, &buf, true);
		break;

	case BT_HAS_OP_SET_NEXT_PRESET_SYNC:
		ret = cp_set_next_preset(conn, &buf, true);
		break;

	case BT_HAS_OP_SET_PREV_PRESET_SYNC:
		ret = cp_set_prev_preset(conn, &buf, true);
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

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct bt_has_read_preset_work_ctx *ctx = &read_preset_ctx[bt_conn_index(conn)];

	if (k_work_is_pending(&ctx->work)) {
		k_work_cancel(&ctx->work);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.disconnected = disconnected,
};

static int has_init(const struct device *dev)
{
	bool writable_presets_support = false;
	bt_has_hearing_aid_type_t type;

	ARG_UNUSED(dev);

	STRUCT_SECTION_FOREACH(bt_has_preset, iter)
	{
		if (!preset_first || preset_first->index > iter->index) {
			preset_first = iter;
		}

		if (!preset_last || preset_last->index < iter->index) {
			preset_last = iter;
		}

		if (IS_ENABLED(CONFIG_BT_HAS_PRESET_NAME_DYNAMIC)) {
			/* If the server exposes at least one preset record with the Writable flag set,
			 * then the server shall set the Writable Presets Support flag. */
			writable_presets_support |= iter->properties & BT_HAS_PROP_WRITABLE;
		}
	}

	if (IS_ENABLED(CONFIG_BT_HAS_HA_TYPE_MONAURAL)) {
		type = BT_HAS_MONAURAL_HEARING_AID;
	} else if (IS_ENABLED(CONFIG_BT_HAS_HA_TYPE_BANDED)) {
		type = BT_HAS_BANDED_HEARING_AID;
	} else {
		type = BT_HAS_BINAURAL_HEARING_AID;
	}

	/* Initialize the supported features characteristic value */
	has.features = BT_HAS_FEAT_HEARING_AID_TYPE & type;
	if (IS_ENABLED(CONFIG_BT_HAS_PRESET_SYNC_SUPPORT)) {
		has.features |= BT_HAS_FEAT_BIT_PRESET_SYNC;
	}
	if (!IS_ENABLED(CONFIG_BT_HAS_IDENTICAL_PRESET_RECORDS)) {
		has.features |= BT_HAS_FEAT_BIT_INDEPENDENT_PRESETS;
	}
	if (writable_presets_support) {
		has.features |= BT_HAS_FEAT_BIT_WRITABLE_PRESETS;
	}

	k_work_init(&active_preset_work, active_preset_work_process);

	for (int i = 0; i < ARRAY_SIZE(read_preset_ctx); i++) {
		struct bt_has_read_preset_work_ctx *ctx = &read_preset_ctx[i];

		k_work_init(&ctx->work, read_preset_work_process);
	}

	return 0;
}

SYS_INIT(has_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static inline bool bt_has_is_local(struct bt_has *_has)
{
	return _has == &has;
}
#endif /* CONFIG_BT_HAS */

int bt_has_register(struct bt_has_preset_ops *ops, struct bt_has **out)
{
#if defined(CONFIG_BT_HAS)
	CHECKIF(ops == NULL || ops->active_set == NULL)
	{
		return -EINVAL;
	}

	*out = &has;

	/* Check if already registered */
	if (preset_ops) {
		return -EALREADY;
	}

	preset_ops = ops;

	return 0;
#else
	return -EOPNOTSUPP;
#endif /* CONFIG_BT_HAS */
}

int bt_has_preset_active_get(struct bt_has *has)
{
	CHECKIF(has == NULL)
	{
		return -EINVAL;
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

#if defined(CONFIG_BT_HAS)
	if (bt_has_is_local(has)) {
		return preset_active_set(index);
	}
#endif /* CONFIG_BT_HAS */

	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT)) {
		return bt_has_client_preset_active_set(has, index);
	}

	return -EOPNOTSUPP;
}

int bt_has_preset_active_clear(struct bt_has *has)
{
	return bt_has_preset_active_set(has, BT_HAS_PRESET_INDEX_NONE);
}

int bt_has_preset_active_set_next(struct bt_has *has)
{
	CHECKIF(has == NULL)
	{
		return -EINVAL;
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

	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT)) {
		return bt_has_client_preset_active_set_prev(has);
	}

	return -EOPNOTSUPP;
}

int bt_has_preset_read(struct bt_has *has, struct bt_has_preset_read_params *params)
{
	CHECKIF(has == NULL)
	{
		return -EINVAL;
	}

	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT)) {
		return bt_has_client_preset_read(has, params);
	}

	return -EOPNOTSUPP;
}

int bt_has_preset_visibility_set(struct bt_has *has, uint8_t index, bool visible)
{
	CHECKIF(has == NULL)
	{
		return -EINVAL;
	}

#if defined(CONFIG_BT_HAS)
	if (bt_has_is_local(has)) {
		return preset_visibility_set(index, visible);
	}
#endif /* CONFIG_BT_HAS */

	return -EOPNOTSUPP;
}

int bt_has_preset_name_set(struct bt_has *has, uint8_t index, const char *name)
{
	CHECKIF(has == NULL || name == NULL)
	{
		return -EINVAL;
	}

#if defined(CONFIG_BT_HAS)
	if (bt_has_is_local(has)) {
		return preset_name_set(index, name, strlen(name));
	}
#endif /* CONFIG_BT_HAS */

	if (IS_ENABLED(CONFIG_BT_HAS_CLIENT)) {
		return bt_has_client_preset_name_set(has, index, name, strlen(name));
	}

	return -EOPNOTSUPP;
}
