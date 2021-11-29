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

#include <sys/util.h>
#include <bluetooth/conn.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_BT_HAS)
#define BT_HAS_PRESET_CNT CONFIG_BT_HAS_PRESET_CNT
#else
#define BT_HAS_PRESET_CNT 0
#endif /* CONFIG_BT_HAS */

#define BT_HAS_PRESET_NAME_MIN 1
#define BT_HAS_PRESET_NAME_MAX 40

/** @brief Opaque Hearing Access Service object. */
struct bt_has;

/** Preset Properties values */
enum {
	/** No properties set */
	BT_HAS_PROP_NONE = 0,

	/** Preset name can be written by the client */
	BT_HAS_PROP_WRITABLE = BIT(0),

	/** Preset availability */
	BT_HAS_PROP_AVAILABLE = BIT(1),
};

/** @brief Register structure for preset. */
struct bt_has_preset_register_param {
	/** Preset id */
	uint8_t id;
	/** Preset properties */
	uint8_t properties;
	/** Preset name */
#if defined(CONFIG_BT_HAS_PRESET_NAME_DYNAMIC)
	char name[BT_HAS_PRESET_NAME_MAX];
#else
	const char *name;
#endif /* CONFIG_BT_HAS_PRESET_NAME_DYNAMIC */
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
	 * @param id	Preset id requested to activate,
	 * @param sync	Whether the server must relay this change to the
	 *		other member of the Binaural Hearing Aid Set.
	 *
	 * @return 0 in case of success or -EBUSY if operation cannot be
	 *	   executed at the time.
	 */
	int (*active_set)(struct bt_has *has, uint8_t id, bool sync);

	/**
	 * @brief Preset name changed callback
	 *
	 * Called if the preset name is changed by either the server or client.
	 *
	 * @param has	Pointer to the Hearing Access Service object.
	 * @param id	Preset id that name changed.
	 * @param name	Preset current name.
	 */
	void (*name_changed)(struct bt_has *has, uint8_t id, const char *name);
};

/** Register structure for Hearing Access Service. */
struct bt_has_register_param {
	/** Preset records with the initial parameters. */
	struct bt_has_preset_register_param preset_param[BT_HAS_PRESET_CNT];

	/** Preset operations structure. */
	struct bt_has_preset_ops *ops;
};

/**
 * @brief Register Hearing Access Service.
 *
 * @param      param 	Hearing Access Service register parameters.
 * @param[out] has	Pointer to the local Hearing Access Service object.
 * 			This will still be valid if the return value is
 * 			-EALREADY.
 *
 * @return 0 if success, errno on failure.
 */
int bt_has_register(struct bt_has_register_param *param, struct bt_has **has);

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
 * Get the ID of currently active preset.
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
 * Set the preset identified by the ID as the active preset.
 *
 * @param has	Pointer to the Hearing Access Service object.
 * @param id	Preset id.
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_active_set(struct bt_has *has, uint8_t id);

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
 * @param id			Preset ID.
 * @param visible               true if preset should be visible
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_visibility_set(struct bt_has *has, uint8_t id, bool visible);

/**
 * @brief Set availability of preset record. Available preset can be set by peer
 * device as active preset.
 *
 * @param has			Pointer to the Hearing Access Service object.
 * @param id			Preset ID.
 * @param available             true if preset should be available
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_availability_set(struct bt_has *has, uint8_t id, bool available);

/**
 * @brief Set Preset Name.
 *
 * Set the presetSS name identified by the ID.
 *
 * @param has	Pointer to the Hearing Access Service object.
 * @param id	Preset ID.
 * @param name	Preset name to be written.
 *
 * @return 0 in case of success or negative value in case of error.
 */
int bt_has_preset_name_set(struct bt_has *has, uint8_t id, const char *name);

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
 *   - id		Preset id.
 *   - properties	Preset properties.
 *   - name		Preset name.
 *
 *  @return BT_HAS_PRESET_READ_CONTINUE to continue read procedure.
 *  @return BT_HAS_PRESET_READ_STOP to stop read procedure.
 */
typedef uint8_t (*bt_has_preset_read_func_t)(struct bt_has *has, int err,
					     struct bt_has_preset_read_params *params, uint8_t id,
					     uint8_t properties, const char *name);

struct bt_has_preset_read_params {
	/** Read preset callback. */
	bt_has_preset_read_func_t func;
	/** If true id parameter is used.
	 *  If false by_count.start_id and by_count.preset_count are used.
	 */
	bool by_id;
	union {
		/** Preset id. */
		uint8_t id;
		struct {
			/** First requested ID number. */
			uint8_t start_id;
			/** Number of presets to read. */
			uint8_t preset_count;
		} by_count;
	};
};

/**
 * @brief Read Preset Record.
 *
 * Read the preset identified by the ID.
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

/** Hearing Aid device type */
typedef enum __packed {
	/** Binaural Hearing Aid. */
	BT_HAS_BINAURAL_HEARING_AID,

	/** Monaural Hearing Aid */
	BT_HAS_MONAURAL_HEARING_AID,

	/** Banded Hearing Aid */
	BT_HAS_BANDED_HEARING_AID,
} bt_has_hearing_aid_type_t;

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
	 * @param id	Active preset ID.
	 */
	void (*active_preset)(struct bt_has *has, int err, uint8_t id);

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
	void (*preset)(struct bt_has *has, int err, uint8_t id, uint8_t properties,
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
