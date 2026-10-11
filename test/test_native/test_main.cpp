// Host-side unit tests for the pure-logic layer (no ESP-IDF required):
//   pio test -e native
// Each test_*.cpp declares its cases below; this file only runs them.
#include <unity.h>

void setUp() {}
void tearDown() {}

// test_nmea.cpp
void nmea_checksum_accepts_correct_sentence();
void nmea_checksum_rejects_corruption();
void nmea_checksum_rejects_missing_star_and_bad_hex();
void nmea_rmc_parses_position_speed_course_and_time();
void nmea_gga_merges_quality_satellites_hdop_altitude();
void nmea_rmc_without_fix_clears_valid_but_keeps_time();
void nmea_rmc_without_time_is_accepted_while_invalid();
void nmea_rmc_rejects_minutes_over_sixty_and_keeps_previous_fix();
void nmea_rmc_empty_speed_and_course_mean_zero();
void nmea_gga_without_fix_parses_zero_satellites();
void nmea_other_sentence_types_are_ignored_but_valid();
void nmea_corrupted_sentence_counts_checksum_failure();
void nmea_western_and_southern_hemispheres_are_negative();
void nmea_gsv_counts_satellites_in_view_per_epoch();
void nmea_gsv_counts_tracked_satellites_and_strongest_cno();
void assembler_delivers_sentence_without_line_ending();
void assembler_ignores_bytes_before_dollar_and_resyncs_mid_sentence();
void assembler_drops_and_counts_oversized_sentence();

// test_ubx.cpp
void ubx_frame_matches_reference_cfg_rate();
void ubx_frame_matches_reference_cfg_msg();
void ubx_m10_setup_frame_has_valset_header_and_checksum();
void ubx_static_hold_frames_carry_threshold_and_checksum();

// test_fsm.cpp
void fsm_starts_booting_and_acquires_on_first_sentence();
void fsm_tracks_on_valid_fix_and_returns_to_acquiring_when_lost();
void fsm_silence_leads_to_no_signal_from_any_live_state();
void fsm_motion_toggles_tracking_and_stationary();
void fsm_ignores_events_not_in_table();
void fsm_sleep_only_from_stationary_and_wakes_into_acquiring();
void fsm_state_names_are_stable();
void motion_speed_detector_starts_above_threshold();
void motion_speed_detector_stops_only_after_delay_below_lower_threshold();
void motion_speed_detector_brief_stop_does_not_count();
void motion_speed_detector_ignores_invalid_fixes();
void motion_null_detector_always_moving();

// test_filters.cpp
void ema_first_sample_seeds_without_transient();
void ema_steps_toward_input_by_alpha();
void ema_is_identity_for_constant_input_and_handles_negative();
void ema_reset_reseeds();
void geodesy_same_point_is_zero();
void geodesy_one_hundredth_degree_of_latitude_is_about_1112_m();
void geodesy_longitude_shrinks_with_latitude();
void logpolicy_never_logs_invalid_fix();
void logpolicy_logs_first_valid_fix_immediately();
void logpolicy_moving_uses_short_interval();
void logpolicy_stationary_uses_long_interval();
void logpolicy_distance_triggers_early_while_moving();
void logpolicy_handles_tick_wraparound();
void record_formats_fixed_point_with_sign_and_padding();
void record_csv_line_matches_expected_layout();
void record_csv_line_reports_overflow_as_zero();

// test_transport.cpp
void logger_writes_header_then_line_into_daily_file();
void logger_retries_transient_failure_without_recovery();
void logger_recovers_after_exhausting_retries();
void logger_reports_loss_when_recovery_fails();
void osmand_unix_time_matches_known_dates();
void osmand_battery_percent_is_clamped_linear();
void osmand_url_carries_every_field();
void osmand_widest_settings_fit_the_url_buffer();
void osmand_url_appends_extra_attributes_verbatim();
void osmand_url_too_long_returns_zero();
void scheduler_first_attempt_is_immediate_then_paced_by_min_interval();
void scheduler_backs_off_exponentially_up_to_cap();
void scheduler_requests_reset_after_failure_run_and_success_clears_it();
void button_short_and_long_press_by_duration();
void button_bounce_edges_inside_debounce_window_are_ignored();
void button_repeated_level_after_dropped_edge_is_ignored();
void command_parser_recognises_words_and_set_arguments();
void command_parser_unsigned_rejects_garbage_and_overflow();

int main()
{
    UNITY_BEGIN();

    RUN_TEST(nmea_checksum_accepts_correct_sentence);
    RUN_TEST(nmea_checksum_rejects_corruption);
    RUN_TEST(nmea_checksum_rejects_missing_star_and_bad_hex);
    RUN_TEST(nmea_rmc_parses_position_speed_course_and_time);
    RUN_TEST(nmea_gga_merges_quality_satellites_hdop_altitude);
    RUN_TEST(nmea_rmc_without_fix_clears_valid_but_keeps_time);
    RUN_TEST(nmea_rmc_without_time_is_accepted_while_invalid);
    RUN_TEST(nmea_rmc_rejects_minutes_over_sixty_and_keeps_previous_fix);
    RUN_TEST(nmea_rmc_empty_speed_and_course_mean_zero);
    RUN_TEST(nmea_gga_without_fix_parses_zero_satellites);
    RUN_TEST(nmea_other_sentence_types_are_ignored_but_valid);
    RUN_TEST(nmea_corrupted_sentence_counts_checksum_failure);
    RUN_TEST(nmea_western_and_southern_hemispheres_are_negative);
    RUN_TEST(nmea_gsv_counts_satellites_in_view_per_epoch);
    RUN_TEST(nmea_gsv_counts_tracked_satellites_and_strongest_cno);
    RUN_TEST(assembler_delivers_sentence_without_line_ending);
    RUN_TEST(assembler_ignores_bytes_before_dollar_and_resyncs_mid_sentence);
    RUN_TEST(assembler_drops_and_counts_oversized_sentence);

    RUN_TEST(ubx_frame_matches_reference_cfg_rate);
    RUN_TEST(ubx_frame_matches_reference_cfg_msg);
    RUN_TEST(ubx_m10_setup_frame_has_valset_header_and_checksum);
    RUN_TEST(ubx_static_hold_frames_carry_threshold_and_checksum);

    RUN_TEST(fsm_starts_booting_and_acquires_on_first_sentence);
    RUN_TEST(fsm_tracks_on_valid_fix_and_returns_to_acquiring_when_lost);
    RUN_TEST(fsm_silence_leads_to_no_signal_from_any_live_state);
    RUN_TEST(fsm_motion_toggles_tracking_and_stationary);
    RUN_TEST(fsm_ignores_events_not_in_table);
    RUN_TEST(fsm_sleep_only_from_stationary_and_wakes_into_acquiring);
    RUN_TEST(fsm_state_names_are_stable);
    RUN_TEST(motion_speed_detector_starts_above_threshold);
    RUN_TEST(motion_speed_detector_stops_only_after_delay_below_lower_threshold);
    RUN_TEST(motion_speed_detector_brief_stop_does_not_count);
    RUN_TEST(motion_speed_detector_ignores_invalid_fixes);
    RUN_TEST(motion_null_detector_always_moving);

    RUN_TEST(ema_first_sample_seeds_without_transient);
    RUN_TEST(ema_steps_toward_input_by_alpha);
    RUN_TEST(ema_is_identity_for_constant_input_and_handles_negative);
    RUN_TEST(ema_reset_reseeds);
    RUN_TEST(geodesy_same_point_is_zero);
    RUN_TEST(geodesy_one_hundredth_degree_of_latitude_is_about_1112_m);
    RUN_TEST(geodesy_longitude_shrinks_with_latitude);
    RUN_TEST(logpolicy_never_logs_invalid_fix);
    RUN_TEST(logpolicy_logs_first_valid_fix_immediately);
    RUN_TEST(logpolicy_moving_uses_short_interval);
    RUN_TEST(logpolicy_stationary_uses_long_interval);
    RUN_TEST(logpolicy_distance_triggers_early_while_moving);
    RUN_TEST(logpolicy_handles_tick_wraparound);
    RUN_TEST(record_formats_fixed_point_with_sign_and_padding);
    RUN_TEST(record_csv_line_matches_expected_layout);
    RUN_TEST(record_csv_line_reports_overflow_as_zero);

    RUN_TEST(logger_writes_header_then_line_into_daily_file);
    RUN_TEST(logger_retries_transient_failure_without_recovery);
    RUN_TEST(logger_recovers_after_exhausting_retries);
    RUN_TEST(logger_reports_loss_when_recovery_fails);
    RUN_TEST(osmand_unix_time_matches_known_dates);
    RUN_TEST(osmand_battery_percent_is_clamped_linear);
    RUN_TEST(osmand_url_carries_every_field);
    RUN_TEST(osmand_widest_settings_fit_the_url_buffer);
    RUN_TEST(osmand_url_appends_extra_attributes_verbatim);
    RUN_TEST(osmand_url_too_long_returns_zero);
    RUN_TEST(scheduler_first_attempt_is_immediate_then_paced_by_min_interval);
    RUN_TEST(scheduler_backs_off_exponentially_up_to_cap);
    RUN_TEST(scheduler_requests_reset_after_failure_run_and_success_clears_it);
    RUN_TEST(button_short_and_long_press_by_duration);
    RUN_TEST(button_bounce_edges_inside_debounce_window_are_ignored);
    RUN_TEST(button_repeated_level_after_dropped_edge_is_ignored);
    RUN_TEST(command_parser_recognises_words_and_set_arguments);
    RUN_TEST(command_parser_unsigned_rejects_garbage_and_overflow);

    return UNITY_END();
}
