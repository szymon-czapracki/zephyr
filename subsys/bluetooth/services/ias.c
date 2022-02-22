/** @file
 *  @brief IAS Service sample
 */

/*
 * Copyright (c) 2022 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bluetooth/att.h"
#include "bluetooth/services/ias.h"
#include <stdint.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr.h>
#include <init.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

#define LOG_LEVEL CONFIG_BT_IAS_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(ias);

#define ALERT_MAX_LEN 1

static void start_alert(struct bt_conn *conn)
{
	STRUCT_SECTION_FOREACH(bt_ias_cb, cb) {
		if (cb->start_alert) {
			cb->start_alert();
		}
	}
}

static void stop_alert(struct bt_conn *conn)
{
	STRUCT_SECTION_FOREACH(bt_ias_cb, cb) {
		if (cb->stop_alert) {
			cb->stop_alert();
		}
	}
}

static void start_alert_high(struct bt_conn *conn)
{
	STRUCT_SECTION_FOREACH(bt_ias_cb, cb) {
		if (cb->start_alert_high) {
			cb->start_alert_high();
		}
	}
}

static ssize_t bt_ias_write_alert_lvl(struct bt_conn *conn,
										const struct bt_gatt_attr *attr,
										const void *buf, uint16_t len,
										uint16_t offset, uint8_t flags)
{
	uint8_t *alert_state = attr->user_data;

	if (!(flags & BT_GATT_WRITE_FLAG_CMD)) {
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
	}

	if (offset + len > ALERT_MAX_LEN) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (offset > 0) {
		return BT_GATT_ERR(BT_ATT_ERR_ATTRIBUTE_NOT_LONG);
	}

	memcpy(alert_state + offset, buf, len);
	alert_state[offset + len] = 0;

	switch(*alert_state) {
		case BT_IAS_ALERT_LVL_NO_ALERT:
			stop_alert(conn);
			break;
		case BT_IAS_ALERT_LVL_MILD_ALERT:
			start_alert(conn);
			break;
		case BT_IAS_ALERT_LVL_HIGH_ALERT:
			start_alert_high(conn);
			break;
		default:
			return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	return len;
}

//static void stop_alert(uint8_t err)
//{
//	if (err) {
//		printk("Unable to set immediate alert");
//	} else {
//		printk("No alert");
//	}
//}
//
//static void start_alert(uint8_t err)
//{
//	if (err) {
//		printk("Unable to set immediate alert");
//	} else {
//		printk("Mild alert");
//	}
//}
//
//static void start_alert_high(uint8_t err)
//{
//	if (err) {
//		printk("Unable to set immediate alert");
//	} else {
//		printk("High alert");
//	}
//}
//
//static struct bt_ias_cb ias_callbacks = {
//	.stop_alert = stop_alert,
//	.start_alert = start_alert,
//	.start_alert_high = start_alert_high,
//};

/* Immediate Alert Service Declaration */
BT_GATT_SERVICE_DEFINE(ias_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_IAS),
		       BT_GATT_CHARACTERISTIC(BT_UUID_ALERT_LEVEL, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
					      BT_GATT_PERM_NONE, NULL, bt_ias_write_alert_lvl, NULL));

static int ias_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

SYS_INIT(ias_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
