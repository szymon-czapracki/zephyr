/** @file
 *  @brief Internal APIs for Bluetooth Hearing Access Profile.
 */

/*
 * Copyright (c) 2021 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <sys/dlist.h>
#include <bluetooth/audio/has.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/uuid.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BT_HAS_PRESET_NAME_MIN 1
#define BT_HAS_PRESET_NAME_MAX 40

#define BT_HAS_PRESET_INDEX_NONE 0x00

#define BT_HAS_OP_READ_ALL_PRESETS 0x00
#define BT_HAS_OP_READ_PRESET_BY_INDEX 0x01
#define BT_HAS_OP_READ_PRESET_RSP 0x02
#define BT_HAS_OP_PRESET_CHANGED 0x03
#define BT_HAS_OP_WRITE_PRESET_NAME 0x04
#define BT_HAS_OP_SET_ACTIVE_PRESET 0x05
#define BT_HAS_OP_SET_NEXT_PRESET 0x06
#define BT_HAS_OP_SET_PREV_PRESET 0x07
#define BT_HAS_OP_SET_ACTIVE_PRESET_SYNC 0x08
#define BT_HAS_OP_SET_NEXT_PRESET_SYNC 0x09
#define BT_HAS_OP_SET_PREV_PRESET_SYNC 0x0a

#define BT_HAS_ERR_INVALID_OP 0x80
#define BT_HAS_ERR_WRITE_NAME_NOT_ALLOWED 0x81
#define BT_HAS_ERR_PRESET_SYNC_NOT_SUPP 0x82
#define BT_HAS_ERR_OPERATION_NOT_POSSIBLE 0x83
#define BT_HAS_ERR_INVALID_PARAM_LEN 0x84

struct bt_has_cp_hdr {
	uint8_t op;
	uint8_t data[0];
} __packed;

struct bt_has_cp_read_preset_by_index_req {
	uint8_t index;
	uint8_t name[0];
} __packed;

struct bt_has_cp_write_preset_name_req {
	uint8_t index;
	uint8_t name[0];
} __packed;

struct bt_has_cp_set_active_preset_req {
	uint8_t index;
} __packed;

struct bt_has_cp_read_preset_rsp {
	uint8_t is_last;
	uint8_t index;
	uint8_t properties;
	uint8_t name[0];
} __packed;

static inline const char *bt_has_op_str(uint8_t op)
{
	switch (op) {
	case BT_HAS_OP_READ_ALL_PRESETS:
		return "Read all presets";
	case BT_HAS_OP_READ_PRESET_BY_INDEX:
		return "Read preset by index";
	case BT_HAS_OP_READ_PRESET_RSP:
		return "Read preset response";
	case BT_HAS_OP_PRESET_CHANGED:
		return "Preset changed";
	case BT_HAS_OP_WRITE_PRESET_NAME:
		return "Write preset name";
	case BT_HAS_OP_SET_ACTIVE_PRESET:
		return "Set active preset";
	case BT_HAS_OP_SET_NEXT_PRESET:
		return "Set next preset";
	case BT_HAS_OP_SET_PREV_PRESET:
		return "Set previous preset";
	case BT_HAS_OP_SET_ACTIVE_PRESET_SYNC:
		return "Set active preset (synchronized)";
	case BT_HAS_OP_SET_NEXT_PRESET_SYNC:
		return "Set next preset (synchronized)";
	case BT_HAS_OP_SET_PREV_PRESET_SYNC:
		return "Set previous preset (synchronized)";
	default:
		return "Unknown";
	}
}

struct bt_has {
	/** Registered application callbacks */
	struct bt_has_cb *cb;

	/** Hearing Aid Fetures value */
	uint8_t features;
};

/** @brief Hearing Access Profile server internal representation */
struct bt_has_server {
	/** Common profile reference object */
	struct bt_has has;

	/** Preset operations structure */
	struct bt_has_preset_ops *preset_ops;

	/** Currently active preset */
	struct bt_has_preset *active_preset;

	/** Preset list */
	sys_dlist_t preset_list;
};

/** @def BT_HAS_SERVER(_has)
 *  @brief Helper macro getting container object of type bt_has_server
 *  address having the same container has member address as object in question.
 *
 *  @param _has Address of object of bt_has type
 *
 *  @return Address of in memory bt_has_server object type containing
 *          the address of in question object.
 */
#define BT_HAS_SERVER(_has) CONTAINER_OF(_has, struct bt_has_server, has)

/** @brief Hearing Access Profile client internal representation */
struct bt_has_client {
	/** Common profile reference object */
	struct bt_has has;

	/** Profile connection reference */
	struct bt_conn *conn;

	uint16_t active_preset_handle;
	uint8_t active_preset_index;

	bool busy;
	union {
		struct bt_has_discover_params *discover;
		struct bt_gatt_read_params read_params;
		struct bt_gatt_write_params write_params;
	};

	struct bt_gatt_subscribe_params features_subscription;
	struct bt_gatt_subscribe_params control_point_subscription;
	struct bt_gatt_subscribe_params active_preset_subscription;
};

/** @def BT_HAS_CLIENT(_has)
 *  @brief Helper macro getting container object of type bt_has_client
 *  address having the same container has member address as object in question.
 *
 *  @param _has Address of object of bt_has type
 *
 *  @return Address of in memory bt_has_client object type containing
 *          the address of in question object.
 */
#define BT_HAS_CLIENT(_has) CONTAINER_OF(_has, struct bt_has_client, has)

int bt_has_client_preset_active_get(struct bt_has *has);
int bt_has_client_preset_active_set(struct bt_has *has, uint8_t index);
int bt_has_client_preset_active_set_next(struct bt_has *has);
int bt_has_client_preset_active_set_prev(struct bt_has *has);
int bt_has_client_preset_get(struct bt_has *has, uint8_t index);
int bt_has_client_preset_name_set(struct bt_has *has, uint8_t index, const char *name, ssize_t len);
int bt_has_client_preset_list_get(struct bt_has *has);

#ifdef __cplusplus
}
#endif
