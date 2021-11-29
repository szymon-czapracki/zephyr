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
#define BT_HAS_PRESET_NAME_MAX 40

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
} bt_has_hearing_aid_type_t;

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
	/** TODO: move somewhere else  */
	bool visible;
};

#if defined(CONFIG_BT_HAS_PRESET_NAME_DYNAMIC)
#define BT_HAS_PRESET_NAME(_index, _name)	_bt_has_preset_name_##_index
#define BT_HAS_PRESET_NAME_DEFINE(_index, _name)                                           	   \
	static char _bt_has_preset_name_##_index[BT_HAS_PRESET_NAME_MAX + 1] = _name;
#else
#define BT_HAS_PRESET_NAME(_index, _name)	_name
#define BT_HAS_PRESET_NAME_DEFINE(_index, _name)
#endif /* CONFIG_BT_HAS_PRESET_NAME_DYNAMIC */

/**
 * @def BT_HAS_PRESET_DEFINE
 * @brief Statically define and register preset record.
 *
 * Helper macro to statically define and register preset record.
 *
 * @param _index	Preset index.
 * @param _name		Preset name NULL-terminated C string.
 * @param _properties	Preset properties.
 */
#define BT_HAS_PRESET_DEFINE(_index, _name, _properties)                                           \
	BUILD_ASSERT(sizeof(_name) <= (BT_HAS_PRESET_NAME_MAX + 1));			  	   \
	BT_HAS_PRESET_NAME_DEFINE(_index, _name);						   \
	const STRUCT_SECTION_ITERABLE(bt_has_preset, _bt_has_preset_##_index) = {                  \
		.index = _index,                                                                   \
		.properties = _properties,                                                         \
		.name = BT_HAS_PRESET_NAME(_index, _name),                                         \
		.visible = true,                                                                   \
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

/**
 * @brief Get a Hearing Access Service.
 *
 * Find a Hearing Access Service on a server identified by conn.
 *
 * @param conn	Bluetooth connection object.
 *
 * @return 0 if success, errno on failure.
 */
int bt_has_discover(struct bt_conn *conn);

/**
 * @brief Get Active Preset.
 *
 * Get the index of currently active preset.
 *
 * The value is returned in the bt_has_cb.active_preset callback.
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
 * @brief Clear out Active Preset.
 *
 * @param has	Pointer to the Hearing Access Service object.
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_active_clear(struct bt_has *has);

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
 * @brief Set visibility of preset record. Invisible presets are not removed
 * from list but are not visible by peer devices.
 *
 * @param has			Pointer to the Hearing Access Service object.
 * @param index			Preset record index.
 * @param visible               true if preset should be visible
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_visibility_set(struct bt_has *has, uint8_t index, bool visible);

/**
 * @brief Set availability of preset record. Available preset can be set by peer
 * device as active preset.
 *
 * @param has			Pointer to the Hearing Access Service object.
 * @param index			Preset record index.
 * @param available             true if preset should be available
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_availability_set(struct bt_has *has, uint8_t index, bool available);

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

enum {
	BT_HAS_PRESET_READ_STOP = 0,
	BT_HAS_PRESET_READ_CONTINUE,
};

struct bt_has_preset_read_params;

/** @typedef bt_has_preset_read_func_t
 *  @brief Read preset callback function.
 *
 *  @param has		Pointer to the Hearing Access Service object.
 *  @param err		error code.
 *  @param params	Read parameters used.
 *  @param preset	Preset object, or NULL if not found.
 *
 *  If read procedure has completed this callback will be called with
 *  preset set to NULL. This will not happen if procedure was stopped by
 *  returning BT_HAS_PRESET_READ_STOP.
 *
 *  The preset object as well as its name are temporary and must be copied
 *  in order to cache its information.
 *  Only the following fields of the attribute contains valid information:
 *   - index		Preset index.
 *   - properties	Preset properties.
 *   - name		Preset name.
 *
 *  @return BT_HAS_PRESET_READ_CONTINUE to continue read procedure.
 *  @return BT_HAS_PRESET_READ_STOP to stop read procedure.
 */
typedef uint8_t (*bt_has_preset_read_func_t)(struct bt_has *has, int err,
					     struct bt_has_preset_read_params *params,
					     struct bt_has_preset *preset);

struct bt_has_preset_read_params {
	/** Read preset callback. */
	bt_has_preset_read_func_t func;
	/** If true index parameter is used.
	 *  If false by_count.start_index and by_count.preset_count are used.
	 */
	bool by_index;
	union {
		/** Preset index. */
		uint8_t index;
		struct {
			/** First requested index number. */
			uint8_t start_index;
			/** Number of presets to read. */
			uint8_t preset_count;
		} by_count;
	};
};

/**
 * @brief Read Preset Record.
 *
 * Read the preset record identified by the Index field.
 *
 * The Response comes in callback @p params->func. The callback is run from
 * the BT RX thread. @p params must remain valid until start of callback.
 *
 * @param has		Pointer to the Hearing Access Service object.
 * @param params	Read parameters.
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_read(struct bt_has *has, struct bt_has_preset_read_params *params);

/** @brief Hearing Access Service callback structure. */
struct bt_has_cb {
	/**
	 * @brief Callback function for bt_has_discover.
	 *
	 * This callback is only used for the client.
	 *
	 * @param conn	Bluetooth connection object.
	 * @param has	Pointer to the Hearing Access Service object.
	 */
	void (*discover)(struct bt_conn *conn, struct bt_has *has, bt_has_hearing_aid_type_t type);

	/**
	 * @brief Callback function for Hearing Access Service active preset.
	 *
	 * Called when the value is locally read as the server.
	 * Called when the value is remotely read as the client.
	 * Called if the value is changed by either the server or client.
	 *
	 * @param has	Pointer to the Hearing Access Service object.
	 * @param err	0 in case of success or negative value in case of error.
	 * @param index	Active preset index.
	 */
	void (*active_preset)(struct bt_has *has, int err, uint8_t index);

	/**
	 * @brief Callback function for Hearing Access Service preset.
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
 * @brief Registers the callbacks used by the Hearing Access Service client.
 *
 * @param cb   The callback structure.
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_client_cb_register(struct bt_has_cb *cb);

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
