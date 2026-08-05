/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_rmng_prototypes.h"
#include "sdkconfig.h"

int test_rmng_all_tests_unity(void)
{
    UNITY_BEGIN();

    /* Node model *****************************************************************/
    RUN_TEST(test_node_model_basic);
    RUN_TEST(test_device_create_rejects_path_separator);
    RUN_TEST(test_param_create_rejects_path_separator);
    RUN_TEST(test_path_round_trip);
    RUN_TEST(test_path_to_update_id_invalid);
    RUN_TEST(test_node_model_error_paths);
    RUN_TEST(test_node_model_persistent_params);
    RUN_TEST(test_node_model_persistent_params_across_deinit);
    RUN_TEST(test_node_config_basic);
    RUN_TEST(test_node_model_device_add_remove_and_errors);
    RUN_TEST(test_node_model_attributes_and_tags);
    RUN_TEST(test_node_model_getters_and_info);
    RUN_TEST(test_node_model_get_node_params_multiple_data_types);
    RUN_TEST(test_device_model_error_paths);
    RUN_TEST(test_device_model_delete_guard);
    RUN_TEST(test_device_model_attrs_callbacks_getters);
    RUN_TEST(test_param_model_getters);
    RUN_TEST(test_param_model_create_bounds_and_update);
    RUN_TEST(test_param_model_same_value_update_skips_nvs);
    RUN_TEST(test_param_model_store_get_invalid_parent);
    RUN_TEST(test_param_model_error_paths);
    RUN_TEST(test_param_model_handle_set_payload);
    RUN_TEST(test_param_model_handle_set_payload_typed);

    /* Timeseries ******************************************************************/
    RUN_TEST(test_timeseries_init_deinit);
    RUN_TEST(test_timeseries_push_data_basic);
    RUN_TEST(test_timeseries_push_data_validation);
    RUN_TEST(test_timeseries_get_payload_basic);
    RUN_TEST(test_timeseries_get_payload_multiple);
    RUN_TEST(test_timeseries_get_payload_max_items);
    RUN_TEST(test_timeseries_uninitialized_access);
    RUN_TEST(test_timeseries_data_types);
    RUN_TEST(test_timeseries_error_paths);
    RUN_TEST(test_timeseries_queue_behavior);

    /* Local control endpoint protocol *********************************************/
    RUN_TEST(test_local_ctrl_get_params_fragment_walk);
    RUN_TEST(test_local_ctrl_get_config_fragment_walk);
    RUN_TEST(test_local_ctrl_get_params_offset_without_reset_fails);
    RUN_TEST(test_local_ctrl_get_params_offset_beyond_total_fails);
    RUN_TEST(test_local_ctrl_get_data_type_mismatch_fails);
    RUN_TEST(test_local_ctrl_free_data_invalidates_cache);
    RUN_TEST(test_local_ctrl_get_params_malformed_frame_rejected);
    RUN_TEST(test_local_ctrl_set_params_invalid_json_keeps_session);
    RUN_TEST(test_local_ctrl_set_params_valid_json_succeeds);
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2
    RUN_TEST(test_local_ctrl_sec2_custom_pop_does_not_poison_cache);
    RUN_TEST(test_local_ctrl_sec2_generated_pop_is_cached);
    RUN_TEST(test_local_ctrl_sec2_resolve_rejects_invalid_args);
#endif

    /* Protobuf helpers ************************************************************/
    RUN_TEST(test_pb_c_cmd_challenge_roundtrip);
    RUN_TEST(test_claim_validate_data_bounds);
    RUN_TEST(test_claim_fragment_sequencing);
    RUN_TEST(test_claim_escape_new_line);
    RUN_TEST(test_claim_unescape_new_line);
    RUN_TEST(test_claim_command_guards);
    RUN_TEST(test_pb_c_resp_pack_to_buffer);

    /* Utilities - checksum ***********************************************************/
    RUN_TEST(test_checksum_basic);
    RUN_TEST(test_checksum_compare_store);
    RUN_TEST(test_checksum_compare_invalid_key);
    RUN_TEST(test_checksum_store_invalid_key);

    /* Utilities - trigger codec ******************************************************/
    RUN_TEST(test_trigger_codec_value_bool);
    RUN_TEST(test_trigger_codec_value_int);
    RUN_TEST(test_trigger_codec_value_float);
    RUN_TEST(test_trigger_codec_value_string);
    RUN_TEST(test_trigger_codec_value_object);
    RUN_TEST(test_trigger_codec_value_array);
    RUN_TEST(test_trigger_codec_preserves_fields);
    RUN_TEST(test_trigger_codec_enabled_defaults_true_when_omitted);
    RUN_TEST(test_trigger_codec_multiple_and_operators);
    RUN_TEST(test_trigger_codec_empty_array);
    RUN_TEST(test_trigger_codec_is_smaller_than_json);
    RUN_TEST(test_trigger_codec_encode_rejects_unknown_operator);
    RUN_TEST(test_trigger_codec_encode_rejects_missing_path);
    RUN_TEST(test_trigger_codec_encode_rejects_null_value);
    RUN_TEST(test_trigger_codec_iter_rejects_wrong_version);
    RUN_TEST(test_trigger_codec_iter_rejects_truncated);
    RUN_TEST(test_trigger_codec_invalid_args);

    /* MQTT topics ******************************************************************/
    RUN_TEST(test_mqtt_topics_basic);
    RUN_TEST(test_mqtt_topics_errors);
    RUN_TEST(test_mqtt_topics_group_control);
    RUN_TEST(test_mqtt_topics_parse_group_control_subgroup);

    /* Event loop *******************************************************************/
    RUN_TEST(test_event_loop_init_and_handlers);

    /* Network common ****************************************************************/
    RUN_TEST(test_network_common_init_deinit);
    RUN_TEST(test_network_common_payload);
    RUN_TEST(test_network_common_payload_zero_length);

    /* Cloud events ******************************************************************/
    RUN_TEST(test_cloud_event_timesync_builder);
    RUN_TEST(test_cloud_event_timesync_builder_registered);
    RUN_TEST(test_cloud_event_timesync_response_sets_processed_bit);
    RUN_TEST(test_cloud_event_timesync_response_ignores_invalid_time);

    /* Network notify ****************************************************************/
    RUN_TEST(test_notify_init_deinit);
    RUN_TEST(test_notify_send_invalid_args);
    RUN_TEST(test_notify_send_payload_generator_fail);
    RUN_TEST(test_notify_send_push_no_node_rejected);
    RUN_TEST(test_notify_send_push_routes_by_update_id);
    RUN_TEST(test_notify_param_update_and_notify_publishes_push);

    /* Schedules ********************************************************************/
    RUN_TEST(test_schedule_port_release_bumps_generation);
    RUN_TEST(test_schedule_port_lookup_rejects_released_slot);
    RUN_TEST(test_schedule_port_lookup_rejects_out_of_range_index);
    RUN_TEST(test_schedule_port_stale_dispatch_does_not_fire_recycled_slot);
    RUN_TEST(test_schedule_port_pool_recycles_slots);
    RUN_TEST(test_schedule_port_pool_grows_then_reuses);
    RUN_TEST(test_schedules_parse_action_valid_light_power);
    RUN_TEST(test_schedules_parse_action_valid_nested_object);
    RUN_TEST(test_schedules_parse_action_missing_returns_null);
    RUN_TEST(test_schedules_parse_action_empty_object_returns_empty);
    RUN_TEST(test_schedules_parse_action_free_null_safe);
    RUN_TEST(test_schedules_details_disabled_schedule_skipped);
    RUN_TEST(test_schedules_details_mixed_enabled_disabled);
    RUN_TEST(test_schedules_details_all_no_id_count_zero);
    RUN_TEST(test_schedules_details_clear_after_skip_no_crash);
    RUN_TEST(test_schedules_details_reparse_after_skip_no_crash);
    RUN_TEST(test_schedules_details_empty_id_rejected);
    RUN_TEST(test_schedules_details_oversized_id_rejected);
    RUN_TEST(test_schedules_details_numeric_id_rejected);
    RUN_TEST(test_schedules_details_null_id_rejected);
    RUN_TEST(test_schedules_details_object_id_rejected);
    RUN_TEST(test_schedules_details_max_length_id_accepted);
    RUN_TEST(test_schedules_details_name_field_ignored);
    RUN_TEST(test_schedules_parse_trigger_relative);
    RUN_TEST(test_schedules_parse_trigger_days_of_week);
    RUN_TEST(test_schedules_parse_trigger_date);
    RUN_TEST(test_schedules_parse_trigger_date_no_month_mask_recurs_monthly);
    RUN_TEST(test_schedules_parse_trigger_date_arm_beats_weekday_arm);
    RUN_TEST(test_schedules_parse_trigger_months_without_day_ignored);
    RUN_TEST(test_schedules_parse_trigger_minutes_out_of_range_fails);
    RUN_TEST(test_schedules_parse_trigger_empty_array_fails);
    RUN_TEST(test_schedules_parse_trigger_missing_fails);
    RUN_TEST(test_schedules_parse_trigger_extra_entries_dropped);
    RUN_TEST(test_schedules_parse_trigger_days_missing_minutes_fails);
    RUN_TEST(test_schedules_parse_trigger_invalid_first_entry_fails);
    RUN_TEST(test_schedules_parse_trigger_invalid_type_fails);
    RUN_TEST(test_schedules_details_count_clamped_to_max);
    RUN_TEST(test_schedules_update_details_null_input_safe);
    RUN_TEST(test_schedules_parse_validity_full);
    RUN_TEST(test_schedules_parse_validity_missing_returns_ok);
    RUN_TEST(test_schedules_parse_validity_empty_object);
    RUN_TEST(test_schedules_parse_validity_large_timestamps);
    RUN_TEST(test_schedules_parse_validity_bogus_keys_only);
    RUN_TEST(test_schedules_parse_validity_only_start);
    RUN_TEST(test_schedules_parse_validity_only_end);
    RUN_TEST(test_schedules_parse_validity_wrong_types);
    RUN_TEST(test_schedules_parse_validity_negative_values);
    RUN_TEST(test_schedules_parse_validity_not_an_object);
    RUN_TEST(test_schedules_parse_action_key_order_independent);
    RUN_TEST(test_schedules_parse_action_whitespace_tolerant);
    RUN_TEST(test_schedules_parse_full_schedule_object);
    RUN_TEST(test_schedules_serialize_round_trip_days_of_week);
    RUN_TEST(test_schedules_serialize_round_trip_date_recurring);
    RUN_TEST(test_schedules_serialize_round_trip_date_year_bounded);
    RUN_TEST(test_schedules_serialize_relative_emits_computed_ts);
    RUN_TEST(test_schedules_serialize_empty_node_is_empty_array);
    RUN_TEST(test_schedules_serialize_round_trip_preserves_count);
    RUN_TEST(test_schedules_arm_records_next_fire_for_date_trigger);
    RUN_TEST(test_schedules_arm_drops_date_trigger_with_past_year);
    RUN_TEST(test_schedules_serialize_emits_ts_for_date_one_shot);
    RUN_TEST(test_schedules_serialize_omits_ts_for_repeating);
    RUN_TEST(test_schedules_replay_skips_one_shot_already_due);
    RUN_TEST(test_schedules_replay_keeps_one_shot_still_pending);
    RUN_TEST(test_schedules_arm_drops_year_bounded_repeating_after_expiry);
    RUN_TEST(test_schedules_arm_keeps_year_bounded_repeating_still_valid);
    RUN_TEST(test_schedules_fired_one_shot_removed_from_node_and_payload);
    RUN_TEST(test_schedules_remove_by_cloud_id_removes_one);
    RUN_TEST(test_schedules_remove_by_cloud_id_unknown_is_noop);
    RUN_TEST(test_schedules_remove_by_cloud_id_last_one_frees_array);
    RUN_TEST(test_schedules_remove_by_cloud_id_null_safe);
    RUN_TEST(test_schedules_one_shot_flagged_in_priv_data);
    RUN_TEST(test_schedules_reload_no_stored_details_is_noop);
    RUN_TEST(test_schedules_reload_null_node_rejected);
    RUN_TEST(test_schedules_drop_node_releases_handles);
    RUN_TEST(test_schedules_erase_node_releases_handles);
    RUN_TEST(test_schedules_drop_and_erase_null_node_safe);
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#endif

    /* Automation *******************************************************************/
    RUN_TEST(test_automation_parse_details_null_invalid_arg);
    RUN_TEST(test_automation_parse_details_empty_invalid_arg);
    RUN_TEST(test_automation_parse_details_invalid_json_fail);
    RUN_TEST(test_automation_parse_details_whitespace_empty_array);
    RUN_TEST(test_automation_parse_details_whitespace_invalid_triggers);
    RUN_TEST(test_automation_parse_details_all_invalid_frees_list);
    RUN_TEST(test_automation_parse_details_valid_format);
    RUN_TEST(test_automation_parse_details_exceeds_maximum);
    RUN_TEST(test_automation_parse_details_boolean_non_eq_ne_operators_rejected);
    RUN_TEST(test_automation_parse_details_partial_failure_rolls_back);
    RUN_TEST(test_automation_parse_details_enabled_false_single_no_triggers);
    RUN_TEST(test_automation_parse_details_enabled_omitted_adds_trigger);
    RUN_TEST(test_automation_parse_details_enabled_explicit_true_and_false_skips);

    /* System control ****************************************************************/
    /* System control tests removed due to potential interference with test execution */

    /* Local config *****************************************************************/
    RUN_TEST(test_local_config_group_info_format);
    RUN_TEST(test_local_config_group_info_empty);
    RUN_TEST(test_local_config_group_info_format_skips_empty_subgroups);
    RUN_TEST(test_local_config_group_info_parse_packs_subgroups);
    RUN_TEST(test_local_config_group_info_set_get);
    RUN_TEST(test_local_config_other_accessors);
    RUN_TEST(test_local_config_group_info_invalid_args);
    RUN_TEST(test_local_config_group_info_null_subgroups);
    RUN_TEST(test_local_config_alexa_flag_default);
    RUN_TEST(test_local_config_sched_trigger_details_roundtrip);
    RUN_TEST(test_local_config_parse_group_info_primary_only);
    RUN_TEST(test_local_config_parse_group_info_with_subgroups);
    RUN_TEST(test_local_config_parse_group_info_empty_and_null);
    RUN_TEST(test_local_config_parse_group_info_invalid_args);
    RUN_TEST(test_local_config_parse_group_info_roundtrip);
    RUN_TEST(test_local_config_parse_group_info_max_subgroups);
    RUN_TEST(test_local_config_parse_group_info_null_subgroups);

    /* State changes ****************************************************************/
    RUN_TEST(test_state_changes_lock_unlock);
    RUN_TEST(test_state_changes_concurrent_mark_and_drain);
    RUN_TEST(test_get_all_rejects_null_ctx);
    RUN_TEST(test_get_all_self_ctx_returns_self_params_only);
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    RUN_TEST(test_get_all_filters_by_child_ctx);
    RUN_TEST(test_get_all_unknown_ctx_returns_zero);
#endif

    /* Subgroup-aware dispatch filter *******************************************/
    RUN_TEST(test_node_is_in_subgroup_rejects_invalid_args);
    RUN_TEST(test_node_is_in_subgroup_self_membership);
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    RUN_TEST(test_node_is_in_subgroup_bridge_child_membership);
#endif

    /* Core *************************************************************************/
    RUN_TEST(test_core_pre_prov_init_success);
    RUN_TEST(test_core_pre_prov_deinit);
    RUN_TEST(test_core_node_init_null_config);
    RUN_TEST(test_core_node_init_success);
    RUN_TEST(test_core_node_deinit_states);
    RUN_TEST(test_core_start_without_init);
    // RUN_TEST(test_core_start_stop);
    RUN_TEST(test_core_stop_without_start);
    RUN_TEST(test_core_online_status_conditions);
    RUN_TEST(test_core_sign_challenge_invalid_args);
    RUN_TEST(test_core_error_paths);

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    /* Bridge *********************************************************************/
    RUN_TEST(test_bridge_topic_group_control_subgroup_wildcard);
    RUN_TEST(test_bridge_topic_bridges_to_cloud);
    RUN_TEST(test_bridge_topic_child_shadow_updates);
    RUN_TEST(test_bridge_suffix_validator);
    RUN_TEST(test_bridge_parse_child_from_from_cloud_topic);
    RUN_TEST(test_bridge_parse_child_and_shadow_from_params_topic);
    RUN_TEST(test_bridge_public_api_arg_validation);
    RUN_TEST(test_bridge_child_nvs_load_absent_returns_not_found);
    RUN_TEST(test_bridge_child_nvs_store_load_roundtrip);
    RUN_TEST(test_bridge_child_nvs_set_sched_ver_preserves_other_fields);
    RUN_TEST(test_bridge_child_nvs_set_sched_ver_creates_when_absent);
    RUN_TEST(test_bridge_child_nvs_set_node_config_writes_both);
    RUN_TEST(test_bridge_child_nvs_erase);
    RUN_TEST(test_bridge_child_nvs_invalidates_on_version_mismatch);
    RUN_TEST(test_bridge_child_nvs_invalidates_on_size_mismatch);
    RUN_TEST(test_bridge_child_nvs_long_local_id_no_truncation_collision);
    RUN_TEST(test_bridge_child_nvs_compute_key);
    RUN_TEST(test_bridge_child_nvs_invalid_arg);
    RUN_TEST(test_bridge_accessors_local_id);
    RUN_TEST(test_bridge_accessors_topic_ctx);
    RUN_TEST(test_bridge_accessors_child_from_ctx_roundtrip);
    RUN_TEST(test_bridge_accessors_child_from_ctx_after_invalidate);
    RUN_TEST(test_bridge_accessors_version_progress);
    RUN_TEST(test_bridge_accessors_find_by_thing_name_and_local_id);
    RUN_TEST(test_bridge_accessors_slot_pool_exhaustion);
    RUN_TEST(test_bridge_accessors_seed_child_arg_validation);
    RUN_TEST(test_bridge_parse_child_from_from_cloud_topic_edge_cases);
    RUN_TEST(test_bridge_parse_child_and_shadow_from_params_topic_edge_cases);
    RUN_TEST(test_bridge_suffix_validator_extended);
    RUN_TEST(test_node_config_pending_init_idempotent);
    RUN_TEST(test_node_config_pending_add_rejects_null_ctx);
    RUN_TEST(test_node_config_pending_add_idempotent);
    RUN_TEST(test_node_config_pending_remove_self_and_readd);
    RUN_TEST(test_node_config_pending_remove_unknown_noop);
    RUN_TEST(test_node_config_pending_clear_inflight_unknown_noop);
    RUN_TEST(test_node_config_pending_clear_all_inflight_safe);
#endif

    return UNITY_END();
}
