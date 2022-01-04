/*
 * Copyright (c) 2021 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

//#ifdef CONFIG_BT_HAS

#include "bluetooth/gatt.h"
#include <bluetooth/bluetooth.h>
#include <bluetooth/audio/has.h>
#include "common.h"
#include <stdint.h>

extern enum bst_result bst_result;
static struct bt_has_preset g_has_preset;
static struct bt_has *has;

#define IS_HAS_CLIENT(has) 1

static volatile bool g_bt_init;
static volatile bool g_is_connected;
static volatile bool g_mtu_exchanged;
static volatile bool g_discovery_complete;

static volatile bool g_cb;
static volatile bool g_has_active = 1;
static struct bt_conn *g_conn;
static char g_has_preset_name[41];

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
	g_conn = conn;
	g_is_connected = true;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

//// TODO: Implement callback
//static void has_active_preset_set_cb(struct bt_has *has, int err, int index)
//{
//	if (err) {
//		FAIL("HAS active preset set cb err\n");
//		return;
//	}
//	g_cb = true;
//}
//
//// TODO: Implement callback
//static void has_active_preset_get_cb(struct bt_has *has, int err, int index)
//{
//	if (err) {
//		FAIL("HAS active preset get cb err\n");
//		return;
//	}
//	g_cb = true;
//}
//
//// TODO: Implement callback
//static void has_active_preset_set_next_cb(struct bt_has *has, int err)
//{
//	if (err) {
//		FAIL("HAS active preset set next cb err\n");
//		return;
//	}
//	g_cb = true;
//}
//
//// TODO: Implement callback
//static void has_active_preset_set_prev_cb(struct bt_has *has, int err)
//{
//	if (err) {
//		FAIL("HAS active preset set prev cb err\n");
//		return;
//	}
//	g_cb = true;
//}
//
//// TODO: Implement callback
//static void has_preset_list_get_cb(struct bt_has *has, int err)
//{
//	if (err) {
//		FAIL("HAS preset list get cb err\n");
//		return;
//	}
//	g_cb = true;
//}

static void has_active_preset_cb(struct bt_has *has, int err, uint8_t index)
{
	if (err) {
		FAIL("HAS active preset cb err\n");
		return;
	}
	g_cb = true;
}

// TODO: Implement callback
static void has_preset_name_cb(struct bt_has *has, int err,
								struct bt_has_preset *preset, bool is_active)
{
	if (err) {
		FAIL("HAS preset name cb err\n");
		return;
	}
	g_cb = true;
}

static struct bt_has_cb has_cb = {
	.active_preset = has_active_preset_cb,
	.preset = has_preset_name_cb,
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

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

static void bt_ready(int err)
{
	if (err) {
		FAIL("Bluetooth discover failed (err %d)\n", err);
		return;
	}

	g_bt_init = true;
}

static void test_has_preset_name_set(void)
{
	int err;
	char expected_preset_name[40];
	struct bt_has_register_param has_param;

	printk("HAS write preset name test\n");
	strncpy(expected_preset_name, "New preset name",
		sizeof(expected_preset_name));
	expected_preset_name[sizeof(expected_preset_name) - 1] = '\0';
	g_cb = false;
	err = bt_has_preset_name_set(has, 0, expected_preset_name);
	if (err) {
		FAIL("HAS write preset name failed\n");
		return;
	}
	WAIT_FOR(g_cb && !strncmp(expected_preset_name, g_has_preset_name,
					sizeof(expected_preset_name)));
	printk("HAS write preset name success\n");
}

static void test_has_preset_list_get(void)
{
	int err;

	printk("Read all HAS presets test\n");
	g_cb = false;
	err = bt_has_preset_list_get(has);
	if (err) {
		FAIL("HAS read all presets failed\n");
		return;
	}
	WAIT_FOR(g_cb);
	printk("HAS presets read success\n");
}

static void test_has_preset_get(void)
{
	int err;

	printk("HAS read preset by index test\n");
	g_cb = false;
	err = bt_has_preset_get(has, 0);
	if (err) {
		FAIL("HAS read preset by index failed\n");
		return;
	}
	WAIT_FOR(g_cb);
	printk("HAS preset by index read success\n");
}

static void test_has_active_preset_get(void)
{
	int err;

	printk("HAS active preset get test\n");
	g_cb = false;
	err = bt_has_preset_active_get(has);
	if (err) {
		FAIL("HAS get active preset failed\n");
	}
	WAIT_FOR(g_cb);
	printk("HAS get active preset success\n");
}

static void test_has_active_preset_set(void)
{
	int err;

	printk("HAS set active preset set test\n");
	g_cb = false;
	err = bt_has_preset_active_set(has);
	if (err) {
		FAIL("HAS set active preset failed\n");
	}
	WAIT_FOR(g_cb);
	printk("HAS set active preset success\n");
}

static void test_has_active_preset_set_next(void)
{
	int err;

	printk("HAS set next preset test\n");
	g_cb = false;
	err = bt_has_preset_active_set_next(has);
	if (err) {
		FAIL("HAS set next preset failed\n")
	}
	WAIT_FOR(g_cb);
	printk("HAS set next preset success\n");
}

static void test_has_active_preset_set_prev(void)
{
	int err;

	printk("HAS set previous preset test\n");
	g_cb = false;
	err = bt_has_preset_active_set_prev(has);
	if (err) {
		FAIL("HAS set previous preset failed\n");
	}
	WAIT_FOR(g_cb);
	printk("HAS set previous preset\n");
}

/* Main testing function TODO: Needs work */
static void test_main(void)
{
	int err;
	char preset_name[CONFIG_BT_HAS_PRESET_COUNT][41];
	static struct bt_has_register_param has_param;

	static struct bt_gatt_exchange_params mtu_params = {
		.func = mtu_cb,
	};

	struct bt_conn *cached_conn;

	err = bt_enable(bt_ready);

	if (err) {
		FAIL("Bluetooth discover failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	memset(&has_param, 0, sizeof(has_param));

	for (int i = 0; i < ARRAY_SIZE(has_param.preset_list); i++) {
		has_param.preset_list[i].index = (uint8_t)i;
		has_param.preset_list[i].properties = BT_HAS_PROP_WRITABLE |
						      BT_HAS_PROP_AVAILABLE;
		snprintf(preset_name[i], sizeof(preset_name[i]),
			 "Preset %d", i + 1);
		has_param.preset_list[i].name = preset_name[i];
	}

	err = bt_has_register(&has_param, &has);
	if (err) {
		FAIL("HAS register failed (err %d)\n", err);
		return;
	}

	err = bt_has_cb_register(&has_cb);
	if (err) {
		FAIL("HAS CB register failed (err %d)\n", err);
		return;
	}

	WAIT_FOR(g_bt_init);

	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);
	if (err) {
		FAIL("Scanning failed to start (err %d)\n", err);
		return;
	}
	printk("Scanning succesfully started\n");

	WAIT_FOR(g_is_connected);

	err = bt_gatt_exchange_mtu(g_conn, &mtu_params);
	if (err) {
		FAIL("Failed to exchange MTU %d\n", err);
	}

	err = bt_has_discover(g_conn, &has);
	if (err) {
		FAIL("Failed to discover HAS %d\n", err);
	}

	printk("Getting HAS client conn\n");
	err = bt_has_conn_get(has, &cached_conn);
	if (err != 0) {
		FAIL("Could not get HAS client conn (err %d)\n");
		return;
	}
	if (cached_conn != g_conn) {
		FAIL("Cached conn was not the conn used to discover\n");
		return;
	}

	test_has_preset_list_get();
	test_has_preset_get();
	test_has_active_preset_set();
	test_has_active_preset_get();
	test_has_active_preset_set_next();
	test_has_active_preset_set_prev();
	test_has_preset_name_set();

	PASS("HAS client passed\n");
}

static const struct bst_test_instance test_has[] = {
	{
		.test_id = "has_client",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_main
	},
	BSTEST_END_MARKER
};

struct bst_test_list *test_has_client_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_has);
}

//#endif /* CONFIG_BT_HAS */