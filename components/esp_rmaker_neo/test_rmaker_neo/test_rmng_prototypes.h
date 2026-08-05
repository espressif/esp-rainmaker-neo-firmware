/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_rmng_prototypes.h
 * @brief Prototypes for the RainMaker Neo test suite.
 */

#ifndef __TEST_RMNG_PROTOTYPES_H__
#define __TEST_RMNG_PROTOTYPES_H__

#include <string.h>
#include "sdkconfig.h"
#include "unity.h"

/* Node model *****************************************************************/

/**
 * @brief Test the basic functionality of the node model.
 */
void test_node_model_basic(void);

/** Path / ID validation tests. */
void test_device_create_rejects_path_separator(void);
void test_param_create_rejects_path_separator(void);
void test_path_round_trip(void);
void test_path_to_update_id_invalid(void);

/**
 * @brief Test the error paths of the node model.
 */
void test_node_model_error_paths(void);

/**
 * @brief Test the persistent parameters of the node model.
 */
void test_node_model_persistent_params(void);

/**
 * @brief Test that persistent parameters survive across node deinit and re-init.
 */
void test_node_model_persistent_params_across_deinit(void);

/**
 * @brief Test the basic functionality of the node config construction.
 */
void test_node_config_basic(void);

/**
 * @brief Test device add/remove and related error paths.
 */
void test_node_model_device_add_remove_and_errors(void);

/**
 * @brief Test node attributes and tags add/update/delete flows.
 */
void test_node_model_attributes_and_tags(void);

/**
 * @brief Test getters for info, devices, and lookup APIs.
 */
void test_node_model_getters_and_info(void);

/**
 * @brief Test getting node params with multiple data types.
 */
void test_node_model_get_node_params_multiple_data_types(void);

/**
 * @brief Test error paths for device model.
 */
void test_device_model_error_paths(void);

/**
 * @brief Test device delete guard, device attrs/callbacks/getters.
 */
void test_device_model_delete_guard(void);
void test_device_model_attrs_callbacks_getters(void);

/**
 * @brief Test param model getters.
 */
void test_param_model_getters(void);

/**
 * @brief Test param model create/bounds/update.
 */
void test_param_model_create_bounds_and_update(void);

/**
 * @brief Test that an equal-value param update skips the NVS write-back but a changed value writes it.
 */
void test_param_model_same_value_update_skips_nvs(void);

/**
 * @brief Test error paths for param model store/get/invalid parent.
 */
void test_param_model_store_get_invalid_parent(void);

/**
 * @brief Test error paths for param model.
 */
void test_param_model_error_paths(void);

/**
 * @brief Test param model handle set payload.
 */
void test_param_model_handle_set_payload(void);

/**
 * @brief Test param model handle set payload with device-type keyed (group control) payloads.
 */
void test_param_model_handle_set_payload_typed(void);

/* Timeseries *******************************************************************/

/**
 * @brief Test timeseries initialization and deinitialization.
 */
void test_timeseries_init_deinit(void);

/**
 * @brief Test basic timeseries data push functionality.
 */
void test_timeseries_push_data_basic(void);

/**
 * @brief Test timeseries data validation (reject object/array types).
 */
void test_timeseries_push_data_validation(void);

/**
 * @brief Test basic timeseries get payload functionality.
 */
void test_timeseries_get_payload_basic(void);

/**
 * @brief Test timeseries get payload with multiple items.
 */
void test_timeseries_get_payload_multiple(void);

/**
 * @brief Test timeseries get payload with max items limit.
 */
void test_timeseries_get_payload_max_items(void);

/**
 * @brief Test timeseries access when not initialized.
 */
void test_timeseries_uninitialized_access(void);

/**
 * @brief Test timeseries with different data types.
 */
void test_timeseries_data_types(void);

/**
 * @brief Test timeseries error paths and edge cases.
 */
void test_timeseries_error_paths(void);

/**
 * @brief Test timeseries queue behavior (FIFO order).
 */
void test_timeseries_queue_behavior(void);

/* Protobuf helpers ************************************************************/

/**
 * @brief Test protobuf packing/unpacking helpers.
 */
/* Local control endpoint protocol (frozen wire contract) */
void test_local_ctrl_get_params_fragment_walk(void);
void test_local_ctrl_get_config_fragment_walk(void);
void test_local_ctrl_get_params_offset_without_reset_fails(void);
void test_local_ctrl_get_params_offset_beyond_total_fails(void);
void test_local_ctrl_get_data_type_mismatch_fails(void);
void test_local_ctrl_free_data_invalidates_cache(void);
void test_local_ctrl_get_params_malformed_frame_rejected(void);
void test_local_ctrl_set_params_invalid_json_keeps_session(void);
void test_local_ctrl_set_params_valid_json_succeeds(void);

/* Local control SEC2 salt/verifier caching (security 2 builds only) */
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2
void test_local_ctrl_sec2_custom_pop_does_not_poison_cache(void);
void test_local_ctrl_sec2_generated_pop_is_cached(void);
void test_local_ctrl_sec2_resolve_rejects_invalid_args(void);
#endif

void test_pb_c_cmd_challenge_roundtrip(void);
void test_pb_c_resp_pack_to_buffer(void);

/* Assisted claiming ***********************************************************/

/**
 * @brief Test the claiming fragment bounds, including the 32-bit offset wrap.
 */
void test_claim_validate_data_bounds(void);

/**
 * @brief Test escaping newlines in the CSR for transport inside a JSON string.
 */
void test_claim_escape_new_line(void);

/**
 * @brief Test unescaping newlines, including a lone trailing backslash.
 */
void test_claim_unescape_new_line(void);

/**
 * @brief Test the claiming command guards, including the terminal state.
 */
void test_claim_command_guards(void);

/**
 * @brief Test claim fragment sequencing: open at zero, then contiguous only.
 */
void test_claim_fragment_sequencing(void);

/* Utilities - checksum ******************************************************/

/**
 * @brief Checksum init/deinit basic.
 */
void test_checksum_basic(void);

/**
 * @brief Compare and store flows.
 */
void test_checksum_compare_store(void);
void test_checksum_compare_invalid_key(void);
void test_checksum_store_invalid_key(void);

/* Utilities - trigger codec *****************************************************/
void test_trigger_codec_value_bool(void);
void test_trigger_codec_value_int(void);
void test_trigger_codec_value_float(void);
void test_trigger_codec_value_string(void);
void test_trigger_codec_value_object(void);
void test_trigger_codec_value_array(void);
void test_trigger_codec_preserves_fields(void);
void test_trigger_codec_enabled_defaults_true_when_omitted(void);
void test_trigger_codec_multiple_and_operators(void);
void test_trigger_codec_empty_array(void);
void test_trigger_codec_is_smaller_than_json(void);
void test_trigger_codec_encode_rejects_unknown_operator(void);
void test_trigger_codec_encode_rejects_missing_path(void);
void test_trigger_codec_encode_rejects_null_value(void);
void test_trigger_codec_iter_rejects_wrong_version(void);
void test_trigger_codec_iter_rejects_truncated(void);
void test_trigger_codec_invalid_args(void);

/* MQTT topics ********************************************************************/

/**
 * @brief Test basic MQTT topic generation and append accepted.
 */
void test_mqtt_topics_basic(void);

/**
 * @brief Test error paths for MQTT topic generation.
 */
void test_mqtt_topics_errors(void);

/**
 * @brief Test group control MQTT topic generation.
 */
void test_mqtt_topics_group_control(void);

/**
 * @brief Test inbound group control topic parser (subgroup extraction).
 */
void test_mqtt_topics_parse_group_control_subgroup(void);

/* Event loop *******************************************************************/

/**
 * @brief Event loop init and handler registration flows.
 */
void test_event_loop_init_and_handlers(void);

/* Network common ****************************************************************/

/**
 * @brief Test network common init/deinit and payload operations.
 */
void test_network_common_init_deinit(void);
void test_network_common_payload(void);
void test_network_common_payload_zero_length(void);

/* Cloud events ******************************************************************/

/**
 * @brief Tests for the 'getTimeSync' cloud event.
 */
void test_cloud_event_timesync_builder(void);
void test_cloud_event_timesync_builder_registered(void);
void test_cloud_event_timesync_response_sets_processed_bit(void);
void test_cloud_event_timesync_response_ignores_invalid_time(void);

/* Network notify ****************************************************************/

/**
 * @brief Test notification manager init/deinit.
 */
void test_notify_init_deinit(void);

/* Schedules *********************************************************************/

/**
 * @brief Test schedule JSON parsing - action, triggers, validity.
 */
void test_schedules_parse_action_valid_light_power(void);
void test_schedules_parse_action_valid_nested_object(void);
void test_schedules_parse_action_missing_returns_null(void);
void test_schedules_parse_action_empty_object_returns_empty(void);
void test_schedules_parse_action_free_null_safe(void);
void test_schedules_details_disabled_schedule_skipped(void);
void test_schedules_details_mixed_enabled_disabled(void);
void test_schedules_details_all_no_id_count_zero(void);
void test_schedules_details_clear_after_skip_no_crash(void);
void test_schedules_details_reparse_after_skip_no_crash(void);
void test_schedules_details_empty_id_rejected(void);
void test_schedules_details_oversized_id_rejected(void);
void test_schedules_details_numeric_id_rejected(void);
void test_schedules_details_null_id_rejected(void);
void test_schedules_details_object_id_rejected(void);
void test_schedules_details_max_length_id_accepted(void);
void test_schedules_details_name_field_ignored(void);
void test_schedules_parse_trigger_relative(void);
void test_schedule_port_release_bumps_generation(void);
void test_schedule_port_lookup_rejects_released_slot(void);
void test_schedule_port_lookup_rejects_out_of_range_index(void);
void test_schedule_port_stale_dispatch_does_not_fire_recycled_slot(void);
void test_schedule_port_pool_recycles_slots(void);
void test_schedule_port_pool_grows_then_reuses(void);
void test_schedules_parse_trigger_days_of_week(void);
void test_schedules_parse_trigger_date(void);
void test_schedules_parse_trigger_date_no_month_mask_recurs_monthly(void);
void test_schedules_parse_trigger_date_arm_beats_weekday_arm(void);
void test_schedules_parse_trigger_months_without_day_ignored(void);
void test_schedules_parse_trigger_minutes_out_of_range_fails(void);
void test_schedules_parse_trigger_empty_array_fails(void);
void test_schedules_parse_trigger_missing_fails(void);
void test_schedules_parse_trigger_extra_entries_dropped(void);
void test_schedules_parse_trigger_days_missing_minutes_fails(void);
void test_schedules_parse_trigger_invalid_first_entry_fails(void);
void test_schedules_parse_trigger_invalid_type_fails(void);
void test_schedules_details_count_clamped_to_max(void);
void test_schedules_update_details_null_input_safe(void);
void test_schedules_parse_validity_full(void);
void test_schedules_parse_validity_missing_returns_ok(void);
void test_schedules_parse_validity_empty_object(void);
void test_schedules_parse_validity_large_timestamps(void);
void test_schedules_parse_validity_bogus_keys_only(void);
void test_schedules_parse_validity_only_start(void);
void test_schedules_parse_validity_only_end(void);
void test_schedules_parse_validity_wrong_types(void);
void test_schedules_parse_validity_negative_values(void);
void test_schedules_parse_validity_not_an_object(void);
void test_schedules_parse_action_key_order_independent(void);
void test_schedules_parse_action_whitespace_tolerant(void);
void test_schedules_parse_full_schedule_object(void);
void test_schedules_serialize_round_trip_days_of_week(void);
void test_schedules_serialize_round_trip_date_recurring(void);
void test_schedules_serialize_round_trip_date_year_bounded(void);
void test_schedules_serialize_relative_emits_computed_ts(void);
void test_schedules_serialize_empty_node_is_empty_array(void);
void test_schedules_serialize_round_trip_preserves_count(void);
void test_schedules_arm_records_next_fire_for_date_trigger(void);
void test_schedules_arm_drops_date_trigger_with_past_year(void);
void test_schedules_serialize_emits_ts_for_date_one_shot(void);
void test_schedules_serialize_omits_ts_for_repeating(void);
void test_schedules_replay_skips_one_shot_already_due(void);
void test_schedules_replay_keeps_one_shot_still_pending(void);
void test_schedules_arm_drops_year_bounded_repeating_after_expiry(void);
void test_schedules_arm_keeps_year_bounded_repeating_still_valid(void);
void test_schedules_fired_one_shot_removed_from_node_and_payload(void);
void test_schedules_remove_by_cloud_id_removes_one(void);
void test_schedules_remove_by_cloud_id_unknown_is_noop(void);
void test_schedules_remove_by_cloud_id_last_one_frees_array(void);
void test_schedules_remove_by_cloud_id_null_safe(void);
void test_schedules_one_shot_flagged_in_priv_data(void);
void test_schedules_reload_no_stored_details_is_noop(void);
void test_schedules_reload_null_node_rejected(void);
void test_schedules_drop_node_releases_handles(void);
void test_schedules_erase_node_releases_handles(void);
void test_schedules_drop_and_erase_null_node_safe(void);
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#endif

/* Automation *********************************************************************/

/**
 * @brief Test automation trigger-details JSON parsing (static helpers via direct include).
 */
void test_automation_parse_details_null_invalid_arg(void);
void test_automation_parse_details_empty_invalid_arg(void);
void test_automation_parse_details_invalid_json_fail(void);
void test_automation_parse_details_whitespace_empty_array(void);
void test_automation_parse_details_whitespace_invalid_triggers(void);
void test_automation_parse_details_all_invalid_frees_list(void);
void test_automation_parse_details_valid_format(void);
void test_automation_parse_details_exceeds_maximum(void);
void test_automation_parse_details_boolean_non_eq_ne_operators_rejected(void);
void test_automation_parse_details_partial_failure_rolls_back(void);
void test_automation_parse_details_enabled_false_single_no_triggers(void);
void test_automation_parse_details_enabled_omitted_adds_trigger(void);
void test_automation_parse_details_enabled_explicit_true_and_false_skips(void);

/**
 * @brief Test notification send with invalid arguments.
 */
void test_notify_send_invalid_args(void);
void test_notify_send_payload_generator_fail(void);

/**
 * @brief Test push notification routing via the state update ID.
 */
void test_notify_send_push_no_node_rejected(void);
void test_notify_send_push_routes_by_update_id(void);

/**
 * @brief Test that esp_rmaker_param_update_and_notify() publishes a push, best-effort.
 */
void test_notify_param_update_and_notify_publishes_push(void);

/* Local config *******************************************************************/

/**
 * @brief Test local config group string formatting and helpers.
 */
void test_local_config_group_info_format(void);
void test_local_config_group_info_empty(void);
void test_local_config_group_info_format_skips_empty_subgroups(void);
void test_local_config_group_info_parse_packs_subgroups(void);
void test_local_config_group_info_set_get(void);
void test_local_config_other_accessors(void);
void test_local_config_group_info_invalid_args(void);
void test_local_config_group_info_null_subgroups(void);
void test_local_config_alexa_flag_default(void);
void test_local_config_sched_trigger_details_roundtrip(void);
void test_local_config_parse_group_info_primary_only(void);
void test_local_config_parse_group_info_with_subgroups(void);
void test_local_config_parse_group_info_empty_and_null(void);
void test_local_config_parse_group_info_invalid_args(void);
void test_local_config_parse_group_info_roundtrip(void);
void test_local_config_parse_group_info_max_subgroups(void);
void test_local_config_parse_group_info_null_subgroups(void);
void test_local_config_mqtt_params_missing(void);

/* State changes ******************************************************************/

/**
 * @brief Test init/lock/unlock/deinit basic flow.
 */
void test_state_changes_lock_unlock(void);
void test_state_changes_concurrent_mark_and_drain(void);

/* get_all ctx filtering */
void test_get_all_rejects_null_ctx(void);
void test_get_all_self_ctx_returns_self_params_only(void);
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
void test_get_all_filters_by_child_ctx(void);
void test_get_all_unknown_ctx_returns_zero(void);
#endif

/* Subgroup-aware group control dispatch filter */
void test_node_is_in_subgroup_rejects_invalid_args(void);
void test_node_is_in_subgroup_self_membership(void);
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
void test_node_is_in_subgroup_bridge_child_membership(void);
#endif

/* Core ***************************************************************************/

/**
 * @brief Test pre-provisioning init/deinit success and error paths.
 */
void test_core_pre_prov_init_success(void);
void test_core_pre_prov_deinit(void);

/**
 * @brief Test node init/register/deinit state machine.
 */
void test_core_node_init_null_config(void);
void test_core_node_init_success(void);
void test_core_node_deinit_states(void);

/**
 * @brief Test start/stop success and error paths.
 */
void test_core_start_without_init(void);
void test_core_start_stop(void);
void test_core_stop_without_start(void);

/**
 * @brief Test online status subscription conditions.
 */
void test_core_online_status_conditions(void);

/**
 * @brief Test sign challenge arg validation and error paths.
 */
void test_core_sign_challenge_invalid_args(void);

/**
 * @brief Test core error paths (start/stop/init).
 */
void test_core_error_paths(void);

/* Bridge *********************************************************************/

void test_bridge_topic_group_control_subgroup_wildcard(void);
void test_bridge_topic_bridges_to_cloud(void);
void test_bridge_topic_child_shadow_updates(void);
void test_bridge_suffix_validator(void);
void test_bridge_parse_child_from_from_cloud_topic(void);
void test_bridge_parse_child_and_shadow_from_params_topic(void);
void test_bridge_public_api_arg_validation(void);
void test_bridge_child_nvs_load_absent_returns_not_found(void);
void test_bridge_child_nvs_store_load_roundtrip(void);
void test_bridge_child_nvs_set_sched_ver_preserves_other_fields(void);
void test_bridge_child_nvs_set_sched_ver_creates_when_absent(void);
void test_bridge_child_nvs_set_node_config_writes_both(void);
void test_bridge_child_nvs_erase(void);
void test_bridge_child_nvs_invalidates_on_version_mismatch(void);
void test_bridge_child_nvs_invalidates_on_size_mismatch(void);
void test_bridge_child_nvs_long_local_id_no_truncation_collision(void);
void test_bridge_child_nvs_compute_key(void);
void test_bridge_child_nvs_invalid_arg(void);
void test_bridge_accessors_local_id(void);
void test_bridge_accessors_topic_ctx(void);
void test_bridge_accessors_child_from_ctx_roundtrip(void);
void test_bridge_accessors_child_from_ctx_after_invalidate(void);
void test_bridge_accessors_version_progress(void);
void test_bridge_accessors_find_by_thing_name_and_local_id(void);
void test_bridge_accessors_slot_pool_exhaustion(void);
void test_bridge_accessors_seed_child_arg_validation(void);
void test_bridge_parse_child_from_from_cloud_topic_edge_cases(void);
void test_bridge_parse_child_and_shadow_from_params_topic_edge_cases(void);
void test_bridge_suffix_validator_extended(void);

/* Node config pending ********************************************************/

void test_node_config_pending_init_idempotent(void);
void test_node_config_pending_add_rejects_null_ctx(void);
void test_node_config_pending_add_idempotent(void);
void test_node_config_pending_remove_self_and_readd(void);
void test_node_config_pending_remove_unknown_noop(void);
void test_node_config_pending_clear_inflight_unknown_noop(void);
void test_node_config_pending_clear_all_inflight_safe(void);

/* All tests ******************************************************************/

int test_rmng_all_tests_unity(void);

#endif /* __TEST_RMNG_PROTOTYPES_H__ */
