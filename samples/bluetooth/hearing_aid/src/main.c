/*
 * Copyright (c) 2022 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/audio/audio.h>
#include <bluetooth/audio/capabilities.h>
#include <bluetooth/audio/mics.h>
#include <bluetooth/audio/has.h>
#include <bluetooth/audio/vcs.h>
#include <sys/byteorder.h>
#include <sys/printk.h>

#define MAX_STREAMS 2

#define AVAILABLE_SINK_CONTEXT  (BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | \
				 BT_AUDIO_CONTEXT_TYPE_MEDIA)

#define AVAILABLE_SOURCE_CONTEXT BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL

#define CHANNEL_COUNT_1 BIT(0)

static const uint8_t universal_idx = 1;
static const uint8_t outdoor_idx = 5;
static const uint8_t noisy_idx = 8;
static const uint8_t office_idx = 22;

static struct bt_codec lc3_codec_sink =
	BT_CODEC_LC3(BT_CODEC_LC3_FREQ_16KHZ | BT_CODEC_LC3_FREQ_24KHZ, BT_CODEC_LC3_DURATION_10,
		     BT_CODEC_LC3_CHAN_COUNT_SUPPORT, 40u, 60u, 1,
		     (BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL | BT_AUDIO_CONTEXT_TYPE_MEDIA),
		      BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);

static struct bt_codec lc3_codec_source =
	BT_CODEC_LC3(BT_CODEC_LC3_FREQ_16KHZ, BT_CODEC_LC3_DURATION_10,
		     BT_CODEC_LC3_CHAN_COUNT_SUPPORT, 40u, 40u, 1,
		     BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL, BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);

NET_BUF_POOL_FIXED_DEFINE(tx_pool, 1, CONFIG_BT_ISO_TX_MTU, 8, NULL);
static struct bt_conn *default_conn;
static struct bt_audio_stream streams[MAX_STREAMS];
static struct bt_has *has;
static struct bt_vcs *vcs;
static struct k_work adv_work;

static uint8_t unicast_server_addata[] = {
	BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL), /* ASCS UUID */
	BT_AUDIO_UNICAST_ANNOUNCEMENT_TARGETED, /* Target Announcement */
	(((AVAILABLE_SINK_CONTEXT) >>  0) & 0xFF),
	(((AVAILABLE_SINK_CONTEXT) >>  8) & 0xFF),
	(((AVAILABLE_SOURCE_CONTEXT) >>  0) & 0xFF),
	(((AVAILABLE_SOURCE_CONTEXT) >>  8) & 0xFF),
	0x00, /* Metadata length */
};

#if defined(CONFIG_BT_PRIVACY)
/* HAP_d1.0r00; 3.3 Service UUIDs AD Type
 * The HA shall not include the Hearing Access Service UUID in the Service UUID AD type field of
 * the advertising data or scan response data if in one of the GAP connectable modes and if the HA
 * is using a resolvable private address.
 */
#define BT_DATA_UUID16_ALL_VAL BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL)
#else
/* HAP_d1.0r00; 3.3 Service UUIDs AD Type
 * The HA shall include the Hearing Access Service Universally Unique Identifier (UUID) defined in
 * [2] in the Service UUID Advertising Data (AD) Type field of the advertising data or scan
 * response data, if in one of the Generic Access Profile (GAP) discoverable modes.
 */
#define BT_DATA_UUID16_ALL_VAL BT_UUID_16_ENCODE(BT_UUID_ASCS_VAL), \
			       BT_UUID_16_ENCODE(BT_UUID_HAS_VAL)
#endif /* CONFIG_BT_PRIVACY */

/* TODO: Expand with BAP data */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_DATA_UUID16_ALL_VAL),
	BT_DATA(BT_DATA_SVC_DATA16, unicast_server_addata, ARRAY_SIZE(unicast_server_addata)),
};

void print_hex(const uint8_t *ptr, size_t len)
{
	while (len-- != 0) {
		printk("%02x", *ptr++);
	}
}

static void print_codec(const struct bt_codec *codec)
{
	printk("codec 0x%02x cid 0x%04x vid 0x%04x count %u\n",
	       codec->id, codec->cid, codec->vid, codec->data_count);

	for (size_t i = 0; i < codec->data_count; i++) {
		printk("data #%zu: type 0x%02x len %u\n",
		       i, codec->data[i].data.type,
		       codec->data[i].data.data_len);
		print_hex(codec->data[i].data.data,
			  codec->data[i].data.data_len -
				sizeof(codec->data[i].data.type));
		printk("\n");
	}

	for (size_t i = 0; i < codec->meta_count; i++) {
		printk("meta #%zu: type 0x%02x len %u\n",
		       i, codec->meta[i].data.type,
		       codec->meta[i].data.data_len);
		print_hex(codec->meta[i].data.data,
			  codec->meta[i].data.data_len -
				sizeof(codec->meta[i].data.type));
		printk("\n");
	}
}

static void print_qos(struct bt_codec_qos *qos)
{
	printk("QoS: interval %u framing 0x%02x phy 0x%02x sdu %u "
	       "rtn %u latency %u pd %u\n",
	       qos->interval, qos->framing, qos->phy, qos->sdu,
	       qos->rtn, qos->latency, qos->pd);
}

static struct bt_audio_stream *lc3_config(struct bt_conn *conn,
					  struct bt_audio_ep *ep,
					  enum bt_audio_pac_type type,
					  struct bt_audio_capability *cap,
					  struct bt_codec *codec)
{
	printk("ASE Codec Config: conn %p ep %p type %u, cap %p\n",
	       conn, ep, type, cap);

	print_codec(codec);

	for (size_t i = 0; i < ARRAY_SIZE(streams); i++) {
		struct bt_audio_stream *stream = &streams[i];

		if (!stream->conn) {
			printk("ASE Codec Config stream %p\n", stream);
			return stream;
		}
	}

	printk("No streams available\n");

	return NULL;
}

static int lc3_reconfig(struct bt_audio_stream *stream,
			struct bt_audio_capability *cap,
			struct bt_codec *codec)
{
	printk("ASE Codec Reconfig: stream %p cap %p\n", stream, cap);

	print_codec(codec);

	/* We only support one QoS at the moment, reject changes */
	return -ENOEXEC;
}

static int lc3_qos(struct bt_audio_stream *stream, struct bt_codec_qos *qos)
{
	printk("QoS: stream %p qos %p\n", stream, qos);

	print_qos(qos);

	return 0;
}

static int lc3_enable(struct bt_audio_stream *stream,
		      struct bt_codec_data *meta,
		      size_t meta_count)
{
	printk("Enable: stream %p meta_count %u\n", stream, meta_count);

	return 0;
}

static int lc3_start(struct bt_audio_stream *stream)
{
	printk("Start: stream %p\n", stream);

	return 0;
}

static int lc3_metadata(struct bt_audio_stream *stream,
			struct bt_codec_data *meta,
			size_t meta_count)
{
	printk("Metadata: stream %p meta_count %u\n", stream, meta_count);

	return 0;
}

static int lc3_disable(struct bt_audio_stream *stream)
{
	printk("Disable: stream %p\n", stream);

	return 0;
}

static int lc3_stop(struct bt_audio_stream *stream)
{
	printk("Stop: stream %p\n", stream);

	return 0;
}

static int lc3_release(struct bt_audio_stream *stream)
{
	printk("Release: stream %p\n", stream);

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

static void stream_started(struct bt_audio_stream *stream)
{
	printk("Audio Stream %p started\n", stream);
}

static void stream_stopped(struct bt_audio_stream *stream)
{
	printk("Audio Stream %p stopped\n", stream);
}

static void stream_recv(struct bt_audio_stream *stream, struct net_buf *buf)
{
	printk("Incoming audio on stream %p len %u\n", stream, buf->len);
}

static struct bt_audio_stream_ops stream_ops = {
	.started = stream_started,
	.stopped = stream_stopped,
	.recv = stream_recv,
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != 0) {
		printk("Failed to connect to %s (%u)\n", addr, err);

		default_conn = NULL;
		return;
	}

	printk("Connected: %s\n", addr);
	default_conn = bt_conn_ref(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (conn != default_conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	bt_conn_unref(default_conn);
	default_conn = NULL;

	/* Restart advertising after disconnection */
	k_work_submit(&adv_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

/* HAP_d1.0r00; 3.7 BAP Unicast Server role requirements
 * The HA shall support a Presentation Delay range in the Codec Configured state that includes
 * the value of 20ms, in addition to the requirement of Table 5.2 of [3].
 */
#define PD_MIN_USEC 20000

/* BAP_v1.0; Table 5.2: QoS configuration support setting requirements for the Unicast Client and
 * Unicast Server
 */
#define PD_MAX_USEC 40000

static struct bt_audio_capability caps[] = {
	{
		.type = BT_AUDIO_SINK,
		.pref = BT_AUDIO_CAPABILITY_PREF(BT_AUDIO_CAPABILITY_UNFRAMED_SUPPORTED,
						 BT_GAP_LE_PHY_2M, 0x02, 10,
						 PD_MIN_USEC, PD_MAX_USEC,
						 PD_MIN_USEC, PD_MAX_USEC),
		.codec = &lc3_codec_sink,
		.ops = &lc3_ops,
	},
	{
		.type = BT_AUDIO_SOURCE,
		.pref = BT_AUDIO_CAPABILITY_PREF(BT_AUDIO_CAPABILITY_UNFRAMED_SUPPORTED,
						 BT_GAP_LE_PHY_2M, 0x02, 10,
						 PD_MIN_USEC, PD_MAX_USEC,
						 PD_MIN_USEC, PD_MAX_USEC),
		.codec = &lc3_codec_source,
		.ops = &lc3_ops,
	},
};

static int set_active_preset_cb(struct bt_has *has, uint8_t index, bool sync)
{
	int err;

	printk("Set active preset index 0x%02x sync %d\n", index, sync);

	err = bt_has_preset_active_set(has, index);
	if (err < 0) {
		printk("Set active failed (err %d)\n", err);
	}

	return err;
}

struct bt_has_ops preset_ops = {
	.active_set = set_active_preset_cb,
};

static int has_init(void)
{
	struct bt_has_register_param param = {
		.preset_param = {
			{
				.index = universal_idx,
				.properties = BT_HAS_PROP_WRITABLE | BT_HAS_PROP_AVAILABLE,
				.name = "Universal",
			},
			{
				.index = outdoor_idx,
				.properties = BT_HAS_PROP_WRITABLE | BT_HAS_PROP_AVAILABLE,
				.name = "Outdoor",
			},
			{
				.index = noisy_idx,
				.properties = BT_HAS_PROP_WRITABLE | BT_HAS_PROP_AVAILABLE,
				.name = "Noisy environment",
			},
			{
				.index = office_idx,
				.properties = BT_HAS_PROP_WRITABLE | BT_HAS_PROP_AVAILABLE,
				.name = "Office",
			},
		},
		.ops = &preset_ops,
	};

	return bt_has_register(&param, &has);
}

static uint32_t accepted_broadcast_id;
static struct bt_audio_base received_base;
static bool sink_syncable;
static struct bt_audio_stream broadcast_sink_streams[BROADCAST_SNK_STREAM_CNT];
static struct bt_audio_broadcast_sink *default_sink;

static bool scan_recv(const struct bt_le_scan_recv_info *info,
		     uint32_t broadcast_id)
{
	printk("Found broadcaster with ID 0x%06X\n", broadcast_id);

	if (broadcast_id == accepted_broadcast_id) {
		printk("PA syncing to broadcaster\n");
		accepted_broadcast_id = 0;
		return true;
	}

	return false;
}

static void pa_synced(struct bt_audio_broadcast_sink *sink,
		      struct bt_le_per_adv_sync *sync,
		      uint32_t broadcast_id)
{
	printk("PA synced to broadcaster with ID 0x%06X as sink %p\n", broadcast_id, sink);

	if (default_sink == NULL) {
		default_sink = sink;

		printk("Sink %p is set as default\n", sink);
	}
}

static void base_recv(struct bt_audio_broadcast_sink *sink,
		      const struct bt_audio_base *base)
{
	uint8_t bis_indexes[BROADCAST_SNK_STREAM_CNT] = { 0 };
	/* "0xXX " requires 5 characters */
	char bis_indexes_str[5 * ARRAY_SIZE(bis_indexes) + 1];
	size_t index_count = 0;

	if (memcmp(base, &received_base, sizeof(received_base)) == 0) {
		/* Don't print duplicates */
		return;
	}

	printk("Received BASE from sink %p:\n", sink);

	for (int i = 0; i < base->subgroup_count; i++) {
		const struct bt_audio_base_subgroup *subgroup;

		subgroup = &base->subgroups[i];

		printk("Subgroup[%d]:\n", i);
		print_codec(&subgroup->codec);

		for (int j = 0; j < subgroup->bis_count; j++) {
			const struct bt_audio_base_bis_data *bis_data;

			bis_data = &subgroup->bis_data[j];

			printk("BIS[%d] index 0x%02x\n", i, bis_data->index);
			bis_indexes[index_count++] = bis_data->index;

			for (int k = 0; k < bis_data->data_count; k++) {
				const struct bt_codec_data *codec_data;

				codec_data = &bis_data->data[k];

				printk("data #%u: type 0x%02x len %u\n", i, codec_data->data.type,
				       codec_data->data.data_len);
				print_hex(codec_data->data.data,
					  codec_data->data.data_len - sizeof(codec_data->data.type));
			}

			printk("\n");
		}
	}

	memset(bis_indexes_str, 0, sizeof(bis_indexes_str));
	/* Create space separated list of indexes as hex values */
	for (int i = 0; i < index_count; i++) {
		char bis_index_str[6];

		sprintf(bis_index_str, "0x%02x ", bis_indexes[i]);

		strcat(bis_indexes_str, bis_index_str);
		printk("[%d]: %s\n", i, bis_index_str);
	}

	printk("Possible indexes: %s\n", bis_indexes_str);

	(void)memcpy(&received_base, base, sizeof(received_base));
}

static void syncable(struct bt_audio_broadcast_sink *sink, bool encrypted)
{
	if (sink_syncable) {
		return;
	}

	printk("Sink %p is ready to sync %s encryption\n", sink, encrypted ? "with" : "without");
	sink_syncable = true;
}

static void scan_term(int err)
{
	printk("Broadcast scan was terminated: %d\n", err);

}

static void pa_sync_lost(struct bt_audio_broadcast_sink *sink)
{
	printk("Sink %p disconnected\n", sink);

	if (default_sink == sink) {
		default_sink = NULL;
		sink_syncable = false;
	}
}

static struct bt_audio_broadcast_sink_cb bcast_sink_cbs = {
	.scan_recv = scan_recv,
	.pa_synced = pa_synced,
	.base_recv = base_recv,
	.syncable = syncable,
	.scan_term = scan_term,
	.pa_sync_lost = pa_sync_lost,
};

static void vcs_state_cb(struct bt_vcs *vcs, int err, uint8_t volume,
			 uint8_t mute)
{
	if (err) {
		printk("VCS state get failed (%d)\n", err);
	} else {
		printk("VCS volume %u, mute %u\n", volume, mute);
	}
}

static void vcs_flags_cb(struct bt_vcs *vcs, int err, uint8_t flags)
{
	if (err) {
		printk("VCS flags get failed (%d)\n", err);
	} else {
		printk("VCS flags 0x%02X\n", flags);
	}
}

static struct bt_vcs_cb vcs_cbs = {
	.state = vcs_state_cb,
	.flags = vcs_flags_cb,
};

#if CONFIG_BT_VCS_AICS_INSTANCE_COUNT > 0
static void aics_state_cb(struct bt_aics *inst, int err, int8_t gain,
			  uint8_t mute, uint8_t mode)
{
	if (err) {
		printk("AICS state get failed (%d) for inst %p\n", err, inst);
	} else {
		printk("AICS inst %p state gain %d, mute %u, mode %u\n", inst, gain, mute, mode);
	}
}

static void aics_gain_setting_cb(struct bt_aics *inst, int err, uint8_t units,
				 int8_t minimum, int8_t maximum)
{
	if (err) {
		printk("AICS gain settings get failed (%d) for inst %p\n", err, inst);
	} else {
		printk("AICS inst %p gain settings units %u, min %d, max %d\n", inst, units,
		       minimum, maximum);
	}
}

static void aics_input_type_cb(struct bt_aics *inst, int err,
			       uint8_t input_type)
{
	if (err) {
		printk("AICS input type get failed (%d) for inst %p\n", err, inst);
	} else {
		printk("AICS inst %p input type %u\n", inst, input_type);
	}
}

static void aics_status_cb(struct bt_aics *inst, int err, bool active)
{
	if (err) {
		printk("AICS status get failed (%d) for inst %p\n", err, inst);
	} else {
		printk("AICS inst %p status %s\n", inst, active ? "active" : "inactive");
	}

}
static void aics_description_cb(struct bt_aics *inst, int err,
				char *description)
{
	if (err) {
		printk("AICS description get failed (%d) for inst %p\n", err, inst);
	} else {
		printk("AICS inst %p description %s\n", inst, description);
	}
}

static struct bt_aics_cb aics_cbs = {
	.state = aics_state_cb,
	.gain_setting = aics_gain_setting_cb,
	.type = aics_input_type_cb,
	.status = aics_status_cb,
	.description = aics_description_cb
};
#endif /* CONFIG_BT_VCS_AICS_INSTANCE_COUNT */

#if CONFIG_BT_VCS_VOCS_INSTANCE_COUNT > 0
static void vocs_state_cb(struct bt_vocs *inst, int err, int16_t offset)
{
	if (err) {
		printk("VOCS state get failed (%d) for inst %p\n", err, inst);
	} else {
		printk("VOCS inst %p offset %d\n", inst, offset);
	}
}

static void vocs_location_cb(struct bt_vocs *inst, int err, uint32_t location)
{
	if (err) {
		printk("VOCS location get failed (%d) for inst %p\n", err, inst);
	} else {
		printk("VOCS inst %p location %u\n", inst, location);
	}
}

static void vocs_description_cb(struct bt_vocs *inst, int err,
				char *description)
{
	if (err) {
		printk("VOCS description get failed (%d) for inst %p\n", err, inst);
	} else {
		printk("VOCS inst %p description %s\n", inst, description);
	}
}

static struct bt_vocs_cb vocs_cbs = {
	.state = vocs_state_cb,
	.location = vocs_location_cb,
	.description = vocs_description_cb
};
#endif /* CONFIG_BT_VCS_VOCS_INSTANCE_COUNT */

static int vcs_init(void)
{
	struct bt_vcs_register_param param;

	memset(&param, 0, sizeof(param));

#if CONFIG_BT_VCS_VOCS_INSTANCE_COUNT > 0
	char output_desc[CONFIG_BT_VCS_VOCS_INSTANCE_COUNT][16];

	for (int i = 0; i < ARRAY_SIZE(param.vocs_param); i++) {
		param.vocs_param[i].location_writable = true;
		param.vocs_param[i].desc_writable = true;
		snprintf(output_desc[i], sizeof(output_desc[i]),
			 "Output %d", i + 1);
		param.vocs_param[i].output_desc = output_desc[i];
		param.vocs_param[i].cb = &vocs_cbs;
	}
#endif /* CONFIG_BT_VCS_VOCS_INSTANCE_COUNT */

#if CONFIG_BT_VCS_AICS_INSTANCE_COUNT > 0
	char input_desc[CONFIG_BT_VCS_AICS_INSTANCE_COUNT][16];

	for (int i = 0; i < ARRAY_SIZE(param.aics_param); i++) {
		param.aics_param[i].desc_writable = true;
		snprintf(input_desc[i], sizeof(input_desc[i]),
			 "Input %d", i + 1);
		param.aics_param[i].description = input_desc[i];
		param.aics_param[i].type = BT_AICS_INPUT_TYPE_UNSPECIFIED;
		param.aics_param[i].status = true;
		param.aics_param[i].gain_mode = BT_AICS_MODE_MANUAL;
		param.aics_param[i].units = 1;
		param.aics_param[i].min_gain = -100;
		param.aics_param[i].max_gain = 100;
		param.aics_param[i].cb = &aics_cbs;
	}
#endif /* CONFIG_BT_VCS_AICS_INSTANCE_COUNT */

	param.step = 1;
	param.mute = BT_VCS_STATE_UNMUTED;
	param.volume = 100;

	param.cb = &vcs_cbs;

	return bt_vcs_register(&param, &vcs);
}

#if defined(CONFIG_BT_MICS)
static struct bt_mics *mics;

static void mics_mute_cb(struct bt_mics *mics, int err, uint8_t mute)
{
	if (err != 0) {
		printk("Mute get failed (%d)\n", err);
	} else {
		printk("Mute value %u\n", mute);
	}
}

static struct bt_mics_cb mics_cbs = {
	.mute = mics_mute_cb,
};

#if CONFIG_BT_MICS_AICS_INSTANCE_COUNT > 0
static void mics_aics_state_cb(struct bt_aics *inst, int err, int8_t gain,
			       uint8_t mute, uint8_t mode)
{
	if (err != 0) {
		printk("AICS state get failed (%d) for inst %p\n", err, inst);
	} else {
		printk("AICS inst %p state gain %d, mute %u, mode %u\n", inst, gain, mute, mode);
	}

}
static void mics_aics_gain_setting_cb(struct bt_aics *inst, int err,
				      uint8_t units, int8_t minimum,
				      int8_t maximum)
{
	if (err != 0) {
		printk("AICS gain settings get failed (%d) for inst %p\n", err, inst);
	} else {
		printk("AICS inst %p gain settings units %u, min %d, max %d\n", inst, units,
		       minimum, maximum);
	}

}
static void mics_aics_input_type_cb(struct bt_aics *inst, int err,
				    uint8_t input_type)
{
	if (err != 0) {
		printk("AICS input type get failed (%d) for inst %p\n", err, inst);
	} else {
		printk("AICS inst %p input type %u\n", inst, input_type);
	}

}
static void mics_aics_status_cb(struct bt_aics *inst, int err, bool active)
{
	if (err != 0) {
		printk("AICS status get failed (%d) for inst %p\n", err, inst);
	} else {
		printk("AICS inst %p status %s\n", inst, active ? "active" : "inactive");
	}

}
static void mics_aics_description_cb(struct bt_aics *inst, int err,
				     char *description)
{
	if (err != 0) {
		printk("AICS description get failed (%d) for inst %p\n", err, inst);
	} else {
		printk("AICS inst %p description %s\n", inst, description);
	}
}

static struct bt_aics_cb aics_cb = {
	.state = mics_aics_state_cb,
	.gain_setting = mics_aics_gain_setting_cb,
	.type = mics_aics_input_type_cb,
	.status = mics_aics_status_cb,
	.description = mics_aics_description_cb,
};
#endif /* CONFIG_BT_MICS_AICS_INSTANCE_COUNT > 0 */

static int mics_init(void)
{
	struct bt_mics_register_param mics_param;
	char input_desc[CONFIG_BT_MICS_AICS_INSTANCE_COUNT][16];

	(void)memset(&mics_param, 0, sizeof(mics_param));

#if CONFIG_BT_MICS_AICS_INSTANCE_COUNT > 0
	for (int i = 0; i < ARRAY_SIZE(mics_param.aics_param); i++) {
		mics_param.aics_param[i].desc_writable = true;
		snprintf(input_desc[i], sizeof(input_desc[i]),
			 "Input %d", i + 1);
		mics_param.aics_param[i].description = input_desc[i];
		mics_param.aics_param[i].type = BT_AICS_INPUT_TYPE_UNSPECIFIED;
		mics_param.aics_param[i].status = true;
		mics_param.aics_param[i].gain_mode = BT_AICS_MODE_MANUAL;
		mics_param.aics_param[i].units = 1;
		mics_param.aics_param[i].min_gain = -100;
		mics_param.aics_param[i].max_gain = 100;
		mics_param.aics_param[i].cb = &aics_cb;
	}
#endif /* CONFIG_BT_MICS_AICS_INSTANCE_COUNT > 0 */

	mics_param.cb = &mics_cbs;

	return bt_mics_register(&mics_param, &mics);
}
#endif /* CONFIG_BT_MICS */

static int bcast_sink_init(void)
{
	int i;

	bt_audio_broadcast_sink_register_cb(&bcast_sink_cbs);

	for (i = 0; i < ARRAY_SIZE(broadcast_sink_streams); i++) {
		bt_audio_stream_cb_register(&broadcast_sink_streams[i],
					    &stream_ops);
	}

	return 0;
}

static int le_ext_adv_create(struct bt_le_ext_adv **adv)
{
	int err;

	/* Create a non-connectable non-scannable advertising set */
	err = bt_le_ext_adv_create(BT_LE_EXT_ADV_CONN_NAME, NULL, adv);
	if (err) {
		printk("Failed to create advertising set (err %d)\n", err);
		return err;
	}

	err = bt_le_ext_adv_set_data(*adv, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Failed to set advertising data (err %d)\n", err);
		return err;
	}

	return err;
}

static void adv_work_process(struct k_work *work)
{
	static struct bt_le_ext_adv *adv = NULL;
	int err;

	if (!adv) {
		err = le_ext_adv_create(&adv);
		__ASSERT_NO_MSG(adv);
	}

	err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (err) {
		printk("Failed to start advertising set (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");
}

void main(void)
{
	int err;

	err = bt_enable(NULL);
	if (err != 0) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	for (size_t i = 0; i < ARRAY_SIZE(caps); i++) {
		bt_audio_capability_register(&caps[i]);
	}

	for (size_t i = 0; i < ARRAY_SIZE(streams); i++) {
		bt_audio_stream_cb_register(&streams[i], &stream_ops);
	}

	printk("Unicast Sink initialized\n");

	err = has_init();
	if (err) {
		printk("HAS init failed (err %d)\n", err);
		return;
	}

	printk("HAS initialized\n");

	err = vcs_init();
	if (err) {
		printk("VCS init failed (err %d)\n", err);
		return;
	}

	printk("VCS initialized\n");

#if defined(CONFIG_BT_MICS)
	err = mics_init();
	if (err) {
		printk("MICS init failed (err %d)\n", err);
		return;
	}

	printk("MICS initialized\n");
#endif /* CONFIG_BT_MICS */

	bcast_sink_init();
	printk("Broadcast Sink initialized\n");

	k_work_init(&adv_work, adv_work_process);
	k_work_submit(&adv_work);
}
