/** @file
 *  @brief Bluetooth Hearing Access Service (HAS) server shell.
 *
 * Copyright (c) 2022 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <bluetooth/conn.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/audio/has.h>
#include <shell/shell.h>
#include <stdlib.h>
#include <stdio.h>

#include "bt.h"

static struct bt_has *has;

BT_HAS_PRESET_DEFINE(1, "Universal", BT_HAS_PROP_WRITABLE | BT_HAS_PROP_AVAILABLE);
BT_HAS_PRESET_DEFINE(5, "Outdoor", BT_HAS_PROP_WRITABLE | BT_HAS_PROP_AVAILABLE);
BT_HAS_PRESET_DEFINE(8, "Noisy environment", BT_HAS_PROP_WRITABLE | BT_HAS_PROP_AVAILABLE);
BT_HAS_PRESET_DEFINE(22, "Office", BT_HAS_PROP_WRITABLE | BT_HAS_PROP_AVAILABLE);

static int set_active_preset_cb(struct bt_has *has, uint8_t index, bool sync)
{
	int err;

	shell_print(ctx_shell, "Set active preset index 0x%02x sync %d", index,
				sync);

	err = bt_has_preset_active_set(has, index);
	if (err < 0) {
		shell_error(ctx_shell, "Set active failed (err %d)", err);
	}

	return err;
}

struct bt_has_preset_ops preset_ops = {
	.active_set = set_active_preset_cb,
};

static int cmd_has(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1) {
		shell_error(sh, "%s unknown parameter: %s",
						argv[0], argv[1]);
	} else {
		shell_error(sh, "%s Missing subcomand", argv[0]);
	}

	return -ENOEXEC;
}

static int cmd_has_init(const struct shell *sh, size_t argc, char **argv)
{
	int result;

	if (!ctx_shell) {
		ctx_shell = sh;
	}

	result = bt_has_register(&preset_ops, &has);
	if (result < 0) {
		shell_error(ctx_shell, "HAS preset ops register failed (err %d)", result);
	} else {
		shell_print(ctx_shell, "HAS server initialized");
	}

	return result;
}

static int cmd_has_active_preset_get(const struct shell *sh, size_t argc,
										char **argv)
{
	int result = bt_has_preset_active_get(has);

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

	result = bt_has_preset_active_set(has, index);
	if (result < 0) {
		shell_print(sh, "Fail: %d", result);
	}

	return result;
}

static int cmd_has_active_preset_set_next(const struct shell *sh, size_t argc,
											char **argv)
{
	int result;

	result = bt_has_preset_active_set_next(has);
	if (result < 0) {
		shell_error(sh, "Fail: %d", result);
	}

	return result;
}

static int cmd_has_active_preset_set_prev(const struct shell *sh, size_t argc,
											char **argv)
{
	int result;

	result = bt_has_preset_active_set_prev(has);
	if (result < 0) {
		shell_error(sh, "Fail: %d", result);
	}

	return result;
}

static int cmd_has_change_preset_avail(const struct shell *sh, size_t argc,
										char **argv)
{
	int index = strtol(argv[1], NULL, 0);
	char *available = argv[2];
	int result;

	if (strcmp(available, "on") == 0) {
		result = bt_has_preset_availability_set(has, index, true);
	} else if (strcmp(available, "off") == 0) {
		result = bt_has_preset_availability_set(has, index, false);
	} else {
		shell_error(sh, "Invalid argument");
		return -EINVAL;
	}

	if (result < 0) {
		shell_error(sh, "Failed to set preset availability (err %d)", result);
	}

	return result;
}

static int cmd_has_change_preset_vis(const struct shell *sh, size_t argc,
										char **argv)
{
	int index = strtol(argv[1], NULL, 0);
	char *available = argv[2];
	int result;

	if (strcmp(available, "on") == 0) {
		result = bt_has_preset_visibility_set(has, index, true);
	} else if (strcmp(available, "off") == 0) {
		result = bt_has_preset_visibility_set(has, index, false);
	} else {
		shell_error(sh, "Invalid argument");
		return -EINVAL;
	}

	if (result < 0) {
		shell_error(sh, "Failed to set preset visibility (err %d)", result);
	}

	return result;
}

SHELL_STATIC_SUBCMD_SET_CREATE(has_cmds,
	SHELL_CMD_ARG(init, NULL,
					"Initialize the service and register callbacks",
					cmd_has_init, 1, 0),
	SHELL_CMD_ARG(get_active_preset, NULL,
					"Get active HAS preset",
					cmd_has_active_preset_get, 1, 0),
	SHELL_CMD_ARG(set_active_preset, NULL,
					"Set active HAS preset",
					cmd_has_active_preset_set, 2, 0),
	SHELL_CMD_ARG(set_active_preset_next, NULL,
					"Set next active HAS preset",
					cmd_has_active_preset_set_next, 1, 0),
	SHELL_CMD_ARG(set_previous_preset_prev, NULL,
					"Set previous active HAS preset",
					cmd_has_active_preset_set_prev, 1, 0),
	SHELL_CMD_ARG(change_preset_available, NULL,
					"Change HAS preset availability",
					cmd_has_change_preset_avail, 3, 0),
	SHELL_CMD_ARG(change_preset_visible, NULL,
					"Change HAS preset visiblity",
					cmd_has_change_preset_vis, 3, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_ARG_REGISTER(has, &has_cmds, "Bluetooth HAS shell commands",
							cmd_has, 1, 1);
