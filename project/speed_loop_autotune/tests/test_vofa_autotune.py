import contextlib
import importlib
import importlib.util
import io
import json
import pathlib
import tempfile
import unittest


MODULE_PATH = pathlib.Path(__file__).resolve().parents[1] / "host" / "vofa_autotune.py"


def load_module():
    spec = importlib.util.spec_from_file_location("vofa_autotune", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError("Unable to load tools/vofa_autotune.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_host_package_module(module_name):
    return importlib.import_module("project.speed_loop_autotune.host.{0}".format(module_name))


class ParseTelemetryTests(unittest.TestCase):
    def test_accepts_three_or_four_columns(self):
        module = load_module()

        three = module.parse_telemetry_line("12.5,10.0,11.0")
        four = module.parse_telemetry_line("12.5,10.0,11.0,0.0")
        seven = module.parse_telemetry_line("30.0,29.5,29.0,3200,3180,1,0")
        ten = module.parse_telemetry_line("30.0,29.5,29.0,3200,3180,1,0,2,3000,0")

        self.assertIsNotNone(three)
        self.assertIsNotNone(four)
        self.assertIsNotNone(seven)
        self.assertIsNotNone(ten)
        self.assertEqual(three.target, 12.5)
        self.assertEqual(three.left_speed, 10.0)
        self.assertEqual(three.right_speed, 11.0)
        self.assertEqual(four.left_pwm, 0.0)
        self.assertEqual(seven.left_pwm, 3200.0)
        self.assertEqual(seven.right_pwm, 3180.0)
        self.assertEqual(seven.trial_active, 1.0)
        self.assertEqual(seven.stop_flag, 0.0)
        self.assertEqual(seven.mode_id, 0.0)
        self.assertEqual(seven.left_cmd_pwm, 0.0)
        self.assertEqual(seven.right_cmd_pwm, 0.0)
        self.assertEqual(ten.mode_id, 2.0)
        self.assertEqual(ten.left_cmd_pwm, 3000.0)
        self.assertEqual(ten.right_cmd_pwm, 0.0)

    def test_rejects_invalid_rows(self):
        module = load_module()

        self.assertIsNone(module.parse_telemetry_line(""))
        self.assertIsNone(module.parse_telemetry_line("abc,1,2"))
        self.assertIsNone(module.parse_telemetry_line("1,2"))


class ScoreTests(unittest.TestCase):
    def test_prefers_small_error_and_low_overshoot(self):
        module = load_module()

        calm = [
            module.TelemetrySample(20.0, 18.0, 18.5, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 19.2, 19.1, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 19.8, 19.7, 0.0, 0.0, 0.0, 1.0),
        ]
        wild = [
            module.TelemetrySample(20.0, 10.0, 8.0, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 28.0, 30.0, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 15.0, 12.0, 0.0, 0.0, 0.0, 1.0),
        ]

        self.assertLess(module.score_trial(calm), module.score_trial(wild))

    def test_prefers_fast_and_stable_response(self):
        module = load_module()

        fast = [
            module.TelemetrySample(20.0, 8.0, 8.5, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 17.5, 17.0, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 19.5, 19.4, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 20.0, 19.9, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 19.9, 20.0, 0.0, 0.0, 0.0, 1.0),
        ]
        slow = [
            module.TelemetrySample(20.0, 3.0, 3.5, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 7.0, 7.5, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 11.0, 11.2, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 14.5, 14.8, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 17.0, 17.2, 0.0, 0.0, 0.0, 1.0),
        ]

        self.assertLess(module.score_trial(fast), module.score_trial(slow))

    def test_scores_left_and_right_wheels_independently(self):
        module = load_module()
        trial = module.build_air_primary_trial("20:200,40:200")

        samples = [
            module.TelemetrySample(20.0, 8.0, 18.0, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 12.0, 19.5, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(40.0, 18.0, 37.0, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(40.0, 22.0, 39.0, 0.0, 0.0, 1.0, 0.0),
        ]

        left_score = module.score_wheel_multi_speed_trial(samples, trial, "left")
        right_score = module.score_wheel_multi_speed_trial(samples, trial, "right")

        self.assertLess(right_score, left_score)

    def test_dual_score_penalizes_joint_high_pwm_with_speed_sag(self):
        module = load_module()
        trial = module.build_air_primary_trial("20:200,40:200")

        healthy = [
            module.TelemetrySample(20.0, 18.5, 18.2, 2200.0, 2250.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 19.8, 19.7, 2400.0, 2420.0, 1.0, 0.0),
            module.TelemetrySample(40.0, 36.5, 36.2, 2900.0, 2920.0, 1.0, 0.0),
            module.TelemetrySample(40.0, 39.0, 38.8, 3100.0, 3120.0, 1.0, 0.0),
        ]
        sagged = [
            module.TelemetrySample(20.0, 13.0, 12.8, 3400.0, 3420.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 15.5, 15.0, 3600.0, 3580.0, 1.0, 0.0),
            module.TelemetrySample(40.0, 25.0, 24.5, 3700.0, 3680.0, 1.0, 0.0),
            module.TelemetrySample(40.0, 29.0, 28.7, 3800.0, 3790.0, 1.0, 0.0),
        ]

        self.assertLess(
            module.score_dual_wheel_multi_speed_trial(healthy, trial),
            module.score_dual_wheel_multi_speed_trial(sagged, trial),
        )


class ScoreConfigTests(unittest.TestCase):
    def test_parser_defaults_follow_current_tuning_rules(self):
        module = load_module()
        parser = module.build_argument_parser()

        args = parser.parse_args([])

        self.assertEqual(args.mode, "pwm-map")
        self.assertEqual(args.air_primary_sequence, module.DEFAULT_AIR_PRIMARY_SEQUENCE)
        self.assertEqual(args.air_verify_sequence, module.DEFAULT_AIR_VERIFY_SEQUENCE)
        self.assertEqual(args.identify_pwm_step, 200)
        self.assertEqual(args.identify_pwm_max, 10000)
        self.assertEqual(args.identify_repeat, 2)
        self.assertEqual(args.identify_hold_ms, 250)
        self.assertEqual(args.identify_tail_zero_ms, 200)
        self.assertEqual(args.map_pwm_step, 500)
        self.assertEqual(args.map_pwm_max, 10000)
        self.assertEqual(args.map_repeat, 2)
        self.assertEqual(args.map_hold_ms, 250)
        self.assertEqual(args.map_tail_zero_ms, 200)
        self.assertEqual(args.map_output, "")
        self.assertEqual(args.profile_path, str(module.DEFAULT_TUNING_PROFILE_PATH))
        self.assertFalse(args.apply_identify_seed)
        self.assertFalse(hasattr(args, "candidate_limit"))
        self.assertFalse(hasattr(args, "interactive_batches"))
        self.assertFalse(hasattr(args, "iterations"))
        self.assertFalse(hasattr(args, "delta_kp"))
        self.assertFalse(hasattr(args, "delta_ki"))
        self.assertFalse(hasattr(args, "delta_kd"))
        self.assertFalse(hasattr(args, "search_tolerance"))

    def test_normalize_mode_name_rejects_legacy_batch_labels(self):
        module = load_module()

        self.assertEqual(module.normalize_mode_name("pwm-identify"), "pwm-identify")
        self.assertEqual(module.normalize_mode_name("pwm-map"), "pwm-map")
        with self.assertRaises(ValueError):
            module.normalize_mode_name("air-dual")
        with self.assertRaises(ValueError):
            module.normalize_mode_name("autotune")
        with self.assertRaises(ValueError):
            module.normalize_mode_name("ground-dual")
        with self.assertRaises(ValueError):
            module.normalize_mode_name("ground-load")

    def test_build_score_config_uses_cli_weights(self):
        module = load_module()
        parser = module.build_argument_parser()

        args = parser.parse_args(
            [
                "--score-rise-weight",
                "1.3",
                "--score-overshoot-weight",
                "2.7",
                "--score-settle-weight",
                "3.4",
                "--score-steady-weight",
                "0.9",
                "--score-overshoot-gate",
                "0.06",
                "--score-overshoot-gate-penalty",
                "2.2",
            ]
        )

        config = module.build_score_config(args)

        self.assertEqual(config.rise_weight, 1.3)
        self.assertEqual(config.overshoot_weight, 2.7)
        self.assertEqual(config.settle_weight, 3.4)
        self.assertEqual(config.steady_weight, 0.9)
        self.assertEqual(config.overshoot_gate, 0.06)
        self.assertEqual(config.overshoot_gate_penalty, 2.2)

    def test_overshoot_gate_can_outweigh_small_rise_advantage(self):
        module = load_module()

        config = module.ScoreConfig(
            1.2,
            1.8,
            2.2,
            1.0,
            0.05,
            2.0,
        )

        calm = [
            module.TelemetrySample(20.0, 7.0, 7.2, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 15.5, 15.7, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 18.8, 19.0, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 20.0, 19.9, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 19.9, 20.0, 0.0, 0.0, 0.0, 1.0),
        ]
        aggressive = [
            module.TelemetrySample(20.0, 10.0, 10.2, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 19.8, 20.0, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 23.8, 24.0, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 20.2, 20.0, 0.0, 0.0, 1.0, 0.0),
            module.TelemetrySample(20.0, 20.0, 20.1, 0.0, 0.0, 0.0, 1.0),
        ]

        self.assertLess(
            module.score_trial(calm, score_config=config),
            module.score_trial(aggressive, score_config=config),
        )


class AirDualSummaryTests(unittest.TestCase):
    def _make_display(self, module, low_score, primary_score):
        return {
            "wheels": {
                "left": {
                    "segments": [
                        {
                            "target_speed": 15.0,
                            "sample_count": 18,
                            "mean_speed": 14.2,
                            "score": low_score,
                            "rise_ratio": 0.12,
                            "settle_ratio": 0.88,
                            "overshoot": 1.1,
                            "steady_error": 0.4,
                        },
                        {
                            "target_speed": 25.0,
                            "sample_count": 15,
                            "mean_speed": 24.4,
                            "score": primary_score,
                            "rise_ratio": 0.15,
                            "settle_ratio": 0.22,
                            "overshoot": 0.7,
                            "steady_error": 0.3,
                        },
                    ]
                }
            }
        }

class ModuleSplitTests(unittest.TestCase):
    def test_split_host_modules_can_be_imported_individually(self):
        common = load_host_package_module("common")
        air_dual = load_host_package_module("air_dual")
        ground_dual = load_host_package_module("ground_dual")
        pwm_identify = load_host_package_module("pwm_identify")
        pwm_map = load_host_package_module("pwm_map")

        self.assertEqual(common.MODE_AIR_DUAL_STEP, "air-dual-step")
        self.assertEqual(common.MODE_GROUND_DUAL_STEP, "ground-dual-step")
        self.assertEqual(common.MODE_PWM_IDENTIFY, "pwm-identify")
        self.assertEqual(common.MODE_PWM_MAP, "pwm-map")
        self.assertEqual(air_dual.DEFAULT_AIR_PRIMARY_SEQUENCE.count(","), 6)
        self.assertFalse(hasattr(air_dual, "run_air_dual_autotune"))
        self.assertFalse(hasattr(ground_dual, "run_ground_dual_autotune"))
        self.assertTrue(hasattr(pwm_identify, "run_pwm_identify"))
        self.assertTrue(hasattr(pwm_map, "run_pwm_map"))

    def test_top_level_module_reexports_split_host_entries(self):
        module = load_module()
        common = load_host_package_module("common")
        air_dual = load_host_package_module("air_dual")
        ground_dual = load_host_package_module("ground_dual")
        pwm_identify = load_host_package_module("pwm_identify")
        pwm_map = load_host_package_module("pwm_map")

        self.assertIs(module.normalize_mode_name, common.normalize_mode_name)
        self.assertFalse(hasattr(module, "run_air_dual_autotune"))
        self.assertFalse(hasattr(module, "run_ground_dual_autotune"))
        self.assertIs(module.run_pwm_identify, pwm_identify.run_pwm_identify)
        self.assertIs(module.run_pwm_map, pwm_map.run_pwm_map)


class FirmwareRegistryTests(unittest.TestCase):
    def test_host_autotune_command_routes_through_registry_dispatch(self):
        source = (
            MODULE_PATH.parents[1] / "firmware" / "host_autotune_command.c"
        ).read_text(encoding="utf-8")

        self.assertIn("speed_loop_autotune_dispatch_param", source)
        self.assertIn("speed_loop_autotune_dispatch_command", source)

    def test_firmware_registry_lists_all_supported_autotune_tokens(self):
        source = (
            MODULE_PATH.parents[1] / "firmware" / "autotune_token_registry.c"
        ).read_text(encoding="utf-8")

        for token in (
            "AT_KP",
            "AT_KI",
            "AT_KD",
            "TEST_speed",
            "AT_SPEED",
            "AT_FUYA",
            "AT_TRIAL_MS",
            "AT_COOLDOWN_MS",
            "AT_TEST_MODE",
            "L_TEST_PWM",
            "R_TEST_PWM",
            "TEST_pwm",
            "START",
            "STOP",
            "AT_RESET",
            "AT_ARM",
            "AT_FIRE",
            "INFO",
        ):
            self.assertIn('"{0}"'.format(token), source)


class FirmwareComponentBoundaryTests(unittest.TestCase):
    def test_component_layers_exist_with_expected_entry_points(self):
        firmware_dir = MODULE_PATH.parents[1] / "firmware"

        component_source = (firmware_dir / "autotune_component.c").read_text(encoding="utf-8")
        binding_source = (firmware_dir / "autotune_binding.c").read_text(encoding="utf-8")
        port_source = (firmware_dir / "autotune_port.c").read_text(encoding="utf-8")
        pid_core_source = (firmware_dir / "autotune_pid_core.c").read_text(encoding="utf-8")
        public_header_source = (firmware_dir / "speed_loop_autotune.h").read_text(encoding="utf-8")
        private_header_source = (firmware_dir / "speed_loop_autotune_private.h").read_text(encoding="utf-8")

        self.assertIn("speed_loop_autotune_component_init", component_source)
        self.assertIn("speed_loop_autotune_component_run_closed_loop", component_source)
        self.assertIn("speed_loop_autotune_binding_bind", binding_source)
        self.assertIn("speed_loop_autotune_port_check", port_source)
        self.assertIn("speed_loop_autotune_pid_step", pid_core_source)
        self.assertNotIn("speed_loop_autotune_component_init_default", component_source)
        self.assertNotIn('#include "autotune_binding.h"', public_header_source)
        self.assertNotIn('#include "autotune_port.h"', public_header_source)
        self.assertNotIn('#include "autotune_component.h"', public_header_source)
        self.assertIn('#include "speed_loop_autotune.h"', private_header_source)

    def test_only_project_adapter_files_touch_external_pid_and_motor_symbols(self):
        firmware_dir = MODULE_PATH.parents[1] / "firmware"
        forbidden_patterns = (
            "PID.left_speed",
            "PID.right_speed",
            "Encoder_get(",
            "pid_speed_reset(",
            "pid_speed_update(",
            "test_speed_func(",
            "motor_output(",
            "fuya_motor_output(",
        )

        for path in firmware_dir.glob("*.c"):
            source = path.read_text(encoding="utf-8")
            for pattern in forbidden_patterns:
                self.assertNotIn(pattern, source, msg="{0} leaked into {1}".format(pattern, path.name))

    def test_host_service_and_info_read_from_component_layer(self):
        firmware_dir = MODULE_PATH.parents[1] / "firmware"
        host_service_source = (firmware_dir / "host_service.c").read_text(encoding="utf-8")
        host_command_source = (firmware_dir / "host_autotune_command.c").read_text(encoding="utf-8")

        self.assertIn("speed_loop_autotune_component_get_left_speed", host_service_source)
        self.assertIn("speed_loop_autotune_component_get_right_speed", host_service_source)
        self.assertIn("speed_loop_autotune_component_get_target_speed", host_command_source)
        self.assertIn("speed_loop_autotune_component_get_gain", host_command_source)

    def test_external_service_adapter_owns_project_binding_and_port(self):
        service_dir = MODULE_PATH.parents[2] / "service"
        adapter_source = (service_dir / "speed_loop_autotune_adapter.c").read_text(encoding="utf-8")
        int_user_source = (MODULE_PATH.parents[2] / "user" / "int_user.c").read_text(encoding="utf-8")
        firmware_dir = MODULE_PATH.parents[1] / "firmware"
        private_users = (
            (firmware_dir / "air_dual_mode.c").read_text(encoding="utf-8"),
            (firmware_dir / "ground_dual_mode.c").read_text(encoding="utf-8"),
            (firmware_dir / "pwm_identify_mode.c").read_text(encoding="utf-8"),
            (firmware_dir / "autotune_runtime.c").read_text(encoding="utf-8"),
            (firmware_dir / "host_autotune_command.c").read_text(encoding="utf-8"),
            (firmware_dir / "host_service.c").read_text(encoding="utf-8"),
            (firmware_dir / "speed_loop_trial.c").read_text(encoding="utf-8"),
        )

        self.assertIn("PID.left_speed.Kp", adapter_source)
        self.assertIn("motor_output(", adapter_source)
        self.assertIn("speed_loop_autotune_component_init(", adapter_source)
        self.assertIn("speed_loop_autotune_project_init();", int_user_source)
        self.assertIn('../speed_loop_autotune/firmware/speed_loop_autotune.h', adapter_source)
        self.assertNotIn("autotune_binding.h", adapter_source)
        self.assertNotIn("autotune_port.h", adapter_source)
        self.assertNotIn("autotune_component.h", adapter_source)
        for source in private_users:
            self.assertIn('#include "speed_loop_autotune_private.h"', source)


class FirmwareHostTransportBoundaryTests(unittest.TestCase):
    def test_host_transport_bridges_text_commands_without_vofa_header(self):
        firmware_dir = MODULE_PATH.parents[1] / "firmware"
        host_transport_source = (firmware_dir / "host_transport.c").read_text(encoding="utf-8")
        host_transport_header = (firmware_dir / "host_transport.h").read_text(encoding="utf-8")
        host_service_source = (firmware_dir / "host_service.c").read_text(encoding="utf-8")
        host_command_source = (firmware_dir / "host_autotune_command.c").read_text(encoding="utf-8")

        self.assertIn("uint8 speed_loop_autotune_handle_text_command", host_transport_header)
        self.assertIn("return speed_loop_autotune_handle_command_text(cmd);", host_transport_source)
        self.assertNotIn("vofa.h", host_service_source)
        self.assertNotIn("vofa.h", host_command_source)

    def test_service_layer_owns_vofa_polling_and_legacy_fallback(self):
        service_dir = MODULE_PATH.parents[2] / "service"
        vofa_source = (service_dir / "vofa.c").read_text(encoding="utf-8")
        vofa_header = (service_dir / "vofa.h").read_text(encoding="utf-8")
        debug_view_source = (service_dir / "debug_view.c").read_text(encoding="utf-8")

        self.assertIn('#include "../speed_loop_autotune/firmware/host_transport.h"', vofa_source)
        self.assertIn("void vofa_service(void)", vofa_source)
        self.assertIn("void vofa_service_legacy(void)", vofa_source)
        self.assertIn("speed_loop_autotune_set_parser_stats(", vofa_source)
        self.assertIn("speed_loop_autotune_emit_telemetry();", vofa_source)
        self.assertIn("if (speed_loop_autotune_handle_text_command(cmd))", vofa_source)
        self.assertIn("vofa_handle_legacy_command(vofa_cmd);", vofa_source)
        for token in ("L_KP", "R_KP", "A_KP", "MOTOR", "ERR", "SAVE", "LOAD", "FUYA"):
            self.assertIn(token, vofa_source)
        self.assertIn("void vofa_service(void);", vofa_header)
        self.assertIn("void vofa_service_legacy(void);", vofa_header)
        self.assertNotIn("vofa_parse_command", vofa_source)
        self.assertNotIn("vofa_parse_command", vofa_header)
        self.assertIn('#include "vofa.h"', debug_view_source)
        self.assertIn("#define DEBUG_VIEW_ENABLE_SPEED_LOOP_AUTOTUNE 1", debug_view_source)
        self.assertIn("vofa_service();", debug_view_source)
        self.assertIn("vofa_service_legacy();", debug_view_source)
        self.assertIn("#if DEBUG_VIEW_ENABLE_SPEED_LOOP_AUTOTUNE", debug_view_source)
        self.assertNotIn("host_service.h", debug_view_source)

    def test_keil_project_includes_host_transport_sources(self):
        uvproj_source = (MODULE_PATH.parents[2] / "mdk" / "seekfree.uvproj").read_text(encoding="utf-8")

        self.assertIn("host_transport.c", uvproj_source)
        self.assertIn("host_transport.h", uvproj_source)


class UserIsrSwitchTests(unittest.TestCase):
    def test_main_and_isr_share_per_function_switch_macros(self):
        user_dir = MODULE_PATH.parents[2] / "user"
        main_source = (user_dir / "main.c").read_text(encoding="utf-8")
        isr_source = (user_dir / "isr.c").read_text(encoding="utf-8")
        isr_header_source = (user_dir / "isr.h").read_text(encoding="utf-8")

        self.assertNotIn('#include "isr.h"', main_source)
        self.assertNotIn('#include "isr.h"', isr_source)
        self.assertIn("#define MAIN_ENABLE_ISR_RUN_TEST_SPEED", isr_header_source)
        self.assertIn("#define MAIN_ENABLE_ISR_TEST_ANGLE_FUNC", isr_header_source)
        self.assertIn("#define MAIN_ENABLE_ISR_TEST_SPEED_FUNC", isr_header_source)
        self.assertIn("#define MAIN_ENABLE_ISR_RUN_TIME_1", isr_header_source)
        self.assertIn("#define MAIN_ENABLE_ISR_RUN_TIME_2", isr_header_source)
        self.assertIn("#if MAIN_ENABLE_ISR_RUN_TEST_SPEED", isr_source)
        self.assertIn("#if MAIN_ENABLE_ISR_TEST_ANGLE_FUNC", isr_source)
        self.assertIn("#if MAIN_ENABLE_ISR_TEST_SPEED_FUNC", isr_source)
        self.assertIn("#if MAIN_ENABLE_ISR_RUN_TIME_1", isr_source)
        self.assertIn("#if MAIN_ENABLE_ISR_RUN_TIME_2", isr_source)
        self.assertIn("run_test_speed();", isr_source)
        self.assertIn("test_angle_func();", isr_source)
        self.assertIn("test_speed_func();", isr_source)
        self.assertIn("run_time_1();", isr_source)
        self.assertIn("run_time_2();", isr_source)


class MainSpeedOutputPathTests(unittest.TestCase):
    def test_motor_layer_does_not_keep_main_deadzone_comp_entry(self):
        service_dir = MODULE_PATH.parents[2] / "service"
        motor_header = (service_dir / "motor.h").read_text(encoding="utf-8")
        motor_source = (service_dir / "motor.c").read_text(encoding="utf-8")

        self.assertNotIn("MAIN_ENABLE_SPEED_DEADZONE_COMP", motor_header)
        self.assertNotIn("MAIN_LEFT_DEADZONE_PWM", motor_header)
        self.assertNotIn("MAIN_RIGHT_DEADZONE_PWM", motor_header)
        self.assertNotIn("MAIN_DEADZONE_BAND_PWM", motor_header)
        self.assertNotIn("MAIN_DEADZONE_EXIT_SPEED", motor_header)
        self.assertNotIn("MAIN_DEADZONE_TARGET_SPEED_MIN", motor_header)
        self.assertNotIn("motor_apply_speed_deadzone_comp", motor_header)
        self.assertNotIn("motor_apply_speed_deadzone_comp", motor_source)

    def test_main_control_paths_output_speed_loop_result_directly(self):
        service_dir = MODULE_PATH.parents[2] / "service"
        user_dir = MODULE_PATH.parents[2] / "user"
        autotune_dir = MODULE_PATH.parents[1]
        a_run_source = (user_dir / "a_run.c").read_text(encoding="utf-8")
        test_source = (service_dir / "test.c").read_text(encoding="utf-8")
        vofa_source = (service_dir / "vofa.c").read_text(encoding="utf-8")
        adapter_source = (service_dir / "speed_loop_autotune_adapter.c").read_text(encoding="utf-8")

        self.assertEqual(a_run_source.count("left_pwm = (int32)PID.left_speed.output;"), 2)
        self.assertEqual(a_run_source.count("right_pwm = (int32)PID.right_speed.output;"), 2)
        self.assertNotIn("motor_apply_speed_deadzone_comp", a_run_source)
        self.assertNotIn("motor_apply_speed_deadzone_comp", test_source)
        self.assertNotIn("motor_apply_speed_deadzone_comp", vofa_source)
        self.assertNotIn("motor_apply_speed_deadzone_comp", adapter_source)

        for source_path in autotune_dir.rglob("*.c"):
            source_text = source_path.read_text(encoding="utf-8")
            self.assertNotIn("motor_apply_speed_deadzone_comp", source_text)


class GroundLoadTests(unittest.TestCase):
    def test_combine_multi_speed_scores_prioritizes_worst_segment(self):
        module = load_module()

        balanced = module.combine_multi_speed_scores([1.0, 1.0])
        spiky = module.combine_multi_speed_scores([0.2, 1.5])

        self.assertLess(balanced, spiky)

    def test_low_speed_segments_can_be_ignored_from_group_score(self):
        module = load_module()

        trial = module.SpeedStageTrial(
            "mixed_8_20",
            ((8.0, 200), (20.0, 200)),
            400,
        )

        with_bad_low_speed = [
            [
                module.TelemetrySample(8.0, 1.0, 0.5, 1800.0, 1750.0, 1.0, 0.0),
                module.TelemetrySample(8.0, 2.0, 1.5, 1900.0, 1850.0, 1.0, 0.0),
                module.TelemetrySample(20.0, 14.0, 14.2, 2300.0, 2320.0, 1.0, 0.0),
                module.TelemetrySample(20.0, 19.5, 19.6, 2500.0, 2510.0, 1.0, 0.0),
                module.TelemetrySample(0.0, 0.4, 0.3, 80.0, 80.0, 0.0, 1.0),
            ]
        ]
        with_good_low_speed = [
            [
                module.TelemetrySample(8.0, 6.5, 6.7, 1800.0, 1750.0, 1.0, 0.0),
                module.TelemetrySample(8.0, 7.8, 7.9, 1900.0, 1850.0, 1.0, 0.0),
                module.TelemetrySample(20.0, 14.0, 14.2, 2300.0, 2320.0, 1.0, 0.0),
                module.TelemetrySample(20.0, 19.5, 19.6, 2500.0, 2510.0, 1.0, 0.0),
                module.TelemetrySample(0.0, 0.4, 0.3, 80.0, 80.0, 0.0, 1.0),
            ]
        ]

        bad_score = module.score_ground_stage_group(
            with_bad_low_speed,
            trials=[trial],
            min_target_speed=10.0,
        )
        good_score = module.score_ground_stage_group(
            with_good_low_speed,
            trials=[trial],
            min_target_speed=10.0,
        )

        self.assertAlmostEqual(bad_score, good_score, places=6)

    def test_build_ground_step_trials_matches_small_track_plan(self):
        module = load_module()

        trials = module.build_ground_step_trials()

        self.assertEqual(
            [(trial.name, trial.segments_ms, trial.trial_ms) for trial in trials],
            [
                (
                    "sequence_25_35_45_35_25",
                    (
                        (25.0, 200),
                        (35.0, 200),
                        (45.0, 200),
                        (35.0, 200),
                        (25.0, 200),
                    ),
                    1000,
                ),
            ],
        )

    def test_build_ground_return_trial_mirrors_distance_conservatively(self):
        module = load_module()

        forward_trial = module.SpeedStageTrial(
            "forward",
            ((25.0, 200), (35.0, 200), (45.0, 200)),
            600,
        )

        return_trial = module.build_ground_return_trial(forward_trial, speed_scale=0.6, max_speed=30.0)

        self.assertEqual(
            (return_trial.name, return_trial.segments_ms, return_trial.trial_ms),
            (
                "forward_return",
                ((-27.0, 333), (-21.0, 333), (-15.0, 333)),
                999,
            ),
        )

    def test_penalizes_failed_stop_and_nonzero_cooldown(self):
        module = load_module()

        healthy = [
            [
                module.TelemetrySample(25.0, 18.0, 18.5, 2100.0, 2120.0, 1.0, 0.0),
                module.TelemetrySample(25.0, 24.0, 24.2, 2400.0, 2380.0, 1.0, 0.0),
                module.TelemetrySample(35.0, 29.0, 29.4, 2700.0, 2690.0, 1.0, 0.0),
                module.TelemetrySample(35.0, 34.0, 34.1, 3000.0, 3010.0, 1.0, 0.0),
                module.TelemetrySample(45.0, 37.0, 36.8, 3200.0, 3210.0, 1.0, 0.0),
                module.TelemetrySample(45.0, 44.0, 44.3, 3400.0, 3390.0, 1.0, 0.0),
                module.TelemetrySample(35.0, 38.0, 37.5, 2800.0, 2810.0, 1.0, 0.0),
                module.TelemetrySample(35.0, 35.2, 35.0, 2550.0, 2560.0, 1.0, 0.0),
                module.TelemetrySample(25.0, 29.0, 28.8, 2200.0, 2190.0, 1.0, 0.0),
                module.TelemetrySample(25.0, 25.3, 25.1, 2000.0, 2010.0, 1.0, 0.0),
                module.TelemetrySample(0.0, 0.5, 0.4, 100.0, 100.0, 0.0, 1.0),
            ]
        ]
        unsafe = [
            [
                module.TelemetrySample(25.0, 18.0, 18.5, 2100.0, 2120.0, 1.0, 0.0),
                module.TelemetrySample(25.0, 21.0, 20.8, 2600.0, 2580.0, 1.0, 0.0),
                module.TelemetrySample(35.0, 30.0, 24.0, 3200.0, 2500.0, 1.0, 0.0),
                module.TelemetrySample(35.0, 32.0, 25.0, 3300.0, 2500.0, 1.0, 0.0),
                module.TelemetrySample(45.0, 32.0, 22.0, 3400.0, 2400.0, 1.0, 0.0),
                module.TelemetrySample(45.0, 34.0, 23.0, 3400.0, 2400.0, 1.0, 0.0),
                module.TelemetrySample(35.0, 30.0, 18.0, 2900.0, 1800.0, 1.0, 0.0),
                module.TelemetrySample(35.0, 28.0, 17.0, 2800.0, 1800.0, 1.0, 0.0),
                module.TelemetrySample(25.0, 24.0, 14.0, 2300.0, 1500.0, 1.0, 0.0),
                module.TelemetrySample(25.0, 22.0, 13.0, 2200.0, 1500.0, 1.0, 0.0),
                module.TelemetrySample(0.0, 8.0, 7.5, 900.0, 920.0, 0.0, 0.0),
            ]
        ]

        self.assertLess(
            module.score_ground_stage_group(healthy),
            module.score_ground_stage_group(unsafe),
        )

    def test_score_ground_stage_group_rejects_missing_trial_groups(self):
        module = load_module()
        trials = [
            module.SpeedStageTrial("t1", ((25.0, 200),), 200),
            module.SpeedStageTrial("t2", ((35.0, 200),), 200),
        ]
        groups = [
            [module.TelemetrySample(25.0, 24.5, 24.2, 2200.0, 2210.0, 0.0, 1.0)],
        ]

        self.assertEqual(
            module.score_ground_stage_group(groups, trials=trials),
            float("inf"),
        )

    def test_run_ground_stage_group_sends_expected_commands(self):
        module = load_module()

        class FakeClient(object):
            def __init__(self, sample_batches):
                self.commands = []
                self.capture_calls = []
                self.sample_batches = list(sample_batches)

            def send_command(self, command):
                self.commands.append(command)

            def drain_input(self):
                self.commands.append("DRAIN")

            def capture_trial(self, duration_seconds, events=None):
                self.capture_calls.append((duration_seconds, list(events or [])))
                return self.sample_batches.pop(0)

        sample_batches = [
            [module.TelemetrySample(45.0, 40.0, 39.5, 3300.0, 3310.0, 0.0, 1.0)],
            [module.TelemetrySample(-15.0, -14.0, -14.2, 1800.0, 1790.0, 0.0, 1.0)],
        ]
        client = FakeClient(sample_batches)
        sleep_calls = []
        return_trial = module.build_ground_return_trial(module.build_ground_step_trials()[0], speed_scale=0.6, max_speed=30.0)

        score, results = module.run_ground_stage_group(
            client,
            module.PidGains(105.0, 20.0, 0.0),
            return_trial=return_trial,
            sleep_fn=lambda seconds: sleep_calls.append(seconds),
        )

        self.assertEqual(len(results), 1)
        self.assertLess(score, float("inf"))
        self.assertEqual(sleep_calls, [0.7])
        self.assertEqual(
            client.commands,
            [
                "AT_FUYA=2000",
                "AT_COOLDOWN_MS=450",
                "AT_KP=105.0000",
                "AT_KI=20.0000",
                "AT_KD=0.0000",
                "AT_ARM",
                "AT_TRIAL_MS=1000",
                "AT_SPEED=25.0000",
                "DRAIN",
                "AT_FIRE",
                "AT_TRIAL_MS=1665",
                "AT_SPEED=-15.0000",
                "DRAIN",
                "AT_FIRE",
            ],
        )
        self.assertEqual(
            client.capture_calls[0][1],
            [
                (0.2, "AT_SPEED=35.0000"),
                (0.4, "AT_SPEED=45.0000"),
                (0.6, "AT_SPEED=35.0000"),
                (0.8, "AT_SPEED=25.0000"),
            ],
        )
        self.assertEqual(
            client.capture_calls[1][1],
            [
                (0.333, "AT_SPEED=-21.0000"),
                (0.666, "AT_SPEED=-27.0000"),
                (0.999, "AT_SPEED=-21.0000"),
                (1.332, "AT_SPEED=-15.0000"),
            ],
        )


class AutotuneModeTests(unittest.TestCase):
    def test_build_air_primary_trial_matches_airborne_plan(self):
        module = load_module()

        trial = module.build_air_primary_trial()

        self.assertEqual(
            (trial.name, trial.segments_ms, trial.trial_ms),
            (
                "air_primary_15_25_35_45_35_25_15",
                (
                    (15.0, 500),
                    (25.0, 500),
                    (35.0, 500),
                    (45.0, 500),
                    (35.0, 500),
                    (25.0, 500),
                    (15.0, 500),
                ),
                3500,
            ),
        )

    def test_build_air_verify_trial_matches_fast_plan(self):
        module = load_module()

        trial = module.build_air_verify_trial()

        self.assertEqual(
            (trial.name, trial.segments_ms, trial.trial_ms),
            (
                "air_verify_15_25_35_45_35_25_15",
                (
                    (15.0, 300),
                    (25.0, 300),
                    (35.0, 300),
                    (45.0, 300),
                    (35.0, 300),
                    (25.0, 300),
                    (15.0, 300),
                ),
                2100,
            ),
        )

    def test_build_air_primary_trial_accepts_custom_sequence(self):
        module = load_module()

        trial = module.build_air_primary_trial("20:120,35:80,50:150")

        self.assertEqual(
            (trial.name, trial.segments_ms, trial.trial_ms),
            (
                "air_primary_custom",
                ((20.0, 120), (35.0, 80), (50.0, 150)),
                350,
            ),
        )

    def test_run_air_step_trial_sends_multi_speed_sequence(self):
        module = load_module()

        class FakeClient(object):
            def __init__(self, sample_batch):
                self.commands = []
                self.capture_calls = []
                self.sample_batch = list(sample_batch)

            def send_command(self, command):
                self.commands.append(command)

            def capture_trial(self, duration_seconds, events=None):
                self.capture_calls.append((duration_seconds, list(events or [])))
                return list(self.sample_batch)

        trial = module.build_air_primary_trial("15:100,25:100,35:100")
        client = FakeClient(
            [
                module.TelemetrySample(15.0, 12.0, 12.2, 0.0, 0.0, 1.0, 0.0),
                module.TelemetrySample(25.0, 21.0, 21.2, 0.0, 0.0, 1.0, 0.0),
                module.TelemetrySample(35.0, 34.6, 34.8, 0.0, 0.0, 1.0, 0.0),
                module.TelemetrySample(0.0, 0.3, 0.2, 0.0, 0.0, 0.0, 1.0),
            ]
        )
        sleep_calls = []

        samples = module.run_air_step_trial(
            client,
            module.PidGains(105.0, 20.0, 0.0),
            trial,
            rest_seconds=0.35,
            sleep_fn=lambda seconds: sleep_calls.append(seconds),
        )

        self.assertEqual(len(samples), 4)
        self.assertEqual(sleep_calls, [0.35])
        self.assertEqual(
            client.commands,
            [
                "TEST_speed=0",
                "AT_RESET",
                "AT_KP=105.0000",
                "AT_KI=20.0000",
                "AT_KD=0.0000",
                "START",
                "TEST_speed=15.0000",
                "TEST_speed=0",
            ],
        )
        self.assertEqual(
            client.capture_calls,
            [
                (
                    0.5,
                    [
                        (0.1, "TEST_speed=25.0000"),
                        (0.2, "TEST_speed=35.0000"),
                        (0.3, "TEST_speed=0.0000"),
                    ],
                )
            ],
        )

    def test_summarize_repeat_scores_uses_median_and_consecutive_overshoot(self):
        module = load_module()

        summary = module.summarize_repeat_scores(
            [9.0, 3.0, 5.0],
            [0.10, 0.09, 0.11],
            overshoot_gate=0.08,
            required_runs=3,
        )

        self.assertEqual(summary.median_score, 5.0)
        self.assertTrue(summary.persistent_overshoot)

        summary = module.summarize_repeat_scores(
            [9.0, 3.0, 5.0],
            [0.10, 0.04, 0.11],
            overshoot_gate=0.08,
            required_runs=3,
        )

        self.assertFalse(summary.persistent_overshoot)

    def test_run_air_step_trial_accepts_independent_left_right_gains(self):
        module = load_module()

        class FakeClient(object):
            def __init__(self, sample_batch):
                self.commands = []
                self.capture_calls = []
                self.sample_batch = list(sample_batch)

            def send_command(self, command):
                self.commands.append(command)

            def capture_trial(self, duration_seconds, events=None):
                self.capture_calls.append((duration_seconds, list(events or [])))
                return list(self.sample_batch)

        trial = module.build_air_primary_trial()
        client = FakeClient(
            [
                module.TelemetrySample(15.0, 12.0, 12.2, 0.0, 0.0, 1.0, 0.0),
                module.TelemetrySample(25.0, 21.0, 21.2, 0.0, 0.0, 1.0, 0.0),
            ]
        )
        gains = module.WheelPidGains(
            module.PidGains(101.0, 21.0, 1.0),
            module.PidGains(109.0, 17.0, 0.5),
        )

        module.run_air_step_trial(
            client,
            gains,
            trial,
            rest_seconds=0.0,
        )

        self.assertEqual(
            client.commands[:9],
            [
                "TEST_speed=0",
                "AT_RESET",
                "L_KP=101.0000",
                "L_KI=21.0000",
                "L_KD=1.0000",
                "R_KP=109.0000",
                "R_KI=17.0000",
                "R_KD=0.5000",
                "START",
            ],
        )


class SearchTests(unittest.TestCase):
    def test_build_pwm_map_levels_includes_zero_and_final_max(self):
        module = load_host_package_module("pwm_map")

        self.assertEqual(module.build_pwm_map_levels(500, 10000)[0], 0)
        self.assertEqual(module.build_pwm_map_levels(500, 10000)[-1], 10000)
        self.assertEqual(module.build_pwm_map_levels(3000, 9500), [0, 3000, 6000, 9000, 9500])

    def test_extract_identify_level_metrics_estimates_step_response(self):
        module = load_module()

        samples = [
            module.TelemetrySample(0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 2.0, 600.0, 0.0),
            module.TelemetrySample(0.0, 2.0, 0.0, 600.0, 0.0, 1.0, 0.0, 2.0, 600.0, 0.0),
            module.TelemetrySample(0.0, 6.0, 0.0, 600.0, 0.0, 1.0, 0.0, 2.0, 600.0, 0.0),
            module.TelemetrySample(0.0, 10.0, 0.0, 600.0, 0.0, 1.0, 0.0, 2.0, 600.0, 0.0),
            module.TelemetrySample(0.0, 13.0, 0.0, 600.0, 0.0, 1.0, 0.0, 2.0, 600.0, 0.0),
            module.TelemetrySample(0.0, 15.0, 0.0, 600.0, 0.0, 1.0, 0.0, 2.0, 600.0, 0.0),
            module.TelemetrySample(0.0, 15.0, 0.0, 600.0, 0.0, 1.0, 0.0, 2.0, 600.0, 0.0),
            module.TelemetrySample(0.0, 15.0, 0.0, 600.0, 0.0, 1.0, 0.0, 2.0, 600.0, 0.0),
            module.TelemetrySample(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 2.0, 0.0, 0.0),
        ]

        metrics = module.extract_identify_level_metrics(samples, "left", 600)

        self.assertTrue(metrics.valid)
        self.assertAlmostEqual(metrics.steady_speed, 15.0, places=3)
        self.assertAlmostEqual(metrics.theta_s, 0.02, places=3)
        self.assertAlmostEqual(metrics.tau_s, 0.04, places=3)

    def test_build_identify_seed_from_levels_uses_simc_and_5ms_discretization(self):
        module = load_module()

        levels = [
            module.IdentifyLevelMetrics(400.0, 10.0, 0.04, 0.08, True),
            module.IdentifyLevelMetrics(800.0, 20.0, 0.04, 0.08, True),
        ]

        seed = module.build_identify_seed_from_levels(levels)

        self.assertAlmostEqual(seed.kp, 40.0, places=3)
        self.assertAlmostEqual(seed.ki, 2.5, places=3)
        self.assertEqual(seed.kd, 0.0)

    def test_run_pwm_identify_trial_issues_mode_and_pwm_commands(self):
        module = load_module()

        class FakeClient(object):
            def __init__(self):
                self.commands = []
                self.capture_calls = []
                self.drain_calls = 0

            def send_command(self, command):
                self.commands.append(command)

            def drain_input(self):
                self.drain_calls += 1

            def capture_trial(self, duration_seconds, events=None):
                self.capture_calls.append((duration_seconds, list(events or [])))
                return []

        client = FakeClient()
        module.run_pwm_identify_trial(
            client,
            "left",
            600,
            hold_ms=80,
            tail_zero_ms=40,
            rest_seconds=0.0,
            sleep_fn=lambda seconds: None,
        )

        self.assertEqual(
            client.commands[:5],
            [
                "AT_RESET",
                "AT_TEST_MODE=1",
                "L_TEST_PWM=600",
                "R_TEST_PWM=0",
                "START",
            ],
        )
        self.assertEqual(
            client.capture_calls,
            [
                (
                    0.12,
                    [
                        (0.04, "START"),
                        (0.08, "L_TEST_PWM=0"),
                        (0.08, "R_TEST_PWM=0"),
                    ],
                )
            ],
        )
        self.assertEqual(client.commands[-2:], ["AT_TEST_MODE=0", "AT_RESET"])

    def test_run_pwm_map_trial_issues_mode_and_open_loop_commands(self):
        module = load_host_package_module("pwm_map")

        class FakeClient(object):
            def __init__(self):
                self.commands = []
                self.capture_calls = []
                self.drain_calls = 0

            def send_command(self, command):
                self.commands.append(command)

            def drain_input(self):
                self.drain_calls += 1

            def capture_trial(self, duration_seconds, events=None):
                self.capture_calls.append((duration_seconds, list(events or [])))
                return []

        client = FakeClient()
        module.run_pwm_map_trial(
            client,
            "right",
            1000,
            hold_ms=120,
            tail_zero_ms=80,
            rest_seconds=0.0,
            sleep_fn=lambda seconds: None,
        )

        self.assertEqual(
            client.commands[:5],
            [
                "AT_RESET",
                "AT_TEST_MODE=1",
                "L_TEST_PWM=0",
                "R_TEST_PWM=1000",
                "START",
            ],
        )
        self.assertEqual(
            client.capture_calls,
            [
                (
                    0.2,
                    [
                        (0.04, "START"),
                        (0.08, "START"),
                        (0.12, "L_TEST_PWM=0"),
                        (0.12, "R_TEST_PWM=0"),
                    ],
                )
            ],
        )
        self.assertEqual(client.drain_calls, 1)
        self.assertEqual(client.commands[-2:], ["AT_TEST_MODE=0", "AT_RESET"])

    def test_run_pwm_map_trial_waits_for_ready_sample_before_capture(self):
        module = load_host_package_module("pwm_map")
        common = load_host_package_module("common")

        class FakeClient(object):
            def __init__(self):
                self.commands = []
                self.capture_calls = []
                self.drain_calls = 0
                self.read_calls = []
                self._batches = [
                    [common.TelemetrySample(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 2.0, 500.0, 0.0)],
                    [common.TelemetrySample(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 2.0, 500.0, 0.0)],
                ]

            def send_command(self, command):
                self.commands.append(command)

            def drain_input(self):
                self.drain_calls += 1

            def read_samples(self, duration_seconds):
                self.read_calls.append(duration_seconds)
                if self._batches:
                    return self._batches.pop(0)
                return []

            def capture_trial(self, duration_seconds, events=None):
                self.capture_calls.append((duration_seconds, list(events or [])))
                return []

        client = FakeClient()
        module.run_pwm_map_trial(
            client,
            "left",
            500,
            hold_ms=120,
            tail_zero_ms=80,
            rest_seconds=0.0,
            sleep_fn=lambda seconds: None,
        )

        self.assertEqual(len(client.read_calls), 2)
        self.assertEqual(client.commands.count("START"), 2)
        self.assertEqual(client.drain_calls, 2)
        self.assertEqual(len(client.capture_calls), 1)

    def test_run_pwm_identify_trial_keeps_pwm_above_4000(self):
        module = load_host_package_module("pwm_identify")

        class FakeClient(object):
            def __init__(self):
                self.commands = []
                self.capture_calls = []

            def send_command(self, command):
                self.commands.append(command)

            def capture_trial(self, duration_seconds, events=None):
                self.capture_calls.append((duration_seconds, list(events or [])))
                return []

        client = FakeClient()
        module.run_pwm_identify_trial(
            client,
            "left",
            6000,
            hold_ms=120,
            tail_zero_ms=80,
            rest_seconds=0.0,
            sleep_fn=lambda seconds: None,
        )

        self.assertEqual(
            client.commands[:5],
            [
                "AT_RESET",
                "AT_TEST_MODE=1",
                "L_TEST_PWM=6000",
                "R_TEST_PWM=0",
                "START",
            ],
        )

    def test_run_pwm_identify_trial_waits_for_ready_and_keeps_start_alive(self):
        module = load_host_package_module("pwm_identify")
        common = load_host_package_module("common")

        class FakeClient(object):
            def __init__(self):
                self.commands = []
                self.capture_calls = []
                self.read_calls = []
                self.drain_calls = 0
                self._batches = [
                    [],
                    [
                        common.TelemetrySample(
                            0.0,
                            0.0,
                            0.0,
                            0.0,
                            0.0,
                            1.0,
                            0.0,
                            2.0,
                            6000.0,
                            0.0,
                        )
                    ],
                ]

            def send_command(self, command):
                self.commands.append(command)

            def drain_input(self):
                self.drain_calls += 1

            def read_samples(self, duration_seconds):
                self.read_calls.append(duration_seconds)
                if self._batches:
                    return self._batches.pop(0)
                return []

            def capture_trial(self, duration_seconds, events=None):
                self.capture_calls.append((duration_seconds, list(events or [])))
                return []

        client = FakeClient()
        module.run_pwm_identify_trial(
            client,
            "left",
            6000,
            hold_ms=120,
            tail_zero_ms=80,
            rest_seconds=0.0,
            sleep_fn=lambda seconds: None,
        )

        self.assertEqual(len(client.read_calls), 2)
        self.assertEqual(client.commands.count("START"), 2)
        self.assertEqual(client.drain_calls, 1)
        self.assertEqual(len(client.capture_calls), 1)
        self.assertEqual(
            client.capture_calls[0][1],
            [
                (0.04, "START"),
                (0.08, "START"),
                (0.12, "L_TEST_PWM=0"),
                (0.12, "R_TEST_PWM=0"),
            ],
        )

    def test_run_pwm_identify_trial_preserves_ready_samples_for_step_response(self):
        module = load_host_package_module("pwm_identify")
        common = load_host_package_module("common")

        ready_sample = common.TelemetrySample(
            0.0,
            6.0,
            0.0,
            0.0,
            0.0,
            1.0,
            0.0,
            2.0,
            6000.0,
            0.0,
        )
        captured_sample = common.TelemetrySample(
            0.0,
            12.0,
            0.0,
            0.0,
            0.0,
            1.0,
            0.0,
            2.0,
            6000.0,
            0.0,
        )

        class FakeClient(object):
            def __init__(self):
                self.commands = []
                self.drain_calls = 0
                self._batches = [[ready_sample]]

            def send_command(self, command):
                self.commands.append(command)

            def drain_input(self):
                self.drain_calls += 1

            def read_samples(self, duration_seconds):
                del duration_seconds
                if self._batches:
                    return self._batches.pop(0)
                return []

            def capture_trial(self, duration_seconds, events=None):
                del duration_seconds, events
                return [captured_sample]

        client = FakeClient()
        samples = module.run_pwm_identify_trial(
            client,
            "left",
            6000,
            hold_ms=120,
            tail_zero_ms=80,
            rest_seconds=0.0,
            sleep_fn=lambda seconds: None,
        )

        self.assertEqual(client.drain_calls, 1)
        self.assertEqual(samples[0], ready_sample)
        self.assertEqual(samples[1], captured_sample)

    def test_extract_pwm_map_level_metrics_marks_deadzone_after_three_fast_samples(self):
        module = load_host_package_module("pwm_map")
        common = load_host_package_module("common")

        samples = [
            common.TelemetrySample(0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 2.0, 500.0, 0.0),
            common.TelemetrySample(0.0, 4.0, 0.0, 0.0, 0.0, 1.0, 0.0, 2.0, 500.0, 0.0),
            common.TelemetrySample(0.0, 6.0, 0.0, 0.0, 0.0, 1.0, 0.0, 2.0, 500.0, 0.0),
            common.TelemetrySample(0.0, 7.0, 0.0, 0.0, 0.0, 1.0, 0.0, 2.0, 500.0, 0.0),
            common.TelemetrySample(0.0, 8.0, 0.0, 0.0, 0.0, 1.0, 0.0, 2.0, 500.0, 0.0),
            common.TelemetrySample(0.0, 9.0, 0.0, 0.0, 0.0, 1.0, 0.0, 2.0, 500.0, 0.0),
        ]

        metrics = module.extract_pwm_map_level_metrics(samples, "left", 500)

        self.assertTrue(metrics.deadzone_reached)
        self.assertEqual(metrics.sample_count, 6)
        self.assertAlmostEqual(metrics.peak_encoder, 9.0, places=3)
        self.assertAlmostEqual(metrics.steady_encoder, 8.5, places=3)

    def test_extract_pwm_map_level_metrics_rejects_stop_flag_and_empty_capture(self):
        module = load_host_package_module("pwm_map")
        common = load_host_package_module("common")

        with self.assertRaises(RuntimeError):
            module.extract_pwm_map_level_metrics([], "left", 500)

        with self.assertRaises(RuntimeError):
            module.extract_pwm_map_level_metrics(
                [common.TelemetrySample(0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 2.0, 500.0, 0.0)],
                "left",
                500,
            )

    def test_extract_pwm_map_level_metrics_ignores_zero_pwm_transition_samples_before_start(self):
        module = load_host_package_module("pwm_map")
        common = load_host_package_module("common")

        samples = [
            common.TelemetrySample(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 2.0, 0.0, 0.0),
            common.TelemetrySample(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 2.0, 0.0, 0.0),
            common.TelemetrySample(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0),
            common.TelemetrySample(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0),
            common.TelemetrySample(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0),
        ]

        metrics = module.extract_pwm_map_level_metrics(samples, "left", 0)

        self.assertEqual(metrics.sample_count, 3)
        self.assertAlmostEqual(metrics.steady_encoder, 0.0, places=3)
        self.assertFalse(metrics.deadzone_reached)

    def test_run_pwm_map_writes_profile_with_shared_targets(self):
        module = load_module()
        pwm_map = load_host_package_module("pwm_map")
        common = load_host_package_module("common")

        original_run_pwm_map_trial = pwm_map.run_pwm_map_trial

        class FakeClient(object):
            def __init__(self):
                self.commands = []

            def send_command(self, command):
                self.commands.append(command)

        class Args(object):
            rest_seconds = 0.0
            map_pwm_step = 500
            map_pwm_max = 1000
            map_repeat = 1
            map_hold_ms = 120
            map_tail_zero_ms = 80
            map_output = ""
            profile_path = ""

        def build_samples(wheel_name, pwm_value):
            if wheel_name == "left":
                speed_map = {
                    0: [0.0, 0.0, 0.0],
                    500: [0.0, 8.0, 9.0, 10.0],
                    1000: [0.0, 18.0, 20.0, 20.0],
                }
            else:
                speed_map = {
                    0: [0.0, 0.0, 0.0],
                    500: [0.0, 7.0, 8.0, 9.0],
                    1000: [0.0, 16.0, 18.0, 18.0],
                }

            samples = []
            for speed in speed_map[pwm_value]:
                if wheel_name == "left":
                    samples.append(common.TelemetrySample(0.0, speed, 0.0, 0.0, 0.0, 1.0, 0.0, 2.0, pwm_value, 0.0))
                else:
                    samples.append(common.TelemetrySample(0.0, 0.0, speed, 0.0, 0.0, 1.0, 0.0, 2.0, 0.0, pwm_value))
            return samples

        def fake_run_pwm_map_trial(client, wheel_name, pwm_value, hold_ms, tail_zero_ms, rest_seconds, sleep_fn=None):
            del client, hold_ms, tail_zero_ms, rest_seconds, sleep_fn
            return build_samples(wheel_name, pwm_value)

        pwm_map.run_pwm_map_trial = fake_run_pwm_map_trial

        try:
            with tempfile.TemporaryDirectory() as temp_dir:
                args = Args()
                args.map_output = str(pathlib.Path(temp_dir) / "pwm_map.csv")
                args.profile_path = str(pathlib.Path(temp_dir) / "profile.json")
                result = module.run_pwm_map(FakeClient(), args)
                profile = json.loads(pathlib.Path(args.profile_path).read_text(encoding="utf-8"))
        finally:
            pwm_map.run_pwm_map_trial = original_run_pwm_map_trial

        self.assertEqual(result, 0)
        self.assertEqual(profile["pwm_map"]["left"]["deadzone_break_pwm"], 500)
        self.assertEqual(profile["pwm_map"]["right"]["deadzone_break_pwm"], 500)
        self.assertAlmostEqual(profile["pwm_map"]["left"]["max_steady_encoder"], 20.0, places=3)
        self.assertAlmostEqual(profile["pwm_map"]["right"]["max_steady_encoder"], 18.0, places=3)
        self.assertAlmostEqual(profile["shared_targets"]["shared_max_encoder"], 18.0, places=3)
        self.assertEqual(profile["shared_targets"]["policy"], "min_wheel_max")
        self.assertEqual(len(profile["shared_targets"]["default_sequences"]["air_primary"]), 5)
        self.assertEqual(len(profile["shared_targets"]["default_sequences"]["air_verify"]), 5)
        self.assertEqual(len(profile["shared_targets"]["default_sequences"]["ground_forward"]), 3)
        self.assertEqual(
            [row["target_speed"] for row in profile["shared_targets"]["default_sequences"]["air_primary"]],
            [
                profile["shared_targets"]["bands"]["low"],
                profile["shared_targets"]["bands"]["mid"],
                profile["shared_targets"]["bands"]["mid"],
                profile["shared_targets"]["bands"]["low"],
                profile["shared_targets"]["bands"]["low"],
            ],
        )

    def test_run_pwm_identify_applies_seed_when_requested(self):
        module = load_module()
        pwm_identify = load_host_package_module("pwm_identify")

        original_run_pwm_identify_trial = pwm_identify.run_pwm_identify_trial
        original_apply_speed_gains = pwm_identify.apply_speed_gains

        class FakeClient(object):
            def __init__(self):
                self.commands = []

            def send_command(self, command):
                self.commands.append(command)

        class Args(object):
            target_speed = 35.0
            rest_seconds = 0.0
            identify_pwm_step = 200
            identify_pwm_max = 800
            identify_repeat = 2
            identify_hold_ms = 160
            identify_tail_zero_ms = 40
            apply_identify_seed = True
            save_best = False
            profile_path = ""

        applied = {"gains": None}

        def build_samples(wheel_name, pwm_value):
            if pwm_value < 400:
                peak_values = [0.0, 2.0, 3.0, 4.0, 4.5, 4.0, 3.0, 2.0]
            else:
                peak_values = [
                    0.0,
                    pwm_value / 120.0,
                    pwm_value / 80.0,
                    pwm_value / 60.0,
                    pwm_value / 50.0,
                    pwm_value / 45.0,
                    pwm_value / 45.0,
                    pwm_value / 45.0,
                ]

            samples = []
            for speed in peak_values:
                if wheel_name == "left":
                    samples.append(
                        module.TelemetrySample(0.0, speed, 0.0, pwm_value, 0.0, 1.0, 0.0, 2.0, pwm_value, 0.0)
                    )
                else:
                    samples.append(
                        module.TelemetrySample(0.0, 0.0, speed, 0.0, pwm_value, 1.0, 0.0, 2.0, 0.0, pwm_value)
                    )
            return samples

        def fake_run_pwm_identify_trial(client, wheel_name, pwm_value, hold_ms, tail_zero_ms, rest_seconds, sleep_fn=None):
            del client, hold_ms, tail_zero_ms, rest_seconds, sleep_fn
            return build_samples(wheel_name, pwm_value)

        def fake_apply_speed_gains(client, gains):
            del client
            applied["gains"] = gains

        pwm_identify.run_pwm_identify_trial = fake_run_pwm_identify_trial
        pwm_identify.apply_speed_gains = fake_apply_speed_gains

        try:
            with tempfile.TemporaryDirectory() as temp_dir:
                profile_path = pathlib.Path(temp_dir) / "profile.json"
                profile_path.write_text(
                    json.dumps(
                        {
                            "meta": {"profile_version": 1},
                            "pwm_map": {
                                "left": {"deadzone_break_pwm": 200},
                                "right": {"deadzone_break_pwm": 200},
                            },
                        }
                    ),
                    encoding="utf-8",
                )
                args = Args()
                args.profile_path = str(profile_path)
                result = module.run_pwm_identify(FakeClient(), args)
        finally:
            pwm_identify.run_pwm_identify_trial = original_run_pwm_identify_trial
            pwm_identify.apply_speed_gains = original_apply_speed_gains

        self.assertEqual(result, 0)
        self.assertIsNotNone(applied["gains"])
        self.assertGreater(applied["gains"].left.kp, 0.0)
        self.assertGreater(applied["gains"].left.ki, 0.0)
        self.assertGreater(applied["gains"].right.kp, 0.0)
        self.assertGreater(applied["gains"].right.ki, 0.0)

    def test_run_pwm_identify_requires_pwm_map_profile(self):
        module = load_module()

        class FakeClient(object):
            def __init__(self):
                self.commands = []

            def send_command(self, command):
                self.commands.append(command)

        class Args(object):
            target_speed = 35.0
            rest_seconds = 0.0
            identify_pwm_step = 200
            identify_pwm_max = 1000
            identify_repeat = 1
            identify_hold_ms = 160
            identify_tail_zero_ms = 40
            apply_identify_seed = False
            save_best = False
            profile_path = ""

        with tempfile.TemporaryDirectory() as temp_dir:
            args = Args()
            args.profile_path = str(pathlib.Path(temp_dir) / "missing_profile.json")
            with self.assertRaises(RuntimeError):
                module.run_pwm_identify(FakeClient(), args)

    def test_run_pwm_identify_starts_above_profile_deadzone_and_persists_seed(self):
        module = load_module()
        pwm_identify = load_host_package_module("pwm_identify")

        original_run_pwm_identify_trial = pwm_identify.run_pwm_identify_trial

        class FakeClient(object):
            def __init__(self):
                self.commands = []

            def send_command(self, command):
                self.commands.append(command)

        class Args(object):
            target_speed = 35.0
            rest_seconds = 0.0
            identify_pwm_step = 200
            identify_pwm_max = 1400
            identify_repeat = 1
            identify_hold_ms = 160
            identify_tail_zero_ms = 40
            apply_identify_seed = False
            save_best = False
            profile_path = ""

        call_state = {"left": [], "right": []}

        def build_samples(wheel_name, pwm_value):
            speeds = [0.0, pwm_value / 120.0, pwm_value / 80.0, pwm_value / 60.0, pwm_value / 45.0, pwm_value / 45.0]
            samples = []
            for speed in speeds:
                if wheel_name == "left":
                    samples.append(
                        module.TelemetrySample(0.0, speed, 0.0, pwm_value, 0.0, 1.0, 0.0, 2.0, pwm_value, 0.0)
                    )
                else:
                    samples.append(
                        module.TelemetrySample(0.0, 0.0, speed, 0.0, pwm_value, 1.0, 0.0, 2.0, 0.0, pwm_value)
                    )
            return samples

        def fake_run_pwm_identify_trial(client, wheel_name, pwm_value, hold_ms, tail_zero_ms, rest_seconds, sleep_fn=None):
            del client, hold_ms, tail_zero_ms, rest_seconds, sleep_fn
            call_state[wheel_name].append(pwm_value)
            return build_samples(wheel_name, pwm_value)

        pwm_identify.run_pwm_identify_trial = fake_run_pwm_identify_trial

        try:
            with tempfile.TemporaryDirectory() as temp_dir:
                profile_path = pathlib.Path(temp_dir) / "profile.json"
                profile_path.write_text(
                    json.dumps(
                        {
                            "meta": {"profile_version": 1},
                            "pwm_map": {
                                "left": {"deadzone_break_pwm": 600},
                                "right": {"deadzone_break_pwm": 800},
                            },
                        }
                    ),
                    encoding="utf-8",
                )
                args = Args()
                args.profile_path = str(profile_path)
                result = module.run_pwm_identify(FakeClient(), args)
                profile = json.loads(profile_path.read_text(encoding="utf-8"))
        finally:
            pwm_identify.run_pwm_identify_trial = original_run_pwm_identify_trial

        self.assertEqual(result, 0)
        self.assertEqual(call_state["left"][0], 800)
        self.assertEqual(call_state["right"][0], 1000)
        self.assertIn("seed_pi", profile["pwm_identify"])
        self.assertGreater(profile["pwm_identify"]["seed_pi"]["left"]["kp"], 0.0)
        self.assertGreater(profile["pwm_identify"]["seed_pi"]["right"]["ki"], 0.0)

    def test_resolve_air_dual_profile_defaults_uses_sequences_and_seed(self):
        air_dual = load_host_package_module("air_dual")
        common = load_host_package_module("common")

        class Args(object):
            air_primary_sequence = air_dual.DEFAULT_AIR_PRIMARY_SEQUENCE
            air_verify_sequence = air_dual.DEFAULT_AIR_VERIFY_SEQUENCE
            initial_kp = 100.0
            initial_ki = 20.0
            initial_kd = 0.0
            profile_path = ""

        with tempfile.TemporaryDirectory() as temp_dir:
            profile_path = pathlib.Path(temp_dir) / "profile.json"
            profile_path.write_text(
                json.dumps(
                    {
                        "shared_targets": {
                            "default_sequences": {
                                "air_primary": [
                                    {"target_speed": 10.0, "hold_ms": 500},
                                    {"target_speed": 20.0, "hold_ms": 500},
                                ],
                                "air_verify": [
                                    {"target_speed": 11.0, "hold_ms": 300},
                                    {"target_speed": 21.0, "hold_ms": 300},
                                ],
                            }
                        },
                        "pwm_identify": {
                            "seed_pi": {
                                "left": {"kp": 1.1, "ki": 2.2, "kd": 0.0},
                                "right": {"kp": 3.3, "ki": 4.4, "kd": 0.0},
                            }
                        },
                    }
                ),
                encoding="utf-8",
            )
            args = Args()
            args.profile_path = str(profile_path)
            resolved = air_dual.resolve_air_dual_profile_defaults(args)

        self.assertEqual(resolved["air_primary_sequence"], "10:500,20:500")
        self.assertEqual(resolved["verify_sequence"], "11:300,21:300")
        self.assertEqual(
            resolved["initial_pair"],
            common.WheelPidGains(common.PidGains(1.1, 2.2, 0.0), common.PidGains(3.3, 4.4, 0.0)),
        )

    def test_resolve_air_dual_profile_defaults_prefers_custom_sequences(self):
        air_dual = load_host_package_module("air_dual")

        class Args(object):
            air_primary_sequence = air_dual.DEFAULT_AIR_PRIMARY_SEQUENCE
            air_verify_sequence = air_dual.DEFAULT_AIR_VERIFY_SEQUENCE
            initial_kp = 100.0
            initial_ki = 20.0
            initial_kd = 0.0
            profile_path = ""

        with tempfile.TemporaryDirectory() as temp_dir:
            profile_path = pathlib.Path(temp_dir) / "profile.json"
            profile_path.write_text(
                json.dumps(
                    {
                        "shared_targets": {
                            "default_sequences": {
                                "air_primary": [
                                    {"target_speed": 10.0, "hold_ms": 500},
                                ],
                                "air_verify": [
                                    {"target_speed": 11.0, "hold_ms": 300},
                                ],
                            },
                            "custom_sequences": {
                                "air_primary": [
                                    {"target_speed": 30.0, "hold_ms": 500},
                                    {"target_speed": 60.0, "hold_ms": 500},
                                ],
                                "air_verify": [
                                    {"target_speed": 31.0, "hold_ms": 300},
                                    {"target_speed": 61.0, "hold_ms": 300},
                                ],
                            },
                        }
                    }
                ),
                encoding="utf-8",
            )
            args = Args()
            args.profile_path = str(profile_path)
            resolved = air_dual.resolve_air_dual_profile_defaults(args)

        self.assertEqual(resolved["air_primary_sequence"], "30:500,60:500")
        self.assertEqual(resolved["verify_sequence"], "31:300,61:300")

    def test_resolve_air_dual_profile_defaults_accepts_custom_sequence_text(self):
        air_dual = load_host_package_module("air_dual")

        class Args(object):
            air_primary_sequence = air_dual.DEFAULT_AIR_PRIMARY_SEQUENCE
            air_verify_sequence = air_dual.DEFAULT_AIR_VERIFY_SEQUENCE
            initial_kp = 100.0
            initial_ki = 20.0
            initial_kd = 0.0
            profile_path = ""

        with tempfile.TemporaryDirectory() as temp_dir:
            profile_path = pathlib.Path(temp_dir) / "profile.json"
            profile_path.write_text(
                json.dumps(
                    {
                        "shared_targets": {
                            "default_sequences": {
                                "air_primary": [
                                    {"target_speed": 10.0, "hold_ms": 500},
                                ],
                                "air_verify": [
                                    {"target_speed": 11.0, "hold_ms": 300},
                                ],
                            },
                            "custom_sequences": {
                                "air_primary": "30:500,60:500,90:500",
                                "air_verify": "31:300,61:300,91:300",
                            },
                        }
                    }
                ),
                encoding="utf-8",
            )
            args = Args()
            args.profile_path = str(profile_path)
            resolved = air_dual.resolve_air_dual_profile_defaults(args)

        self.assertEqual(resolved["air_primary_sequence"], "30:500,60:500,90:500")
        self.assertEqual(resolved["verify_sequence"], "31:300,61:300,91:300")

    def test_resolve_air_dual_profile_defaults_prefers_air_best_over_seed_pi(self):
        air_dual = load_host_package_module("air_dual")
        common = load_host_package_module("common")

        class Args(object):
            air_primary_sequence = air_dual.DEFAULT_AIR_PRIMARY_SEQUENCE
            air_verify_sequence = air_dual.DEFAULT_AIR_VERIFY_SEQUENCE
            initial_kp = 100.0
            initial_ki = 20.0
            initial_kd = 0.0
            profile_path = ""

        with tempfile.TemporaryDirectory() as temp_dir:
            profile_path = pathlib.Path(temp_dir) / "profile.json"
            profile_path.write_text(
                json.dumps(
                    {
                        "air_dual": {
                            "best_pid": {
                                "left": {"kp": 101.0, "ki": 19.0, "kd": 0.0},
                                "right": {"kp": 106.0, "ki": 18.0, "kd": 0.0},
                            }
                        },
                        "pwm_identify": {
                            "seed_pi": {
                                "left": {"kp": 143.0, "ki": 35.0, "kd": 0.0},
                                "right": {"kp": 136.0, "ki": 34.0, "kd": 0.0},
                            }
                        },
                    }
                ),
                encoding="utf-8",
            )
            args = Args()
            args.profile_path = str(profile_path)
            resolved = air_dual.resolve_air_dual_profile_defaults(args)

        self.assertEqual(
            resolved["initial_pair"],
            common.WheelPidGains(
                common.PidGains(101.0, 19.0, 0.0),
                common.PidGains(106.0, 18.0, 0.0),
            ),
        )

    def test_ground_dual_profile_trials_prefer_custom_sequences(self):
        ground_dual = load_host_package_module("ground_dual")

        profile = {
            "shared_targets": {
                "default_sequences": {
                    "ground_forward": [
                        {"target_speed": 12.0, "hold_ms": 200},
                    ]
                },
                "custom_sequences": {
                    "ground_forward": [
                        {"target_speed": 42.0, "hold_ms": 250},
                        {"target_speed": 84.0, "hold_ms": 250},
                    ]
                },
            }
        }

        trials = ground_dual.build_ground_step_trials(profile)

        self.assertEqual(trials[0].segments_ms, ((42.0, 250), (84.0, 250)))

    def test_ground_dual_profile_trials_accept_custom_sequence_text(self):
        ground_dual = load_host_package_module("ground_dual")

        profile = {
            "shared_targets": {
                "default_sequences": {
                    "ground_forward": [
                        {"target_speed": 12.0, "hold_ms": 200},
                    ]
                },
                "custom_sequences": {
                    "ground_forward": "42:250,84:250,126:250",
                },
            }
        }

        trials = ground_dual.build_ground_step_trials(profile)

        self.assertEqual(trials[0].segments_ms, ((42.0, 250), (84.0, 250), (126.0, 250)))

    def test_save_tuning_profile_places_custom_sequences_first_in_shared_targets(self):
        common = load_host_package_module("common")

        with tempfile.TemporaryDirectory() as temp_dir:
            profile_path = pathlib.Path(temp_dir) / "profile.json"
            profile = {
                "meta": {"profile_version": 1},
                "shared_targets": {
                    "bands": {"low": 1.0},
                    "default_sequences": {"air_primary": [{"target_speed": 1.0, "hold_ms": 500}]},
                    "custom_sequences": {"air_primary": [{"target_speed": 2.0, "hold_ms": 500}]},
                    "policy": "min_wheel_max",
                    "shared_max_encoder": 2.0,
                },
            }

            common.save_tuning_profile(profile, str(profile_path))
            text = profile_path.read_text(encoding="utf-8")

        self.assertLess(text.index('"custom_sequences"'), text.index('"bands"'))
        self.assertLess(text.index('"custom_sequences"'), text.index('"default_sequences"'))

    def test_save_tuning_profile_places_shared_targets_near_file_top(self):
        common = load_host_package_module("common")

        with tempfile.TemporaryDirectory() as temp_dir:
            profile_path = pathlib.Path(temp_dir) / "profile.json"
            profile = {
                "meta": {"profile_version": 1},
                "air_dual": {"best_pid": {"left": {"kp": 1.0, "ki": 2.0, "kd": 0.0}, "right": {"kp": 1.0, "ki": 2.0, "kd": 0.0}}},
                "shared_targets": {
                    "custom_sequences": {"air_primary": "30:500,60:500"},
                    "bands": {"low": 1.0},
                },
                "pwm_map": {"left": {"deadzone_break_pwm": 1000}},
            }

            common.save_tuning_profile(profile, str(profile_path))
            text = profile_path.read_text(encoding="utf-8")

        self.assertLess(text.index('"shared_targets"'), text.index('"air_dual"'))
        self.assertLess(text.index('"shared_targets"'), text.index('"pwm_map"'))

    def test_run_pwm_identify_requires_two_valid_levels_per_wheel(self):
        module = load_module()
        pwm_identify = load_host_package_module("pwm_identify")

        original_run_pwm_identify_trial = pwm_identify.run_pwm_identify_trial

        class FakeClient(object):
            pass

        class Args(object):
            target_speed = 35.0
            rest_seconds = 0.0
            identify_pwm_step = 200
            identify_pwm_max = 400
            identify_repeat = 1
            identify_hold_ms = 160
            identify_tail_zero_ms = 40
            apply_identify_seed = False
            save_best = False
            profile_path = ""

        invalid_samples = [
            module.TelemetrySample(0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 2.0, 200.0, 0.0),
            module.TelemetrySample(0.0, 2.0, 0.0, 200.0, 0.0, 1.0, 0.0, 2.0, 200.0, 0.0),
            module.TelemetrySample(0.0, 4.0, 0.0, 200.0, 0.0, 1.0, 0.0, 2.0, 200.0, 0.0),
        ]
        valid_samples = [
            module.TelemetrySample(0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 2.0, 400.0, 0.0),
            module.TelemetrySample(0.0, 6.0, 0.0, 400.0, 0.0, 1.0, 0.0, 2.0, 400.0, 0.0),
            module.TelemetrySample(0.0, 9.0, 0.0, 400.0, 0.0, 1.0, 0.0, 2.0, 400.0, 0.0),
            module.TelemetrySample(0.0, 10.0, 0.0, 400.0, 0.0, 1.0, 0.0, 2.0, 400.0, 0.0),
            module.TelemetrySample(0.0, 10.0, 0.0, 400.0, 0.0, 1.0, 0.0, 2.0, 400.0, 0.0),
        ]

        def fake_run_pwm_identify_trial(client, wheel_name, pwm_value, hold_ms, tail_zero_ms, rest_seconds, sleep_fn=None):
            del client, hold_ms, tail_zero_ms, rest_seconds, sleep_fn
            if wheel_name == "left" and pwm_value == 400:
                return list(valid_samples)
            return list(invalid_samples)

        pwm_identify.run_pwm_identify_trial = fake_run_pwm_identify_trial

        try:
            with tempfile.TemporaryDirectory() as temp_dir:
                profile_path = pathlib.Path(temp_dir) / "profile.json"
                profile_path.write_text(
                    json.dumps(
                        {
                            "meta": {"profile_version": 1},
                            "pwm_map": {
                                "left": {"deadzone_break_pwm": 200},
                                "right": {"deadzone_break_pwm": 200},
                            },
                        }
                    ),
                    encoding="utf-8",
                )
                args = Args()
                args.profile_path = str(profile_path)
                with self.assertRaises(RuntimeError):
                    module.run_pwm_identify(FakeClient(), args)
        finally:
            pwm_identify.run_pwm_identify_trial = original_run_pwm_identify_trial

    def test_main_routes_pwm_identify_mode(self):
        module = load_module()

        original_detect_port = module.detect_port
        original_serial_client = module.VofaSerialClient
        original_run_pwm_identify = module.run_pwm_identify

        call_state = {"called": 0}

        class FakeClient(object):
            def __init__(self, port, baudrate, timeout):
                self.port = port
                self.baudrate = baudrate
                self.timeout = timeout

            def close(self):
                pass

        def fake_detect_port(preferred=None):
            del preferred
            return "COM8"

        def fake_run_pwm_identify(client, args):
            del args
            self.assertEqual(client.port, "COM8")
            call_state["called"] += 1
            return 0

        module.detect_port = fake_detect_port
        module.VofaSerialClient = FakeClient
        module.run_pwm_identify = fake_run_pwm_identify

        try:
            result = module.main(["--mode", "pwm-identify"])
        finally:
            module.detect_port = original_detect_port
            module.VofaSerialClient = original_serial_client
            module.run_pwm_identify = original_run_pwm_identify

        self.assertEqual(result, 0)
        self.assertEqual(call_state["called"], 1)

    def test_main_rejects_save_best_for_pwm_identify(self):
        module = load_module()
        stderr = io.StringIO()

        with contextlib.redirect_stderr(stderr):
            result = module.main(["--mode", "pwm-identify", "--save-best"])

        self.assertEqual(result, 2)
        self.assertIn("pwm-identify does not support --save-best", stderr.getvalue())

    def test_run_pwm_map_writes_low_pwm_rows_and_single_deadzone_break(self):
        module = load_module()
        pwm_map = load_host_package_module("pwm_map")
        common = load_host_package_module("common")

        original_run_pwm_map_trial = pwm_map.run_pwm_map_trial

        class FakeClient(object):
            pass

        class Args(object):
            rest_seconds = 0.0
            map_pwm_step = 500
            map_pwm_max = 1000
            map_repeat = 2
            map_hold_ms = 120
            map_tail_zero_ms = 80
            map_output = ""
            profile_path = ""

        def build_samples(wheel_name, pwm_value):
            speeds = [0.0, 2.0, 3.0, 4.0]
            if pwm_value >= 500:
                speeds = [0.0, 6.0, 7.0, 8.0, 8.0]
            samples = []
            for speed in speeds:
                if wheel_name == "left":
                    samples.append(common.TelemetrySample(0.0, speed, 0.0, 0.0, 0.0, 1.0, 0.0, 2.0, pwm_value, 0.0))
                else:
                    samples.append(common.TelemetrySample(0.0, 0.0, speed, 0.0, 0.0, 1.0, 0.0, 2.0, 0.0, pwm_value))
            return samples

        def fake_run_pwm_map_trial(client, wheel_name, pwm_value, hold_ms, tail_zero_ms, rest_seconds, sleep_fn=None):
            del client, hold_ms, tail_zero_ms, rest_seconds, sleep_fn
            return build_samples(wheel_name, pwm_value)

        pwm_map.run_pwm_map_trial = fake_run_pwm_map_trial

        try:
            with tempfile.TemporaryDirectory() as temp_dir:
                args = Args()
                args.map_output = str(pathlib.Path(temp_dir) / "pwm_map.csv")
                args.profile_path = str(pathlib.Path(temp_dir) / "profile.json")
                stdout = io.StringIO()
                with contextlib.redirect_stdout(stdout):
                    result = module.run_pwm_map(FakeClient(), args)
                csv_lines = pathlib.Path(args.map_output).read_text(encoding="utf-8").strip().splitlines()
        finally:
            pwm_map.run_pwm_map_trial = original_run_pwm_map_trial

        self.assertEqual(result, 0)
        self.assertEqual(len(csv_lines), 7)
        self.assertIn("deadzone_break_pwm", csv_lines[0])
        self.assertIn(",left,0,", csv_lines[1])
        self.assertIn(",left,500,", csv_lines[2])
        self.assertTrue(any("left deadzone_break_pwm=500" in line for line in stdout.getvalue().splitlines()))
        self.assertEqual(sum([1 for line in csv_lines[1:] if ",left," in line and line.endswith(",500")]), 1)

    def test_run_pwm_map_stops_at_effective_pwm_limit_without_duplicate_rows(self):
        module = load_module()
        pwm_map = load_host_package_module("pwm_map")
        common = load_host_package_module("common")

        original_run_pwm_map_trial = pwm_map.run_pwm_map_trial
        class FakeClient(object):
            pass

        class TrialResult(object):
            def __init__(self, samples, effective_pwm):
                self.samples = samples
                self.effective_pwm = effective_pwm

        class Args(object):
            rest_seconds = 0.0
            map_pwm_step = 500
            map_pwm_max = 1500
            map_repeat = 1
            map_hold_ms = 120
            map_tail_zero_ms = 80
            map_output = ""
            profile_path = ""

        def build_samples(wheel_name, effective_pwm):
            speeds = [0.0, 0.0, 0.0]
            if effective_pwm >= 500:
                speeds = [0.0, 6.0, 7.0, 8.0, 8.0]
            samples = []
            for speed in speeds:
                if wheel_name == "left":
                    samples.append(
                        common.TelemetrySample(0.0, speed, 0.0, effective_pwm, 0.0, 1.0, 0.0, 2.0, effective_pwm, 0.0)
                    )
                else:
                    samples.append(
                        common.TelemetrySample(0.0, 0.0, speed, 0.0, effective_pwm, 1.0, 0.0, 2.0, 0.0, effective_pwm)
                    )
            return samples

        def fake_run_pwm_map_trial(client, wheel_name, pwm_value, hold_ms, tail_zero_ms, rest_seconds, sleep_fn=None):
            effective_pwm = pwm_value
            del client, hold_ms, tail_zero_ms, rest_seconds, sleep_fn
            if pwm_value >= 1500:
                effective_pwm = 1000
            return TrialResult(build_samples(wheel_name, effective_pwm), effective_pwm)

        pwm_map.run_pwm_map_trial = fake_run_pwm_map_trial

        try:
            with tempfile.TemporaryDirectory() as temp_dir:
                args = Args()
                args.map_output = str(pathlib.Path(temp_dir) / "pwm_map_limit.csv")
                args.profile_path = str(pathlib.Path(temp_dir) / "profile.json")
                stdout = io.StringIO()
                with contextlib.redirect_stdout(stdout):
                    result = module.run_pwm_map(FakeClient(), args)
                csv_lines = pathlib.Path(args.map_output).read_text(encoding="utf-8").strip().splitlines()
        finally:
            pwm_map.run_pwm_map_trial = original_run_pwm_map_trial

        self.assertEqual(result, 0)
        self.assertEqual(len(csv_lines), 7)
        self.assertFalse(any(",1500," in line for line in csv_lines[1:]))
        self.assertEqual(sum([1 for line in csv_lines[1:] if ",left,1000," in line]), 1)
        self.assertEqual(sum([1 for line in csv_lines[1:] if ",right,1000," in line]), 1)
        self.assertIn("left pwm clamp requested=1500 effective=1000", stdout.getvalue())
        self.assertIn("right pwm clamp requested=1500 effective=1000", stdout.getvalue())

    def test_main_routes_pwm_map_mode(self):
        module = load_module()

        original_detect_port = module.detect_port
        original_serial_client = module.VofaSerialClient
        original_run_pwm_map = module.run_pwm_map

        call_state = {"called": 0}

        class FakeClient(object):
            def __init__(self, port, baudrate, timeout):
                self.port = port
                self.baudrate = baudrate
                self.timeout = timeout

            def close(self):
                pass

        def fake_detect_port(preferred=None):
            del preferred
            return "COM8"

        def fake_run_pwm_map(client, args):
            del args
            self.assertEqual(client.port, "COM8")
            call_state["called"] += 1
            return 0

        module.detect_port = fake_detect_port
        module.VofaSerialClient = FakeClient
        module.run_pwm_map = fake_run_pwm_map

        try:
            result = module.main(["--mode", "pwm-map"])
        finally:
            module.detect_port = original_detect_port
            module.VofaSerialClient = original_serial_client
            module.run_pwm_map = original_run_pwm_map

        self.assertEqual(result, 0)
        self.assertEqual(call_state["called"], 1)

    def test_main_rejects_invalid_pwm_map_arguments(self):
        module = load_module()
        stderr = io.StringIO()

        with contextlib.redirect_stderr(stderr):
            result = module.main(["--mode", "pwm-map", "--map-pwm-step", "0"])

        self.assertEqual(result, 2)
        self.assertIn("pwm-map requires --map-pwm-step > 0", stderr.getvalue())

    def test_firmware_open_loop_pwm_clamp_uses_pwm_duty_max(self):
        source = (
            MODULE_PATH.parents[1] / "firmware" / "autotune_runtime.c"
        ).read_text(encoding="utf-8")

        self.assertIn("PWM_DUTY_MAX", source)
        self.assertNotIn("value > 4000.0f", source)

    def test_twiddle_moves_toward_quadratic_minimum(self):
        module = load_module()

        initial = module.PidGains(60.0, 20.0, 0.0)
        deltas = module.PidGains(20.0, 10.0, 5.0)

        def evaluator(gains):
            return (
                (gains.kp - 100.0) ** 2
                + (gains.ki - 40.0) ** 2
                + (gains.kd - 10.0) ** 2
            )

        best, score = module.twiddle_optimize(
            initial,
            deltas,
            evaluator,
            iterations=12,
            tolerance=0.05,
        )

        self.assertLess(score, evaluator(initial))
        self.assertAlmostEqual(best.kp, 100.0, delta=25.0)
        self.assertAlmostEqual(best.ki, 40.0, delta=15.0)
        self.assertAlmostEqual(best.kd, 10.0, delta=10.0)


if __name__ == "__main__":
    unittest.main()
