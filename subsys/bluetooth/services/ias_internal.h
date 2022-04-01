///** @file
// * @brief Internal Header for Bluetooth Immediate Alert Service (IAS).
// *
// * Copyright (c) 2022 Codecoup
// *
// * SPDX-License-Identifier: Apache-2.0
// */
//
//#include "bluetooth/uuid.h"
//#include <bluetooth/gatt.h>
//#include <stdint.h>
//
//struct bt_ias {
//	uint8_t alert;
//};
//
//struct bt_ias_client {
//	bool busy;
//	uint8_t alert_state;
//	struct bt_ias *ias;
//	struct bt_conn *conn;
//
//	struct bt_uuid_16 uuid;
//	struct bt_gatt_write_params write;
//	struct bt_gatt_discover_params discover;
//};
//
//struct bt_ias_client_cb {
//	/** @brief Callback function for bt_ias_discover.
//	 *
//	 *  This callback is called when discovery procedure is complete.
//	 *
//	 *  @param conn Bluetooth connection object.
//	 *  @param ias Pointer to the Hearing Access Service object.
//	 */
//	void (*discover)(struct bt_conn *conn, struct bt_ias *ias);
//
//	/** @brief Callback function for bt_ias_set_alarm.
//	 *
//	 *  This callback is called when discovery procedure is complete.
//	 *
//	 *  @param conn Bluetooth connection object.
//	 *  @param ias Pointer to the Hearing Access Service object.
//	 */
//	int (*set_alarm)(struct bt_conn *conn, struct bt_ias *ias);
//};
//
//int bt_ias_alert_write(struct bt_ias_client *client, bt_gatt_write_func_t func,
//			const void *data, uint16_t len);
//int bt_ias_discover(struct bt_conn *conn);
//int bt_ias_client_cb_register(struct bt_ias_client_cb *cb);
