/*
 * Copyright (c) 2022 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_BLUETOOTH_AUDIO_HAS_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_AUDIO_HAS_H_

/**
 * @brief Hearing Access Service (HAS)
 *
 * @defgroup bt_has Hearing Access Service (HAS)
 *
 * @ingroup bluetooth
 * @{
 *
 * The Hearing Access Service is used to identify a hearing aid and optionally
 * to control hearing aid presets. This API implements the server functionality.
 *
 * [Experimental] Users should note that the APIs can change as a part of
 * ongoing development.
 */

#include <zephyr/types.h>
#include <sys/dlist.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/gatt.h>
#include <bluetooth/uuid.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_BT_HAS)
#define BT_HAS_PRESET_COUNT CONFIG_BT_HAS_PRESET_COUNT
#else
#define BT_HAS_PRESET_COUNT 0
#endif /* CONFIG_BT_HAS */

/** @brief Opaque Hearing Access Service object. */
struct bt_has;

/** Hearing Aid device type */
typedef enum __packed {
	/** Binaural Hearing Aid. */
	BT_HAS_BINAURAL_HEARING_AID,

	/** Monaural Hearing Aid */
	BT_HAS_MONAURAL_HEARING_AID,

	/** Banded Hearing Aid */
	BT_HAS_BANDED_HEARING_AID,
} bt_has_type_t;

/** Preset Properties values */
enum {
	/** No properties set */
	BT_HAS_PROP_NONE = 0,

	/** Preset name can be written by the client */
	BT_HAS_PROP_WRITABLE = BIT(0),

	/** Preset availability */
	BT_HAS_PROP_AVAILABLE = BIT(1),
};

/** @brief Preset representation */
struct bt_has_preset {
	/** Preset index */
	uint8_t index;
	/** Preset properties */
	uint8_t properties;
	/** Preset name */
	const char *name;

	sys_dnode_t node;
};

/**
 * @def BT_HAS_PRESET_DEFINE
 * @brief Statically define and register preset record.
 *
 * Helper macro to statically define and register preset record.
 *
 * @param _index	Preset index.
 * @param _name		User-friendly preset name.
 * @param _properties	Preset properties.
 */
#define BT_HAS_PRESET_DEFINE(_index, _name, _properties)                                           \
	const STRUCT_SECTION_ITERABLE(bt_has_preset, _bt_has_preset_##_index) = {                  \
		.index = _index,                                                                   \
		.properties = _properties,                                                         \
		.name = _name,                                                                     \
	};

/** @brief Preset operations structure. */
struct bt_has_preset_ops {
	/**
	 * @brief Preset set active callback
	 *
	 * Once the preset becomes active, the bt_has_preset_active_set shall
	 * be called to notify all the clients.
	 *
	 * @param has	Pointer to the Hearing Access Service object.
	 * @param index	Preset record index requested to activate
	 * @param sync	Whether the server must relay this change to the
	 *		other member of the Binaural Hearing Aid Set.
	 *
	 * @return 0 in case of success or -EBUSY if operation cannot be
	 *	   executed at the time.
	 */
	int (*active_set)(struct bt_has *has, uint8_t index, bool sync);
};

/**
 * @brief Register Hearing Access Service.
 *
 * @param      ops	Preset operations structure.
 * @param[out] has	Pointer to the local Hearing Access Service object.
 * 			This will still be valid if the return value is
 * 			-EALREADY.
 *
 * @return 0 if success, errno on failure.
 */
int bt_has_register(struct bt_has_preset_ops *ops, struct bt_has **has);

struct bt_has_discover_params;

/** @typedef bt_has_discover_func_t
 *  @brief Discover Hearing Access Service callback function.
 */
typedef void (*bt_has_discover_func_t)(struct bt_conn *conn, struct bt_has *has,
				       struct bt_has_discover_params *params);

struct bt_has_discover_params {
	bt_has_discover_func_t func;

	struct bt_uuid_16 uuid;
	union {
		struct bt_gatt_read_params read;
		struct bt_gatt_discover_params discover;
	};
};

/**
 * @brief Get a Hearing Access Service.
 *
 * Find a Hearing Access Service on a server identified by conn.
 *
 * @param conn	Bluetooth connection object.
 *
 * @return 0 if success, errno on failure.
 */
int bt_has_discover(struct bt_conn *conn, struct bt_has_discover_params *params);

/**
 * @brief Get Active Preset.
 *
 * Get the index of currently active preset.
 *
 * @param has	Pointer to the Hearing Access Service object.
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_active_get(struct bt_has *has);

/**
 * @brief Set Active Preset.
 *
 * Set the preset record identified by the Index field as the active preset.
 *
 * @param has	Pointer to the Hearing Access Service object.
 * @param index	Preset record index.
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_active_set(struct bt_has *has, uint8_t index);

/**
 * @brief Set Next Preset.
 *
 * Set the next preset record on the server list as the active preset.
 *
 * @param has	Pointer to the Hearing Access Service object.
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_active_set_next(struct bt_has *has);

/**
 * @brief Set Previous Preset.
 *
 * Set the previous preset record on the server list as the active preset.
 *
 * @param has	Pointer to the Hearing Access Service object.
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_active_set_prev(struct bt_has *has);

/**
 * @brief Set Preset Name.
 *
 * Set the preset record name identified by the Index field.
 *
 * @param has	Pointer to the Hearing Access Service object.
 * @param index	Preset record index.
 * @param name	Preset name to be written.
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_name_set(struct bt_has *has, uint8_t index, const char *name);

/**
 * @brief Get Preset Record.
 *
 * Get the preset record identified by the Index field.
 *
 * @param has	Pointer to the Hearing Access Service object.
 * @param index	Preset record index.
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_get(struct bt_has *has, uint8_t index);

/**
 * @brief Get Preset List.
 *
 * Get the preset record list.
 *
 * @param has	Pointer to the Hearing Access Service object.
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_list_get(struct bt_has *has);

/** @brief Hearing Access Service callback structure. */
struct bt_has_cb {
	/**
	 * @brief Callback function for Hearing Access Service state.
	 *
	 * Called when the state is locally read as the server.
	 * Called when the state is remotely read as the client.
	 * Called if the state is changed by either the server or client.
	 *
	 * @param has		Pointer to the Hearing Access Service object.
	 * @param err		0 in case of success or negative value in case
	 *			of error.
	 * @param ha_type	Hearing Aid device type.
	 * @param active_index	Active preset index.
	 * @param num_presets	Number of presets.
	 */
	void (*state)(struct bt_has *has, int err, uint8_t ha_type, uint8_t active_index,
		      uint8_t num_presets);

	/**
	 * @brief Callback function for Hearing Access Service active preset.
	 *
	 * Called when the value is locally read as the server.
	 * Called when the value is remotely read as the client.
	 * Called if the value is changed by either the server or client.
	 *
	 * @param has		Pointer to the Hearing Access Service object.
	 * @param err		0 in case of success or negative value in case
	 *			of error.
	 * @param index		Active preset index.
	 */
	void (*active_preset)(struct bt_has *has, int err, uint8_t index);

	/**
	 * @brief Callback function for Hearing Access Service preset name.
	 *
	 * Called when the value is locally read as the server.
	 * Called when the value is remotely read as the client.
	 * Called if the value is changed by either the server or client.
	 *
	 * @param has		Pointer to the Hearing Access Service object.
	 * @param err		0 in case of success or negative value in case
	 *			of error.
	 * @param preset	Preset record including name.
	 */
	void (*preset)(struct bt_has *has, int err, uint8_t index, uint8_t properties,
		       const char *name);
};

/**
 * @def BT_HAS_CB_DEFINE
 *
 * @brief Register a callback structure for Hearing Access Service events.
 *
 * @param _name Name of callback structure.
 */
#define BT_HAS_CB_DEFINE(_name)                                                                    \
	static const STRUCT_SECTION_ITERABLE(bt_has_cb, _CONCAT(bt_has_cb_, _name))

/**
 * @brief Get the Bluetooth connection object of the service object.
 *
 * @param has	Service object.
 *
 * @return Bluetooth connection object or NULL if local service object.
 */
struct bt_conn *bt_has_conn_get(const struct bt_has *has);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_AUDIO_HAS_H_ */
