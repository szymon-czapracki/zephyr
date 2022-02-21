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
#include <bluetooth/audio/capabilities.h>
#include <sys/util.h>
#include <shell/shell.h>
#include <stdlib.h>
#include <stdio.h>

#include "bt.h"

static struct bt_has *has;

static const uint8_t g_universal_idx = 1;
static const uint8_t g_outdoor_idx = 5;
static const uint8_t g_noisy_idx = 8;
static const uint8_t g_office_idx = 22;

#define MAX_PAC 1

#define AVAILABLE_SINK_CONTEXT  (BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED | \
				 BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | \
				 BT_AUDIO_CONTEXT_TYPE_MEDIA | \
				 BT_AUDIO_CONTEXT_TYPE_GAME | \
				 BT_AUDIO_CONTEXT_TYPE_INSTRUCTIONAL)

#define AVAILABLE_SOURCE_CONTEXT (BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED | \
				  BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | \
				  BT_AUDIO_CONTEXT_TYPE_MEDIA | \
				  BT_AUDIO_CONTEXT_TYPE_GAME)

static struct bt_audio_stream streams[MAX_PAC];

static uint8_t unicast_server_addata[] = {
	BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL), /* ASCS UUID */
	BT_AUDIO_UNICAST_ANNOUNCEMENT_TARGETED, /* Target Announcement */
	(((AVAILABLE_SINK_CONTEXT) >>  0) & 0xFF),
	(((AVAILABLE_SINK_CONTEXT) >>  8) & 0xFF),
	(((AVAILABLE_SOURCE_CONTEXT) >>  0) & 0xFF),
	(((AVAILABLE_SOURCE_CONTEXT) >>  8) & 0xFF),
	0x00, /* Metadata length */
};

/* TODO: Expand with BAP data */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL)),
	BT_DATA(BT_DATA_SVC_DATA16, unicast_server_addata, ARRAY_SIZE(unicast_server_addata)),
};

static struct bt_audio_lc3_preset preset_16_2_1 = BT_AUDIO_LC3_UNICAST_PRESET_16_2_1;

void print_hex(const uint8_t *ptr, size_t len)
{
	while (len-- != 0) {
		shell_print(ctx_shell, "%02x", *ptr++);
	}
}

static void print_codec(const struct bt_codec *codec)
{
	shell_print(ctx_shell, "codec 0x%02x cid 0x%04x vid 0x%04x count %u\n",
	       codec->id, codec->cid, codec->vid, codec->data_count);

	for (size_t i = 0; i < codec->data_count; i++) {
		shell_print(ctx_shell, "data #%zu: type 0x%02x len %u\n",
		       i, codec->data[i].data.type,
		       codec->data[i].data.data_len);
		print_hex(codec->data[i].data.data,
			  codec->data[i].data.data_len -
				sizeof(codec->data[i].data.type));
		shell_print(ctx_shell, "\n");
	}

	for (size_t i = 0; i < codec->meta_count; i++) {
		shell_print(ctx_shell, "meta #%zu: type 0x%02x len %u\n",
		       i, codec->meta[i].data.type,
		       codec->meta[i].data.data_len);
		print_hex(codec->meta[i].data.data,
			  codec->meta[i].data.data_len -
				sizeof(codec->meta[i].data.type));
		shell_print(ctx_shell, "\n");
	}
}

static void print_qos(struct bt_codec_qos *qos)
{
	shell_print(ctx_shell, "QoS: dir 0x%02x interval %u framing 0x%02x phy 0x%02x sdu %u "
	       "rtn %u latency %u pd %u\n",
	       qos->dir, qos->interval, qos->framing, qos->phy, qos->sdu,
	       qos->rtn, qos->latency, qos->pd);
}

static struct bt_audio_stream *lc3_config(struct bt_conn *conn,
					  struct bt_audio_ep *ep,
					  struct bt_audio_capability *cap,
					  struct bt_codec *codec)
{
	shell_print(ctx_shell, "ASE Codec Config: conn %p ep %p cap %p\n",
				conn, ep, cap);

	print_codec(codec);

	for (size_t i = 0; i < ARRAY_SIZE(streams); i++) {
		struct bt_audio_stream *stream = &streams[i];

		if (!stream->conn) {
			shell_print(ctx_shell, "ASE Codec Config stream %p\n", stream);
			return stream;
		}
	}

	shell_error(ctx_shell, "No streams available\n");

	return NULL;
}

static int lc3_reconfig(struct bt_audio_stream *stream,
			struct bt_audio_capability *cap,
			struct bt_codec *codec)
{
	shell_print(ctx_shell, "ASE Codec Reconfig: stream %p cap %p\n",
				stream, cap);

	print_codec(codec);

	/* We only support one QoS at the moment, reject changes */
	return -ENOEXEC;
}

static int lc3_qos(struct bt_audio_stream *stream, struct bt_codec_qos *qos)
{
	shell_print(ctx_shell, "QoS: stream %p qos %p\n", stream, qos);

	print_qos(qos);

	return 0;
}

static int lc3_enable(struct bt_audio_stream *stream, uint8_t meta_count,
		      struct bt_codec_data *meta)
{
	shell_print(ctx_shell, "Enable: stream %p meta_count %u\n",
				stream, meta_count);

	return 0;
}

static int lc3_start(struct bt_audio_stream *stream)
{
	shell_print(ctx_shell, "Start: stream %p\n", stream);

	return 0;
}

static int lc3_metadata(struct bt_audio_stream *stream, uint8_t meta_count,
			struct bt_codec_data *meta)
{
	shell_print(ctx_shell, "Metadata: stream %p meta_count %u\n",
				stream, meta_count);

	return 0;
}

static int lc3_disable(struct bt_audio_stream *stream)
{
	shell_print(ctx_shell, "Disable: stream %p\n", stream);

	return 0;
}

static int lc3_stop(struct bt_audio_stream *stream)
{
	shell_print(ctx_shell, "Stop: stream %p\n", stream);

	return 0;
}

static int lc3_release(struct bt_audio_stream *stream)
{
	shell_print(ctx_shell, "Release: stream %p\n", stream);

	return 0;
}

static struct bt_audio_capability_ops lc3_ops = {
	.config = lc3_config,
	.reconfig = lc3_reconfig,
	.qos = lc3_qos,
	.enable = lc3_enable,
	.start = lc3_start,
	.metadata = lc3_metadata,
	.disable = lc3_disable,
	.stop = lc3_stop,
	.release = lc3_release,
};

static struct bt_audio_capability caps[] = {
	{
		.type = BT_AUDIO_SINK,
		.pref = BT_AUDIO_CAPABILITY_PREF(
				BT_AUDIO_CAPABILITY_UNFRAMED_SUPPORTED,
				BT_GAP_LE_PHY_2M, 0x02, 10, 40000, 40000,
				40000, 40000),
		.codec = &preset_16_2_1.codec,
		.ops = &lc3_ops,
	}
};

static void stream_connected(struct bt_audio_stream *stream)
{
	shell_print(ctx_shell, "Audio Stream %p connected\n", stream);
}

static void stream_disconnected(struct bt_audio_stream *stream, uint8_t reason)
{
	shell_print(ctx_shell, "Audio Stream %p disconnected (reason 0x%02x)\n",
				stream, reason);
}

static void stream_recv(struct bt_audio_stream *stream, struct net_buf *buf)
{
	shell_print(ctx_shell, "Incoming audio on stream %p len %u\n", stream,
				buf->len);
}

static struct bt_audio_stream_ops stream_ops = {
	.connected = stream_connected,
	.disconnected = stream_disconnected,
	.recv = stream_recv
};

static int set_active_preset_cb(struct bt_has *has, uint8_t index, bool sync)
{
	int err;

	shell_print(ctx_shell, "Set active preset index 0x%02x sync %d", index, sync);

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
		shell_error(sh, "%s unknown parameter: %s", argv[0], argv[1]);
	} else {
		shell_error(sh, "%s Missing subcomand", argv[0]);
	}

	return -ENOEXEC;
}

static int cmd_has_init(const struct shell *sh, size_t argc, char **argv)
{
	int result;
	struct bt_le_ext_adv *adv;
	struct bt_has_register_param register_param = {
		.preset_param = {
			{.id = g_universal_idx,
			.properties = BT_HAS_PROP_WRITABLE | BT_HAS_PROP_AVAILABLE,
			.name = "Universal"},
			{.id = g_outdoor_idx,
			.properties = BT_HAS_PROP_WRITABLE | BT_HAS_PROP_AVAILABLE,
			.name = "Outdoor"},
			{.id = g_noisy_idx,
			.properties = BT_HAS_PROP_WRITABLE | BT_HAS_PROP_AVAILABLE,
			.name = "Noisy environment"},
			{.id = g_office_idx,
			.properties = BT_HAS_PROP_WRITABLE | BT_HAS_PROP_AVAILABLE,
			.name = "Office"},
		},
		.ops = &preset_ops,
	};

	if (!ctx_shell) {
		ctx_shell = sh;
	}

	result = bt_has_register(&register_param, &has);
	if (result < 0) {
		shell_error(ctx_shell, "HAS preset ops register failed (err %d)", result);
	} else {
		shell_print(ctx_shell, "HAS server initialized");
	}

	shell_print(sh, "Bluetooth initialized\n");

	for (size_t i = 0; i < ARRAY_SIZE(caps); i++) {
		bt_audio_capability_register(&caps[i]);
	}

	for (size_t i = 0; i < ARRAY_SIZE(streams); i++) {
		bt_audio_stream_cb_register(&streams[i], &stream_ops);
	}

	/* Create a non-connectable non-scannable advertising set */
	result = bt_le_ext_adv_create(BT_LE_EXT_ADV_CONN_NAME, NULL, &adv);
	if (result) {
		shell_error(sh, "Failed to create advertising set (err %d)\n", result);
		return result;
	}

	result = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
	if (result) {
		shell_error(sh, "Failed to set advertising data (err %d)\n", result);
		return result;
	}

	result = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (result) {
		shell_error(sh, "Failed to start advertising set (err %d)\n", result);
		return result;
	}

	shell_print(sh, "Advertising successfully started\n");

	return result;
}

static int cmd_has_active_preset_get(const struct shell *sh, size_t argc, char **argv)
{
	int result = bt_has_preset_active_get(has);

	if (result < 0) {
		shell_error(sh, "Fail: %d", result);
	}

	return result;
}

static int cmd_has_active_preset_set(const struct shell *sh, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);

	result = bt_has_preset_active_set(has, index);
	if (result < 0) {
		shell_print(sh, "Fail: %d", result);
	}

	return result;
}

static int cmd_has_active_preset_set_next(const struct shell *sh, size_t argc, char **argv)
{
	int result;

	result = bt_has_preset_active_set_next(has);
	if (result < 0) {
		shell_error(sh, "Fail: %d", result);
	}

	return result;
}

static int cmd_has_active_preset_set_prev(const struct shell *sh, size_t argc, char **argv)
{
	int result;

	result = bt_has_preset_active_set_prev(has);
	if (result < 0) {
		shell_error(sh, "Fail: %d", result);
	}

	return result;
}

static int cmd_has_change_preset_avail(const struct shell *sh, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);
	char *available = argv[2];

	if (!strcmp(available, "on")) {
		result = bt_has_preset_availability_set(has, index, true);
	} else if (!strcmp(available, "off")) {
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

static int cmd_has_change_preset_vis(const struct shell *sh, size_t argc, char **argv)
{
	int result;
	int index = strtol(argv[1], NULL, 0);
	char *available = argv[2];

	if (!strcmp(available, "on")) {
		result = bt_has_preset_visibility_set(has, index, true);
	} else if (!strcmp(available, "off")) {
		result = bt_has_preset_visibility_set(has, index, false);
	} else {
		shell_error(sh, "Invalid argument");
		return -EINVAL;
	}

	if (result < 0) {
		shell_error(sh, "Failed to set preset availability (err %d)", result);
	}

	return result;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	has_cmds,
	SHELL_CMD_ARG(init, NULL, "Initialize the service and register callbacks", cmd_has_init, 1,
		      0),
	SHELL_CMD_ARG(get_active_preset, NULL, "Get active HAS preset", cmd_has_active_preset_get,
		      1, 0),
	SHELL_CMD_ARG(set_active_preset, NULL, "Set active HAS preset", cmd_has_active_preset_set,
		      2, 0),
	SHELL_CMD_ARG(set_active_preset_next, NULL, "Set next active HAS preset",
		      cmd_has_active_preset_set_next, 1, 0),
	SHELL_CMD_ARG(set_previous_preset_prev, NULL, "Set previous active HAS preset",
		      cmd_has_active_preset_set_prev, 1, 0),
	SHELL_CMD_ARG(change_preset_available, NULL, "Change HAS preset availability",
		      cmd_has_change_preset_avail, 3, 0),
	SHELL_CMD_ARG(change_preset_visible, NULL, "Change HAS preset visiblity",
		      cmd_has_change_preset_vis, 3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_ARG_REGISTER(has, &has_cmds, "Bluetooth HAS shell commands", cmd_has, 1, 1);
