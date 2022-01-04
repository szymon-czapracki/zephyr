/*
 * Copyright (c) 2021 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <sys/byteorder.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/audio/has.h>


#include "../../../../subsys/bluetooth/host/audio/media_proxy.h"

#include "common.h"

static volatile bool g_is_connected;
static volatile bool g_bt_init;
static volatile bool g_mtu_exchanged;
static volatile bool g_discovery_complete;
static struct bt_has_preset g_has_preset;
static struct bt_conn *has_conn;

/* Callback when connected */
static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		FAIL("Failed to connect to %s (%u)\n", addr, err);
		return;
	}

	printk("Connected to %s\n", addr);

	has_conn = conn;
	g_is_connected = true;
}

static void bt_ready(int err)
{
	if (err) {
		FAIL("Bluetooth discover failed (err %d)\n", err);
		return;
	}

	g_bt_init = true;
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

static void has_accept_cb()
{
	// TODO: Implement callback
}

static void has_set_active_cb()
{
	// TODO: Implement callback
}

static void has_preset_name_changed_cb()
{
	// TODO: Implement callback
}

static struct bt_has_cb has_cbs = {
	.accept = has_accept_cb,
	.set_active = has_set_active_cb,
	.preset_name_changed = has_preset_name_changed_cb,
};

static void mtu_cb(struct bt_conn *conn, uint8_t err,
		   struct bt_gatt_exchange_params *params)
{
	if (err) {
		FAIL("Failed to exchange MTU (%u)\n", err);
		return;
	}

	g_mtu_exchanged = true;
}

static void test_main(void)
{
	int err;
	static struct bt_gatt_exchange_params mtu_params =  {
		.func = mtu_cb,
	};

	err = bt_enable(bt_ready);

	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	err = bt_has_cb_register(&has_cbs);
	if (err) {
		FAIL("CB register failed (err %d)\n", err);
		return;
	}

	WAIT_FOR(g_bt_init);

	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);
	if (err) {
		FAIL("Scanning failed to start (err %d)\n", err);
		return;
	}

	printk("Scanning successfully started\n");

	WAIT_FOR(g_is_connected);

	err = bt_gatt_exchange_mtu(has_conn, &mtu_params);
	if (err) {
		FAIL("Failed to exchange MTU %d", err);
	}

	WAIT_FOR(g_mtu_exchanged);

	/* TODO: There are no definitions yet */
	err = bt_has_discover(has_conn);
	if (err) {
		FAIL("FAILED to discover HAS %d", err);
	}

	WAIT_FOR(g_discovery_complete);

	/* TODO: Implement testing procedure */

	PASS("HAS passed\n");
}

static const struct bst_test_instance test_has[] = {
	{
		.test_id = "has",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_main
	},
	BSTEST_END_MARKER
};


struct bst_test_list *test_has_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_has);
}