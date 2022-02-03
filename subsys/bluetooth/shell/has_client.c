/** @file
 *  @brief Bluetooth Hearing Access Service (HAS) client shell.
 *
 * Copyright (c) 2022 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bluetooth/gatt.h"
#include <stdint.h>
#include <string.h>
#include <zephyr/types.h>
#include <bluetooth/conn.h>
#include <bluetooth/audio/has.h>
#include <bluetooth/bluetooth.h>
#include <shell/shell.h>
#include <stdlib.h>
#include <stdio.h>

#include "bt.h"

static struct bt_has *g_has;
static struct bt_has_preset_read_params preset_read_params;
static volatile bool g_preset_read;

static void has_discover_cb(struct bt_conn *conn, struct bt_has *has,
							bt_has_hearing_aid_type_t type)
{
	if (has == NULL) {
		shell_error(ctx_shell, "Failed to discover HAS");
		return;
	}

	g_has = has;
	shell_print(ctx_shell, "HAS discovered (type %d)", type);
}

static void has_active_preset_cb(struct bt_has *has, int err, uint8_t index)
{
	if (err != 0) {
		shell_error(ctx_shell,
					"HAS preset get failed (%d) for inst %p",
					err, has);
	} else {
		shell_print(ctx_shell, "Preset (%d) set successfull", index);
	}
}

static uint8_t preset_read_cb(struct bt_has *has, int err,
								struct bt_has_preset_read_params *params,
								struct bt_has_preset *preset)
{
	if (preset == NULL) {
		g_preset_read = true;
		return BT_HAS_PRESET_READ_STOP;
	}

	shell_print(ctx_shell, "Index: %d Properties: 0x%02x Name: %s",
				preset->index, preset->properties, preset->name);

	return BT_HAS_PRESET_READ_CONTINUE;
}

static void has_preset_name_cb(struct bt_has *has, int err, uint8_t index,
									uint8_t properties, const char *name)
{
	if (err != 0) {
		shell_error(ctx_shell,
					"HAS preset change failed %d for inst %p",
					err, has);
	} else {
		shell_print(ctx_shell, "Preset changed Index: %d \
					Properties: 0x%02x Name: %s",
					index, properties, name);
	}
}

static void mtu_cb(struct bt_conn *conn, uint8_t err,
					struct bt_gatt_exchange_params *params)
{
	int result;

	if (err > 0) {
		shell_error(ctx_shell, "Failed to exchange MTU (err %u)\n", err);
		return;
	}

	result = bt_has_discover(default_conn);
	if (result < 0) {
		shell_error(ctx_shell, "Fail (err %d)", result);
	}
}

static struct bt_has_cb has_cbs = {
	.discover = has_discover_cb,
	.active_preset = has_active_preset_cb,
	.preset = has_preset_name_cb,
};

static int cmd_has_discover(const struct shell *sh, size_t argc, char **argv)
{
	int result;
	static struct bt_gatt_exchange_params mtu_params = {
		.func = mtu_cb,
	};

	if (!ctx_shell) {
		ctx_shell = sh;
	}

	if (default_conn == NULL) {
		shell_error(sh, "Not connected");
		return -ENOEXEC;
	}

	result = bt_gatt_exchange_mtu(default_conn, &mtu_params);
	if (result < 0) {
		shell_error(sh, "Failed to exchange mtu (err %d)", result);
	}

	return result;
}

static int cmd_has_client_init(const struct shell *sh, size_t argc, char **argv)
{
	int result;

	if (!ctx_shell) {
		ctx_shell = sh;
	}

	result = bt_has_client_cb_register(&has_cbs);
	if (result < 0) {
		shell_error(sh, "CB register failed (err %d)", result);
		return result;
	} else {
		shell_print(sh, "HAS client initialized");
	}

	return result;
}

static int cmd_has_active_preset_get(const struct shell *sh, size_t argc,
										char **argv)
{
	int result;

	if (default_conn == NULL) {
		shell_error(sh, "Not connected");
		return -ENOEXEC;
	}

	result = bt_has_preset_active_get(g_has);
	if (result < 0) {
		shell_error(sh, "Fail: %d", result);
	}

	return result;
}

static int cmd_has_active_preset_set(const struct shell *sh, size_t argc,
										char **argv)
{
	int index = strtol(argv[1], NULL, 0);
	int result;

	if (default_conn == NULL) {
		shell_error(sh, "Not connected");
		return -ENOEXEC;
	}

	result = bt_has_preset_active_set(g_has, index);
	if (result < 0) {
		shell_error(sh, "Fail: %d", result);
	}

	return result;
}

static int cmd_has_active_preset_set_next(const struct shell *sh, size_t argc,
											char **argv)
{
	int result;

	if (default_conn == NULL) {
		shell_error(sh, "Not connected");
		return -ENOEXEC;
	}

	result = bt_has_preset_active_set_next(g_has);
	if (result < 0) {
		shell_error(sh, "Fail: %d", result);
	}

	return result;
}

static int cmd_has_active_preset_set_prev(const struct shell *sh, size_t argc,
											char **argv)
{
	int result;

	if (default_conn == NULL) {
		shell_error(sh, "Not connected");
		return -ENOEXEC;
	}

	result = bt_has_preset_active_set_prev(g_has);
	if (result < 0) {
		shell_error(sh, "Fail: %d", result);
	}

	return result;
}

static int cmd_has_read_presets(const struct shell *sh, size_t argc,
								char **argv)
{
	int result;

	preset_read_params.func = preset_read_cb;
	preset_read_params.by_index = false;
	preset_read_params.by_count.start_index = 0x01;
	preset_read_params.by_count.preset_count = 0xff;

	result = bt_has_preset_read(g_has, &preset_read_params);
	if (result < 0) {
		shell_print(sh, "Failed to read all presets (err %d)", result);
	}

	return result;
}

static int cmd_has_change_preset_name(const struct shell *sh, size_t argc,
										char **argv)
{
	int index = strtol(argv[1], NULL, 0);
	char *name = argv[2];
	int result;

	if (default_conn == NULL) {
		shell_error(sh, "Not connected");
		return -ENOEXEC;
	}

	result = bt_has_preset_name_set(g_has, index, name);
	if (result < 0) {
		shell_print(sh, "Failed to set preset name (err %d)", result);
	}

	return result;
}

static int cmd_has_client(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1) {
		shell_error(sh, "%s unknown parameter: %s",
			    argv[0], argv[1]);
	} else {
		shell_error(sh, "%s Missing subcommand", argv[0]);
	}

	return -ENOEXEC;
}

SHELL_STATIC_SUBCMD_SET_CREATE(has_client_cmds,
	SHELL_CMD_ARG(init, NULL,
					"Initialize HAS client",
					cmd_has_client_init, 1, 0),
	SHELL_CMD_ARG(discover, NULL,
					"Discover HAS for current connection",
					cmd_has_discover, 1, 0),
	SHELL_CMD_ARG(get_active_preset, NULL,
					"Get active HAS preset",
					cmd_has_active_preset_get, 1, 0),
	SHELL_CMD_ARG(set_active_preset, NULL,
					"Set active HAS preset",
					cmd_has_active_preset_set, 2, 0),
	SHELL_CMD_ARG(set_active_preset_next, NULL,
					"Set next active HAS preset",
					cmd_has_active_preset_set_next, 1, 0),
	SHELL_CMD_ARG(set_active_preset_prev, NULL,
					"Set previous active HAS preset",
					cmd_has_active_preset_set_prev, 1, 0),
	SHELL_CMD_ARG(read_has_presets, NULL,
					"Read HAS presets",
					cmd_has_read_presets, 1, 0),
	SHELL_CMD_ARG(change_preset_name, NULL,
					"Change HAS preset name",
					cmd_has_change_preset_name, 3, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_ARG_REGISTER(has_client, &has_client_cmds,
						"Bluetooth HAS shell commands", cmd_has_client, 1, 1);
