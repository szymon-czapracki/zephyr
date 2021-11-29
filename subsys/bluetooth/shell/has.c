/** @file
 *  @brief Bluetooth Hearing Access Service (HAS) shell.
 *
 * Copyright (c) 2021 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <bluetooth/conn.h>
#include <bluetooth/audio/has.h>
#include <shell/shell.h>
#include <stdlib.h>
#include <stdio.h>

#include "bt.h"

BT_HAS_PRESET_DEFINE(1, "Universal", _properties, _user_data);

static struct bt_vcs *vcs;
static struct bt_vcs_included vcs_included;

static struct bt_vcs_cb vcs_cbs = {
	.state = vcs_state_cb,
	.flags = vcs_flags_cb,
};

static struct bt_aics_cb aics_cbs = {
	.state = aics_state_cb,
	.gain_setting = aics_gain_setting_cb,
	.type = aics_input_type_cb,
	.status = aics_status_cb,
	.description = aics_description_cb
};

static struct bt_vocs_cb vocs_cbs = {
	.state = vocs_state_cb,
	.location = vocs_location_cb,
	.description = vocs_description_cb
};

static int cmd_vcs_init(const struct shell *sh, size_t argc, char **argv)
{
	int result;
	struct bt_vcs_register_param vcs_param;
	char input_desc[CONFIG_BT_VCS_AICS_INSTANCE_COUNT][16];
	char output_desc[CONFIG_BT_VCS_VOCS_INSTANCE_COUNT][16];

	if (!ctx_shell) {
		ctx_shell = sh;
	}

	memset(&vcs_param, 0, sizeof(vcs_param));

	for (int i = 0; i < ARRAY_SIZE(vcs_param.vocs_param); i++) {
		vcs_param.vocs_param[i].location_writable = true;
		vcs_param.vocs_param[i].desc_writable = true;
		snprintf(output_desc[i], sizeof(output_desc[i]),
			 "Output %d", i + 1);
		vcs_param.vocs_param[i].output_desc = output_desc[i];
		vcs_param.vocs_param[i].cb = &vocs_cbs;
	}

	for (int i = 0; i < ARRAY_SIZE(vcs_param.aics_param); i++) {
		vcs_param.aics_param[i].desc_writable = true;
		snprintf(input_desc[i], sizeof(input_desc[i]),
			 "Input %d", i + 1);
		vcs_param.aics_param[i].description = input_desc[i];
		vcs_param.aics_param[i].type = BT_AICS_INPUT_TYPE_UNSPECIFIED;
		vcs_param.aics_param[i].status = true;
		vcs_param.aics_param[i].gain_mode = BT_AICS_MODE_MANUAL;
		vcs_param.aics_param[i].units = 1;
		vcs_param.aics_param[i].min_gain = -100;
		vcs_param.aics_param[i].max_gain = 100;
		vcs_param.aics_param[i].cb = &aics_cbs;
	}

	vcs_param.step = 1;
	vcs_param.mute = BT_VCS_STATE_UNMUTED;
	vcs_param.volume = 100;

	vcs_param.cb = &vcs_cbs;

	result = bt_vcs_register(&vcs_param, &vcs);
	if (result) {
		shell_print(sh, "Fail: %d", result);
		return result;
	}

	result = bt_vcs_included_get(vcs, &vcs_included);
	if (result != 0) {
		shell_error(sh, "Failed to get included services: %d", result);
		return result;
	}

	return result;
}

static int cmd_vcs(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1) {
		shell_error(sh, "%s unknown parameter: %s",
			    argv[0], argv[1]);
	} else {
		shell_error(sh, "%s Missing subcommand", argv[0]);
	}

	return -ENOEXEC;
}

SHELL_STATIC_SUBCMD_SET_CREATE(has_cmds,
	SHELL_CMD_ARG(init, NULL,
		      "Initialize the service and register callbacks",
		      cmd_has_init, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_ARG_REGISTER(has, &has_cmds, "Bluetooth HAS shell commands",
		       cmd_has, 1, 1);
