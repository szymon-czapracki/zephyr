/*
 * Copyright (c) 2022 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bluetooth/att.h"
#include "bluetooth/uuid.h"
#include "net/buf.h"
#include <errno.h>
#include <stdint.h>
#include <zephyr.h>
#include <sys/byteorder.h>
#include <sys/check.h>

#include <device.h>
#include <init.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>

#include <bluetooth/services/ias.h>
#include "../host/conn_internal.h"
#include "ias_internal.h"

#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_IAS_CLIENT)
#define LOG_MODULE_NAME bt_ias_client
#include "common/log.h"

#define BT_IAS_CLIENT(_ias) CONTAINER_OF(_ias, struct bt_ias_client, ias)
#define IS_HANDLE_VALID(handle) ((handle) != 0x0000)

static struct bt_ias_client_cb *ias_cb;

static struct bt_ias_client client_list[CONFIG_BT_MAX_CONN];

static struct bt_ias_client *client_by_conn(struct bt_conn *conn)
{
	return &client_list[bt_conn_index(conn)];
}

int bt_ias_alert_write(struct bt_conn *conn, struct net_buf_simple *buf)
{
	return bt_gatt_write_without_response(conn,
					client_by_conn(conn)->write.handle,
					buf->data, buf->len, false);
}

static int ias_set_write_handle(struct bt_conn *conn, uint16_t value_handle)
{
	client_by_conn(conn)->write.handle = value_handle;

	return 0;
}

static uint8_t bt_ias_discover_cb(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				struct bt_gatt_discover_params *discover)
{
	struct bt_has_client *client = client_by_conn(conn);
	const struct bt_gatt_chrc *chrc;
	int err = 0;

	BT_DBG("conn %p attr %p", conn, attr);

	if (!attr) {
		// XXX:discovery_complete(client, false);
		return BT_GATT_ITER_STOP;
	}

	chrc = attr->user_data;

	if (chrc->properties & BT_GATT_CHRC_WRITE_WITHOUT_RESP)
	{
		err = ias_set_write_handle(conn, chrc->value_handle);
	}

	if (err < 0) {
		// XXX:discovery_complete(client, false);
	}

	return BT_GATT_ITER_STOP;
}

int bt_ias_discover(struct bt_ias_client *client)
{
	(void)memcpy(&client->uuid, BT_UUID_IAS, sizeof(client->uuid));
	client->discover.func = bt_ias_discover_cb;
	client->discover.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	client->discover.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	client->discover.type = BT_GATT_DISCOVER_PRIMARY;

	return bt_gatt_discover(client->conn, &client->discover);
}

//int bt_ias_discover(struct bt_conn *conn)
//{
//	struct bt_ias_client *client;
//	int err = 0;
//
//	BT_DBG("conn %p", conn);
//
//	CHECKIF(!ias_cb || ias_cb->discover) {
//		return -EINVAL;
//	}
//
//	if (!conn) {
//		return -ENOTCONN;
//	}
//
//	client = client_by_conn(conn);
//	if (client->busy) {
//		return -EBUSY;
//	}
//
//	client->conn = conn;
//
//	return err;
//}

int bt_ias_client_cb_register(struct bt_ias_client_cb *cb)
{
	CHECKIF(ias_cb) {
		return -EALREADY;
	}

	ias_cb = cb;

	return 0;
}

int bt_ias_client_conn_get(const struct bt_ias *ias,
				struct bt_conn **conn)
{
	struct bt_ias_client *client = BT_IAS_CLIENT(ias);

	*conn = bt_conn_ref(client->conn);

	return 0;
}
