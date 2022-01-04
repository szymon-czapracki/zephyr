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

extern enum bst_result bst_result;
static struct bt_has_preset g_has_preset;
static struct bt_has *has;

static volatile bool g_bt_init;
static volatile bool g_is_connected;
static volatile bool g_mtu_exchanged;
static volatile bool g_discovery_complete;

static volatile bool g_cb;
static volatile bool g_has_active = 1;
static struct bt_conn *g_conn;

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

static void has_preset_enable_cb(int err)
{
	if (err) {
		FAIL("HAS preset enable cb err\n");
		return;
	}
	// TODO: Implement callback
	g_cb = true;
}

static void has_preset_disable_cb(struct bt_has *has, int err)
{
	if (err) {
		FAIL("HAS preset disable cb err\n");
		return;
	}
	// TODO: Implement callback
	g_cb = true;
}

static void has_preset_register_cb(struct bt_has *has, int err)
{
	if (err) {
		FAIL("HAS preset register cb err\n");
		return;
	}
	// TODO: Implement callback
	g_cb = true;
}

static void has_preset_unregister_cb(struct bt_has *has, int err)
{
	if (err) {
		FAIL("HAS preset unregister cb err\n");
		return;
	}
	// TODO: Implement callback
	g_cb = true;
}

static void has_active_preset_changed_cb(struct bt_has *has, int err)
{
	if (err) {
		FAIL("HAS activer preset cb err\n");
		return;
	}
	// TODO: Implement callback
	g_cb = true;
}

/* Do we need those callbacks?
* static void has_preset_name_changed_cb(struct bt_has *has, int err, uint8_t *name)
* {
*	if (err) {
*		FAIL("HAS preset name changed cb err\n");
*		return;
*	}
*	g_has_preset_name = name;
* 	// TODO: Implement callback
*	g_cb = true;
* }
* 
* static void has_read_all_presets_cb(struct bt_has *has, int err)
* {
*	if (err) {
*		FAIL("HAS read all presets cb err\n");
*		return;
*	}
* 	// TODO: Implement callback
*	g_cb = true;
* }
* 
* static void has_read_preset_by_index_cb(struct bt_has *has, int err)
* {
*	if (err) {
*		FAIL("HAS read preset by index cb err\n");
*		return;
*	}
* 	// TODO: Implement callback
*	g_cb = true;
* }
* 
* static void has_read_preset_cb(struct bt_has *has, int err)
* {
*	if (err) {
*		FAIL("HAS read preset cb err\n");
*		return;
*	}
* 	// TODO: Implement callback
*	g_cb = true;
* }
* 
* static void has_preset_changed_cb(struct bt_has *has, int err)
* {
*	if (err) {
*		FAIL("HAS preset changed cb err\n");
*		return;
*	}
* 	// TODO: Implement callback
*	g_cb = true;
* }
* 
* static void has_write_preset_name_cb(struct bt_has *has, int err)
* {
*	if (err) {
*		FAIL("HAS write preset name cb err\n");
*		return;
*	}
* 	// TODO: Implement callback
*	g_cb = true;
* }
* 
* static void has_set_active_preset_cb(struct bt_has *has, int err)
* {
*	if (err) {
*		FAIL("HAS set active preset cb err\n");
*		return;
*	}
* 	// TODO: Implement callback
*	g_cb = true;
* }
* 
* static void has_set_previous_preset_cb(struct bt_has *has, int err)
* {
*	if (err) {
*		FAIL("HAS set previous preset cb err\n");
*		return;
*	}
* 	// TODO: Implement callback
*	g_cb = true;
* }
* 
* static void has_set_next_preset_cb(struct bt_has *has, int err)
* {
*	if (err) {
*		FAIL("HAS set next preset cb err\n");
*		return;
*	}
* 	// TODO: Implement callback
	g_cb = true;
* }
*/

static struct bt_has_cb has_cb = {
	.preset_enable = has_preset_enable_cb,
	.preset_disable = has_preset_disable_cb,
	.preset_register = has_preset_register_cb,
	.preset_unregister = has_preset_unregister_cb,
	.preset_changed = has_active_preset_changed_cb,
	.preset_read_all = has_read_all_presets_cb,
	.preset_read_by_index = has_read_preset_by_index_cb,
	.preset_read_preset = has_read_preset_cb,
	.preset_changed = has_preset_changed_cb,
	.preset_name_write = has_write_preset_name_cb,
	.preset_set_active = has_set_active_preset_cb,
	.preset_set_previous = has_set_previous_preset_cb,
	.preset_set_next = has_set_next_preset_cb,
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

/* Main testing function TODO: Needs work */
static void test_main(void)
{
	int err;
	struct bt_has_preset expected_preset;
	struct bt_has_preset previous_preset;
	struct bt_has_preset next_preset;

	static struct bt_gatt_exchange_params mtu_params = {
		.func = mtu_cb,
	};

	struct bt_conn *cached_conn;

	err = bt_enable(bt_ready);

	if (err) {
		FAIL("Bluetooth discover failed (err %d)\n", err);
		return;
	}

	err = bt_has_cb_register(&has_cb);
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
	err = bt_has_client_conn_get(has, &cached_conn);
	if (err != 0) {
		FAIL("Could not get HAS client conn (err %d)\n");
		return;
	}
	if (cached_conn != g_conn) {
		FAIL("Cached conn was not the conn used to discover\n");
		return;
	}

	/* Rread all presets test */
	printk("Read all HAS presets test\n");
	g_cb = false;
	err = bt_has_read_all_presets(has);
	if (err) {
		FAIL("HAS read all presets failed\n");
		return;
	}
	WAIT_FOR(g_cb);
	printk("HAS presets read success\n");

	/* Read preset by index test */
	printk("HAS read preset by index test\n");
	g_cb = false;
	err = bt_has_read_preset_by_index(has);
	if (err) {
		FAIL("HAS read preset by index failed\n");
		return;
	}
	WAIT_FOR(g_cb);
	printk("HAS preset by index read success\n");

	/* Preset changed test */
	printk("HAS preset changed test")
	g_cb = false;
	err = bt_has_preset_changed(has);
	if (err) {
		FAIL("HAS preset changed failed\n");
	}
	WAIT_FOR(g_cb);
	printk("HAS preset changed success");

	/* Write preset name test */
	printk("HAS write preset name test\n");
	g_cb = false;
	err = bt_has_write_preset_name(has);
	if (err) {
		FAIL("HAS write preset name failed\n");
	}
	WAIT_FOR(g_cb);
	printk("HAS write preset name success\n");

	/* Set active preset test */
	printk("HAS set active preset test\n");
	g_cb = false;
	err = bt_has_set_active_preset(has);
	if (err) {
		FAIL("HAS set active preset failed\n");
	}
	WAIT_FOR(g_cb);
	printk("HAS set active preset success\n");

	/* Set next preset test */
	printk("HAS set next preset test\n");
	g_cb = false;
	err = bt_has_set_next_preset(has);
	if (err) {
		FAIL("HAS set next preset failed\n")
	}
	WAIT_FOR(g_cb);
	printk("HAS set next preset success\n");

	/* Set previous preset tests */
	printk("HAS set previous preset test\n");
	g_cb = false;
	err = bt_has_set_previous_preset(has);
	if (err) {
		FAIL("HAS set previous preset failed\n");
	}
	WAIT_FOR(g_cb);
	printk("HAS set previous preset\n");

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