/** @file
 *  @brief IAS Service sample
 */

/*
 * Copyright (c) 2022 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "net/buf.h"
#include "bluetooth/services/ias.h"
#include <init.h>
#include <zephyr.h>
#include <logging/log.h>
#include <bluetooth/gatt.h>

#define LOG_LEVEL CONFIG_BT_IAS_LOG_LEVEL
LOG_MODULE_REGISTER(ias);

#define ALERT_MAX_LEN 1
#define BT_IAS_ALERT_LVL_NO_ALERT		0
#define BT_IAS_ALERT_LVL_MILD_ALERT		1
#define BT_IAS_ALERT_LVL_HIGH_ALERT		2

#define GATT_PERM_WRITE_MASK    (BT_GATT_PERM_WRITE | \
				 BT_GATT_PERM_WRITE_ENCRYPT | \
				 BT_GATT_PERM_WRITE_AUTHEN)

#ifndef CONFIG_BT_IAS_DEFAULT_PERM_RW_AUTHEN
#define CONFIG_BT_IAS_DEFAULT_PERM_RW_AUTHEN 0
#endif
#ifndef CONFIG_BT_IAS_DEFAULT_PERM_RW_ENCRYPT
#define CONFIG_BT_IAS_DEFAULT_PERM_RW_ENCRYPT 0
#endif

#define IAS_GATT_PERM_DEFAULT (						\
	CONFIG_BT_IAS_DEFAULT_PERM_RW_AUTHEN ?				\
	(BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN) :	\
	CONFIG_BT_IAS_DEFAULT_PERM_RW_ENCRYPT ?				\
	(BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT) :	\
	(BT_GATT_PERM_READ | BT_GATT_PERM_WRITE))			\

static void alert_level(const uint8_t alert_lvl)
{
	if (alert_lvl == BT_IAS_ALERT_LVL_NO_ALERT) {
		STRUCT_SECTION_FOREACH(bt_ias_cb, cb)
		{
			if (cb->stop_alert) {
				cb->stop_alert();
			}
		}
	} else if (alert_lvl == BT_IAS_ALERT_LVL_MILD_ALERT) {
		STRUCT_SECTION_FOREACH(bt_ias_cb, cb)
		{
			if (cb->start_alert) {
				cb->start_alert();
			}
		}
	} else if (alert_lvl == BT_IAS_ALERT_LVL_HIGH_ALERT) {
		STRUCT_SECTION_FOREACH(bt_ias_cb, cb)
		{
			if (cb->start_alert_high) {
				cb->start_alert_high();
			}
		}
	}
}

static ssize_t bt_ias_write_alert_lvl(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				      const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	struct net_buf_simple *data = NET_BUF_SIMPLE(ALERT_MAX_LEN);
	net_buf_simple_init(data, 0);
	net_buf_simple_add_u8(data, *(uint8_t*)buf);

	if (offset > 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len > ALERT_MAX_LEN) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	alert_level(net_buf_simple_pull_u8(data));
	return len;
}

/* Immediate Alert Service Declaration */
BT_GATT_SERVICE_DEFINE(ias_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_IAS),
		       BT_GATT_CHARACTERISTIC(BT_UUID_ALERT_LEVEL, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
					      IAS_GATT_PERM_DEFAULT & GATT_PERM_WRITE_MASK, NULL,
						  bt_ias_write_alert_lvl, NULL));

static int ias_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

SYS_INIT(ias_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
