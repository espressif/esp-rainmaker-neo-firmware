/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_rmaker_neo_prototypes.h
 * @brief Prototypes for the RainMaker Neo test suite.
 */

#ifndef __TEST_RMAKER_NEO_PROTOTYPES_H__
#define __TEST_RMAKER_NEO_PROTOTYPES_H__

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
void test_device_create_rejects_long_id(void);
void test_param_create_rejects_long_id(void);
void test_path_round_trip(void);
void test_path_to_update_id_invalid(void);

/** UTF-8 validation tests. */
void test_utf8_check_cases(void);
void test_utf8_param_create(void);
void test_utf8_param_update(void);
void test_utf8_param_valid_values(void);
void test_utf8_device_attribute(void);
void test_utf8_node_attribute_and_tag(void);
void test_utf8_node_info(void);
void test_utf8_restore_invalid_stored_value(void);
void test_utf8_restore_valid_stored_value(void);
void test_utf8_cloud_write_keeps_reports_working(void);

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
 * @brief Test param unit and valid_values in the node config.
 */
void test_node_config_param_metadata(void);

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
 * @brief Test data model introspection: device/param enumeration.
 */
void test_introspect_enumeration(void);

/**
 * @brief Test data model introspection: param metadata getters.
 */
void test_introspect_metadata_getters(void);

/**
 * @brief Test the param update observer registration and notification.
 */
void test_introspect_param_update_observer(void);

/**
 * @brief Test programmatic param write injection to device callbacks.
 */
void test_introspect_device_write_params(void);

/* Factory reset participants **************************************************/

/**
 * @brief Test factory reset participant register/unregister argument validation.
 */
void test_factory_reset_participant_register_arg_validation(void);

/**
 * @brief Test that only one participant may claim reboots_on_wipe.
 */
void test_factory_reset_participant_register_rejects_second_rebooting(void);

/**
 * @brief Test that the participant table rejects registrations past its capacity.
 */
void test_factory_reset_participant_register_table_full(void);

/**
 * @brief Test that re-registering the same wipe updates the entry instead of adding one.
 */
void test_factory_reset_participant_reregister_updates_in_place(void);

/**
 * @brief Test that participants are wiped in registration order.
 */
void test_factory_reset_participants_wiped_in_registration_order(void);

/**
 * @brief Test that a reboots_on_wipe participant is wiped after all the others.
 */
void test_factory_reset_participants_rebooting_wiped_last(void);

/**
 * @brief Test that a failing participant wipe does not stop the remaining ones.
 */
void test_factory_reset_participants_wipe_failure_does_not_stop_others(void);

/**
 * @brief Test that a participant-originated reset skips only the originator's wipe.
 */
void test_factory_reset_from_participant_skips_only_the_originator(void);

/**
 * @brief Test that a participant-originated reset with no originator wipes every participant.
 */
void test_factory_reset_from_participant_null_self_wipes_all(void);

/**
 * @brief Test that a reset requested from inside a participant's wipe is a no-op.
 */
void test_factory_reset_reentrant_request_is_a_noop(void);

/**
 * @brief Test that unregistering a participant preserves the remaining wipe order.
 */
void test_factory_reset_participants_unregister_preserves_order(void);

/**
 * @brief Test that a factory reset that did not reboot leaves the node resettable again.
 */
void test_factory_reset_runs_again_after_a_run_without_reboot(void);

/**
 * @brief Test that the extra wipe runs after the data clear and before the participants.
 */
void test_factory_reset_with_wipe_runs_extra_before_participants(void);

/**
 * @brief Test that a nested reset with an extra wipe does nothing, extra wipe included.
 */
void test_factory_reset_with_wipe_nested_skips_extra(void);

/**
 * @brief Test that a delayed factory reset starts the worker and wipes participants after the delay.
 */
void test_factory_reset_delayed_runs_participants_after_delay(void);

/**
 * @brief Test that a repeated delayed reset replaces the pending one instead of running twice.
 */
void test_system_ctrl_delayed_reset_repeat_replaces_pending(void);

/**
 * @brief Test that delayed resets of different types wait out their delays side by side.
 */
void test_system_ctrl_delayed_resets_of_different_types_do_not_stack(void);

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

/**
 * @brief Test int64 params: node config, reports, cloud writes, bounds and persistence.
 */
void test_param_model_int64(void);

/**
 * @brief Test that only int64 params store a full-size value blob, and that an old-size blob still loads.
 */
void test_param_model_stored_blob_len(void);

/**
 * @brief Test that a stored int widens to int64, and that any other stored type mismatch keeps the default.
 */
void test_param_model_stored_type_mismatch(void);

/**
 * @brief Test the int64 value constructor, compare and copy.
 */
void test_val_int64(void);

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
void test_timeseries_failed_publish_keeps_queue_order(void);
void test_timeseries_bad_entry_is_dropped(void);

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
void test_local_ctrl_custom_endpoint_requires_running_service(void);
void test_local_ctrl_custom_endpoint_add_remove(void);
void test_local_ctrl_custom_endpoint_table_full(void);
void test_local_ctrl_custom_endpoint_removed_on_disable(void);
void test_local_ctrl_custom_endpoint_removed_while_chal_resp_runs(void);

/**
 * @brief Test that a get request without a node answers Fail rather than NoMemory.
 */
void test_local_ctrl_get_params_without_node_fails(void);

/* Filtered get_params (CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_GET_FILTER) */
void test_local_ctrl_get_params_filter_selects_params(void);
void test_local_ctrl_get_params_filter_skips_unknown_and_duplicates(void);
void test_local_ctrl_get_params_filter_malformed_rejected(void);

/* Fragmented set_params (CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SET_FRAG) */
void test_local_ctrl_set_frag_applies_json(void);
void test_local_ctrl_set_frag_bad_fragment_discards_transfer(void);
void test_local_ctrl_set_frag_session_close_frees_transfer(void);
void test_local_ctrl_set_frag_bad_json_fails(void);
void test_local_ctrl_set_frag_malformed_request(void);

/**
 * @brief Test that the local control endpoints advertise `local_ctrl` and, if enabled, `set_frag`.
 */
void test_prov_local_ctrl_advertises_capabilities(void);

/**
 * @brief Test that the end of a provisioning session frees a half-served get-data cache.
 */
void test_prov_local_ctrl_prov_end_frees_cache(void);

/**
 * @brief Test that deinit removes the three endpoints and their capability.
 */
void test_prov_local_ctrl_deinit_removes_endpoints(void);

/* Local control SEC2 salt/verifier caching (security 2 builds only) */
#if CONFIG_ESP_RMAKER_NEO_LOCAL_CTRL_SECURITY_2
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
 * @brief Test the claiming command guards, including the terminal state.
 */
void test_claim_command_guards(void);

/**
 * @brief Test the claim-verify request body: exact JSON, buffer bounds and argument checks.
 */
void test_claim_verify_request(void);

/**
 * @brief Test claim fragment sequencing: open at zero, then contiguous only.
 */
void test_claim_fragment_sequencing(void);

/* MQTT host migration ***********************************************************/

/**
 * @brief Test that the migration check rejects a NULL or empty target host.
 */
void test_migration_rejects_empty_target(void);

/**
 * @brief Test that a stored host equal to the target needs no migration.
 */
void test_migration_same_host_does_nothing(void);

/**
 * @brief Test that a new target host is used in memory and not written.
 */
void test_migration_new_host_in_memory_only(void);

/**
 * @brief Test that the migration takes an app MQTT host provider as the stored host.
 */
void test_migration_app_override_is_stored_host(void);

/**
 * @brief Test that a stored node ID needs no write-back.
 */
void test_migration_stored_node_id_needs_no_write(void);

/**
 * @brief Test that a node ID from the certificate CN needs a write-back.
 */
void test_migration_cert_cn_needs_write(void);

/**
 * @brief Test that a node with no node ID and no certificate fails the migration check.
 */
void test_migration_no_node_id_no_cert_fails(void);

/**
 * @brief Test that a valid image writes back the CN node ID and the host on the first connection.
 */
void test_migration_flow_valid_connect_writes_back(void);

/**
 * @brief Test that a valid image with no connection posts the timeout event and writes nothing.
 */
void test_migration_flow_valid_no_connect_times_out(void);

/**
 * @brief Test that the first connection cancels the target host timeout.
 */
void test_migration_flow_connect_cancels_timeout(void);

/**
 * @brief Test that a reboot before the first connection runs the migration again.
 */
void test_migration_flow_reboot_before_connect_retries(void);

#ifndef ESP_PLATFORM
/* POSIX only: the fixtures stage a pending verify image in the POSIX OTA config. */

/**
 * @brief Test that a pending image writes back only after the connection and the mark-valid.
 */
void test_migration_flow_pending_connect_writes_back(void);

/**
 * @brief Test that a pending image with no connection rolls back with no timeout event and no write.
 */
void test_migration_flow_pending_no_connect_rolls_back(void);

/**
 * @brief Test that an image marked valid after the start times out with no rollback.
 */
void test_migration_flow_valid_after_start_no_connect_times_out(void);

/**
 * @brief Test that an image marked valid without the OSAL call writes back on the connection.
 */
void test_migration_flow_raw_mark_valid_connect_writes_back(void);

/**
 * @brief Test that a power loss after the mark-valid and before the write-back runs the migration again.
 */
void test_migration_flow_power_loss_after_mark_valid_retries(void);
#endif /* ESP_PLATFORM */

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
void test_trigger_codec_value_int64(void);
void test_trigger_codec_int_blob_unchanged(void);
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
void test_trigger_codec_iter_rejects_truncated_int64(void);
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
void test_cloud_event_server_config_builder(void);
void test_cloud_event_server_config_build_nested(void);
void test_cloud_event_server_config_build_buffer_size(void);
void test_cloud_event_server_config_build_invalid_args(void);
void test_cloud_event_server_config_parse_nested(void);
void test_cloud_event_server_config_parse_partial(void);
void test_cloud_event_server_config_parse_too_long(void);
void test_cloud_event_server_config_value_limits(void);
void test_cloud_event_server_config_ignores_unrequested(void);
void test_cloud_event_server_config_missing_key(void);
void test_cloud_event_server_config_non_string_value(void);
void test_cloud_event_server_config_waits_for_response(void);
void test_cloud_event_server_config_error_reply_not_received(void);
void test_cloud_event_server_config_response_is_cached(void);
void test_cloud_event_server_config_missing_rmng_base(void);
void test_cloud_event_server_config_invalid_args(void);
void test_cloud_event_server_config_deinit_keeps_readers_safe(void);
void test_cloud_event_server_config_stop_wakes_reader(void);

/* Cloud manager inbox */
void test_cloud_manager_failed_send_releases_context(void);
void test_cloud_manager_newer_send_supersedes_context(void);
void test_cloud_manager_response_during_failed_send(void);
void test_cloud_manager_response_without_status_reports_failure(void);

/* Network notify ****************************************************************/

/**
 * @brief Test notification manager init/deinit.
 */
void test_notify_init_deinit(void);

/* Host control ******************************************************************/

/**
 * @brief Test the correlation id on host_ctrl commands and responses.
 */
void test_host_ctrl_seq_response_untagged(void);
void test_host_ctrl_seq_response_echoes_id(void);
void test_host_ctrl_seq_take_strips_id(void);
void test_host_ctrl_seq_take_leaves_untagged_command(void);
void test_host_ctrl_seq_take_ignores_id_without_command(void);

/**
 * @brief Test the host_ctrl int64 value codec.
 */
void test_host_ctrl_param_value_int64(void);

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
void test_schedule_port_cancel_waits_for_running_callback(void);
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
#ifdef CONFIG_ESP_RMAKER_NEO_BRIDGE_ENABLE
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

/**
 * @brief Test that int64 and float params accept a trigger value above 32 bits, and an int param rejects it.
 */
void test_automation_parse_details_int64(void);
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

/* Time service Timestamp ********************************************************/

/**
 * @brief Test that the Timestamp param starts at 0 and rejects invalid writes.
 */
void test_time_timestamp_param_rejects_invalid(void);

/**
 * @brief Test that a Timestamp write does not step a clock that SNTP owns.
 */
void test_time_timestamp_param_ignored_when_synced(void);

/**
 * @brief Test that time sync with no SNTP sync yet does not block a Timestamp write.
 */
void test_time_timestamp_param_applies_before_sntp_sync(void);

/* Local config *******************************************************************/

/**
 * @brief Test local config group string formatting and helpers.
 */
void test_local_config_group_info_format(void);
void test_local_config_group_info_empty(void);
void test_local_config_group_info_format_skips_empty_subgroups(void);
void test_local_config_group_info_parse_packs_subgroups(void);
void test_local_config_group_info_set_get(void);
void test_local_config_stored_group_getters(void);
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
 * @brief Test the init/deinit basic flow.
 */
void test_state_changes_init_deinit(void);
void test_state_changes_concurrent_mark_and_drain(void);
void test_state_update_id_compare_orders_by_address(void);

/**
 * @brief Test that a report success keeps a timer for an update that arrived during the run.
 */
void test_state_report_success_rearms_late_update(void);

/* get_all ctx filtering */
void test_get_all_rejects_null_ctx(void);
void test_get_all_self_ctx_returns_self_params_only(void);
#ifdef CONFIG_ESP_RMAKER_NEO_BRIDGE_ENABLE
void test_get_all_filters_by_child_ctx(void);
void test_get_all_unknown_ctx_returns_zero(void);
#endif

/* Subgroup-aware group control dispatch filter */
void test_node_is_in_subgroup_rejects_invalid_args(void);
void test_node_is_in_subgroup_self_membership(void);
#ifdef CONFIG_ESP_RMAKER_NEO_BRIDGE_ENABLE
void test_node_is_in_subgroup_bridge_child_membership(void);
#endif

/* Core ***************************************************************************/

/**
 * @brief Test what node init does for provisioning: succeeds unclaimed, registers ch_resp,
 *        leaves MQTT init to the start task.
 */
void test_core_node_init_unclaimed_succeeds(void);
void test_core_node_init_registers_chal_resp(void);
void test_core_node_init_defers_mqtt_init(void);

/**
 * @brief Test that local endpoints enabled before start wait for the start task.
 */
void test_local_endpoints_enable_before_start_defers(void);

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
void test_bridge_accessors_get_node_id_for_node(void);
void test_bridge_accessors_child_from_ctx_roundtrip(void);
void test_bridge_accessors_child_from_ctx_after_invalidate(void);
void test_bridge_accessors_version_progress(void);
void test_bridge_accessors_find_by_thing_name_and_local_id(void);
void test_bridge_accessors_slot_pool_exhaustion(void);
void test_bridge_accessors_seed_child_arg_validation(void);
void test_bridge_accessors_before_init(void);
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

#ifdef CONFIG_ESP_RMAKER_NEO_CONTROLLER

/* Group controller ***********************************************************/

/** Base-URL split: the stage path is what every data-API path is built on. */
void test_controller_url_splits_host_and_stage_path(void);
void test_controller_url_without_path_yields_empty_base_path(void);
void test_controller_url_strips_one_trailing_slash(void);
void test_controller_url_without_scheme_is_tolerated(void);
void test_controller_url_truncates_an_oversized_host(void);
void test_controller_url_null_yields_empty_outputs(void);

/** Shadow names and topics: a wrong one is accepted by the broker and simply never arrives. */
void test_controller_shadow_name_plain_group(void);
void test_controller_shadow_name_joins_subgroups_with_a_dash(void);
void test_controller_shadow_name_sorts_the_subgroup_path(void);
void test_controller_shadow_name_ignores_a_duplicate_subgroup(void);
void test_controller_topic_shadow_get(void);
void test_controller_topic_node_params(void);
void test_controller_topic_filters_use_a_single_level_wildcard(void);
void test_controller_topic_group_control_without_subgroup(void);
void test_controller_topic_group_control_with_subgroup(void);
void test_controller_topic_rejects_an_undersized_buffer(void);
void test_controller_thing_name_extracted_from_shadow_topic(void);
void test_controller_thing_name_honours_topic_len_without_nul(void);
void test_controller_thing_name_rejects_a_foreign_prefix(void);
void test_controller_thing_name_rejects_a_missing_separator(void);
void test_controller_thing_name_rejects_an_empty_name(void);

/** Shadow bookkeeping: which filters a sync must drop after the group moved. */
void test_controller_shadow_collect_reports_each_name_once(void);
void test_controller_shadow_collect_honours_the_buffer_bound(void);
void test_controller_shadow_collect_reports_none_for_an_empty_list(void);
void test_controller_shadow_in_use_finds_a_name_the_list_holds(void);
void test_controller_shadow_in_use_rejects_a_name_the_migration_dropped(void);

/** Expired credentials: a 403 that asks for fresh credentials, not a replay. */
void test_controller_expired_credentials_detected_in_a_403_body(void);
void test_controller_expired_credentials_not_reported_for_a_denial(void);
void test_controller_expired_credentials_honours_body_len(void);

/** GroupInfo: the node list and every node's subgroup path. */
void test_controller_group_info_reports_every_node(void);
void test_controller_group_info_reports_the_group_name(void);
void test_controller_group_info_tolerates_a_missing_group_name(void);
void test_controller_group_info_reports_subgroups_and_their_members(void);
void test_controller_group_info_tolerates_a_missing_subgroup_name(void);
void test_controller_group_info_stops_when_a_callback_stops(void);
void test_controller_group_info_reports_an_empty_group(void);
void test_controller_group_info_rejects_a_body_without_node_ids(void);

/** Node config: the device and param tree, and the in-place update. */
void test_controller_config_parses_devices_and_params(void);
void test_controller_config_parses_bounds_on_a_numeric_param(void);
void test_controller_config_parses_int64_bounds(void);
void test_controller_config_parses_services_into_the_same_list(void);
void test_controller_config_accepts_name_as_a_legacy_alias_for_id(void);
void test_controller_config_parses_the_ui_type_and_the_primary_param(void);
void test_controller_config_primary_param_reports_none_once_it_is_dropped(void);
void test_controller_config_reports_the_parents_of_a_param(void);
void test_controller_config_maps_every_ui_type(void);
void test_controller_config_maps_every_data_type(void);
void test_controller_config_reads_the_data_model(void);
void test_controller_config_update_keeps_surviving_handles_valid(void);
void test_controller_config_update_makes_a_dropped_device_stale(void);

/** Reported shadow state: the full seed and the delta merge. */
void test_controller_state_applies_reported_params(void);
void test_controller_state_reports_not_found_before_any_value(void);
void test_controller_state_delta_keeps_params_it_does_not_mention(void);
void test_controller_state_reports_no_change_when_the_value_is_the_same(void);
void test_controller_state_ignores_the_desired_plane(void);
void test_controller_state_rejects_a_body_without_reported(void);
void test_controller_state_ncfg_ver_change_marks_the_config_stale(void);
void test_controller_state_config_is_current_until_ncfg_ver_moves(void);
void test_controller_state_reports_an_online_transition(void);
void test_controller_state_ncfg_ver_accepts_an_integer_token(void);
void test_controller_state_tracks_the_online_flag(void);
void test_controller_state_reports_every_changed_param(void);

/** Write payloads: node writes key by name, group writes by type. */
void test_controller_write_node_payload_is_keyed_by_id(void);
void test_controller_write_refuses_a_read_only_param(void);
void test_controller_write_batch_groups_params_by_device(void);
void test_controller_write_node_batch_refuses_to_truncate(void);
void test_controller_write_batch_rejects_a_read_only_param(void);
void test_controller_write_batch_needs_a_begin(void);
void test_controller_write_group_payload_is_keyed_by_type(void);
void test_controller_write_group_payload_reaches_a_service_type(void);
void test_controller_write_group_payload_rejects_an_empty_type(void);
void test_controller_write_group_batch_holds_every_device_type(void);
void test_controller_write_group_batch_shares_one_envelope_per_type(void);
void test_controller_write_group_batch_needs_a_begin(void);
void test_controller_write_group_batch_refuses_to_truncate(void);
void test_controller_write_group_payload_refuses_invalid_utf8(void);
void test_controller_write_group_payload_refuses_to_truncate(void);

/** Handle lifetime: a survivor keeps its handle, and a dropped object goes stale. */
void test_controller_handle_resolves_only_its_own_kind_and_generation(void);
void test_controller_handle_of_a_reused_slot_does_not_match_the_old_handle(void);
void test_controller_handle_node_survives_a_resync_that_keeps_it(void);
void test_controller_handle_node_goes_stale_when_a_resync_drops_it(void);
void test_controller_handle_subgroup_survives_a_resync_and_takes_its_new_name(void);
void test_controller_handle_dropped_param_is_stale_and_a_restore_gets_a_new_one(void);
void test_controller_handle_param_is_stale_after_release_config(void);
void test_controller_handle_of_another_kind_is_an_invalid_arg(void);
void test_controller_handle_getter_returns_a_copy_the_caller_frees(void);
void test_controller_handle_device_param_by_id(void);
void test_controller_handle_notify_skips_a_released_param(void);
void test_controller_handle_batch_with_a_dropped_param_renders_nothing(void);
void test_controller_handle_parser_skips_fields_that_do_not_fit(void);
void test_controller_handle_parser_skips_a_name_that_does_not_fit(void);
void test_controller_handle_parser_skips_a_second_object_with_the_same_id(void);

/** Change lists: what each rebuild reports, in which order, and to whom. */
void test_controller_changes_register_replays_the_snapshot(void);
void test_controller_changes_register_before_a_sync_replays_nothing(void);
void test_controller_changes_resync_reports_a_dropped_and_an_added_node(void);
void test_controller_changes_resync_reports_a_node_whose_subgroups_changed(void);
void test_controller_changes_refetch_reports_a_dropped_and_an_added_param(void);
void test_controller_changes_refetch_reports_changed_bounds(void);
void test_controller_changes_refetch_with_no_change_reports_nothing(void);
void test_controller_changes_release_config_reports_each_device(void);
void test_controller_changes_deinit_reports_each_node_and_subgroup(void);
void test_controller_changes_lists_arrive_in_order_and_old_ones_are_dropped(void);
void test_controller_changes_none_are_collected_without_a_callback(void);

void test_controller_lifecycle_sync_builds_the_group_with_values(void);
void test_controller_lifecycle_resync_of_an_unchanged_group_fetches_no_config(void);
void test_controller_lifecycle_delta_updates_one_value_and_notifies_it(void);
void test_controller_lifecycle_online_change_notifies_the_node(void);
void test_controller_lifecycle_config_change_refetches_and_seeds_the_new_params(void);
void test_controller_lifecycle_failed_refetch_keeps_the_config_stale(void);
void test_controller_lifecycle_write_publishes_by_id_and_refuses_what_it_must(void);
void test_controller_lifecycle_group_write_publishes_by_type(void);
void test_controller_lifecycle_migration_rebuilds_the_group_and_its_filters(void);
void test_controller_lifecycle_leaving_the_group_drops_the_inventory(void);
void test_controller_lifecycle_mqtt_down_fails_cleanly(void);
void test_controller_lifecycle_missing_config_reports_not_found(void);
void test_controller_lifecycle_api_get_returns_the_body(void);
void test_controller_lifecycle_disable_drops_the_shadow_filters(void);
void test_controller_lifecycle_disable_from_a_callback_is_refused(void);
void test_controller_lifecycle_disable_drops_an_open_group_batch(void);
void test_controller_lifecycle_blocking_call_after_disable_is_not_initialized(void);
void test_controller_lifecycle_disable_makes_every_handle_stale(void);

#endif /* CONFIG_ESP_RMAKER_NEO_CONTROLLER */

/* All tests ******************************************************************/

int test_rmaker_neo_all_tests_unity(void);

/* Provisioning registry ******************************************************/

/**
 * @brief Test that capabilities under one label merge into a single app info entry.
 */
void test_prov_capabilities_merge_under_one_label(void);

/**
 * @brief Test that each label collects only its own capabilities.
 */
void test_prov_labels_stay_separate(void);

/**
 * @brief Test that the registry rejects an incomplete endpoint.
 */
void test_prov_add_rejects_incomplete_endpoints(void);

/**
 * @brief Test that the registry rejects an incomplete capability.
 */
void test_prov_add_rejects_incomplete_capabilities(void);

/**
 * @brief Test that adding the same endpoint again overrides its entry.
 */
void test_prov_add_same_endpoint_overrides(void);

/**
 * @brief Test that removing an endpoint keeps the order of the others.
 */
void test_prov_del_preserves_order(void);

/**
 * @brief Test that the registry reports a full registry.
 */
void test_prov_add_reports_a_full_registry(void);

/**
 * @brief Test that an endpoint can be added again after the registry empties.
 */
void test_prov_readd_after_drain(void);

#endif /* __TEST_RMAKER_NEO_PROTOTYPES_H__ */
