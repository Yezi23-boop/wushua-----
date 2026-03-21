import contextlib
import importlib
import importlib.util
import io
import pathlib
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

        self.assertIsNotNone(three)
        self.assertIsNotNone(four)
        self.assertIsNotNone(seven)
        self.assertEqual(three.target, 12.5)
        self.assertEqual(three.left_speed, 10.0)
        self.assertEqual(three.right_speed, 11.0)
        self.assertEqual(four.left_pwm, 0.0)
        self.assertEqual(seven.left_pwm, 3200.0)
        self.assertEqual(seven.right_pwm, 3180.0)
        self.assertEqual(seven.trial_active, 1.0)
        self.assertEqual(seven.stop_flag, 0.0)

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
        trial = module.build_autotune_trial("20:200,40:200")

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
        trial = module.build_autotune_trial("20:200,40:200")

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

        self.assertEqual(args.mode, "air-dual")
        self.assertEqual(args.delta_kp, 10.0)
        self.assertEqual(args.delta_ki, 5.0)
        self.assertEqual(args.delta_kd, 0.5)
        self.assertEqual(args.search_tolerance, 1.0)
        self.assertEqual(args.autotune_sequence, module.DEFAULT_AUTOTUNE_SEQUENCE)
        self.assertEqual(args.autotune_verify_sequence, module.DEFAULT_AUTOTUNE_VERIFY_SEQUENCE)

    def test_normalize_mode_name_supports_explicit_and_legacy_labels(self):
        module = load_module()

        self.assertEqual(module.normalize_mode_name("air-dual"), "air-dual")
        self.assertEqual(module.normalize_mode_name("autotune"), "air-dual")
        self.assertEqual(module.normalize_mode_name("ground-dual"), "ground-dual")
        self.assertEqual(module.normalize_mode_name("ground-load"), "ground-dual")

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


class ModuleSplitTests(unittest.TestCase):
    def test_split_host_modules_can_be_imported_individually(self):
        common = load_host_package_module("common")
        air_dual = load_host_package_module("air_dual")
        ground_dual = load_host_package_module("ground_dual")

        self.assertEqual(common.MODE_AIR_DUAL, "air-dual")
        self.assertEqual(air_dual.DEFAULT_AUTOTUNE_SEQUENCE.count(","), 6)
        self.assertTrue(hasattr(ground_dual, "run_ground_dual_autotune"))

    def test_top_level_module_reexports_split_host_entries(self):
        module = load_module()
        common = load_host_package_module("common")
        air_dual = load_host_package_module("air_dual")
        ground_dual = load_host_package_module("ground_dual")

        self.assertIs(module.normalize_mode_name, common.normalize_mode_name)
        self.assertIs(module.run_air_dual_autotune, air_dual.run_air_dual_autotune)
        self.assertIs(module.run_ground_dual_autotune, ground_dual.run_ground_dual_autotune)


class GroundLoadTests(unittest.TestCase):
    def test_combine_multi_speed_scores_prioritizes_worst_segment(self):
        module = load_module()

        balanced = module.combine_multi_speed_scores([1.0, 1.0])
        spiky = module.combine_multi_speed_scores([0.2, 1.5])

        self.assertLess(balanced, spiky)

    def test_low_speed_segments_can_be_ignored_from_group_score(self):
        module = load_module()

        trial = module.GroundLoadTrial(
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

        bad_score = module.score_ground_load_group(
            with_bad_low_speed,
            trials=[trial],
            min_target_speed=10.0,
        )
        good_score = module.score_ground_load_group(
            with_good_low_speed,
            trials=[trial],
            min_target_speed=10.0,
        )

        self.assertAlmostEqual(bad_score, good_score, places=6)

    def test_build_ground_load_trials_matches_small_track_plan(self):
        module = load_module()

        trials = module.build_ground_load_trials()

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

    def test_build_ground_load_return_trial_mirrors_distance_conservatively(self):
        module = load_module()

        forward_trial = module.GroundLoadTrial(
            "forward",
            ((25.0, 200), (35.0, 200), (45.0, 200)),
            600,
        )

        return_trial = module.build_ground_load_return_trial(forward_trial, speed_scale=0.6, max_speed=30.0)

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
            module.score_ground_load_group(healthy),
            module.score_ground_load_group(unsafe),
        )

    def test_score_ground_load_group_rejects_missing_trial_groups(self):
        module = load_module()
        trials = [
            module.GroundLoadTrial("t1", ((25.0, 200),), 200),
            module.GroundLoadTrial("t2", ((35.0, 200),), 200),
        ]
        groups = [
            [module.TelemetrySample(25.0, 24.5, 24.2, 2200.0, 2210.0, 0.0, 1.0)],
        ]

        self.assertEqual(
            module.score_ground_load_group(groups, trials=trials),
            float("inf"),
        )

    def test_run_ground_load_group_sends_expected_commands(self):
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
        prompts = []
        sleep_calls = []
        return_trial = module.build_ground_load_return_trial(module.build_ground_load_trials()[0], speed_scale=0.6, max_speed=30.0)

        score, results = module.run_ground_load_group(
            client,
            module.PidGains(105.0, 20.0, 0.0),
            wait_for_operator=lambda message: prompts.append(message),
            return_trial=return_trial,
            sleep_fn=lambda seconds: sleep_calls.append(seconds),
        )

        self.assertEqual(len(prompts), 1)
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
    def test_build_autotune_trial_matches_airborne_plan(self):
        module = load_module()

        trial = module.build_autotune_trial()

        self.assertEqual(
            (trial.name, trial.segments_ms, trial.trial_ms),
            (
                "autotune_15_25_35_45_35_25_15",
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

    def test_build_autotune_verify_trial_matches_fast_plan(self):
        module = load_module()

        trial = module.build_autotune_verify_trial()

        self.assertEqual(
            (trial.name, trial.segments_ms, trial.trial_ms),
            (
                "autotune_verify_15_25_35_45_35_25_15",
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

    def test_build_autotune_trial_accepts_custom_sequence(self):
        module = load_module()

        trial = module.build_autotune_trial("20:120,35:80,50:150")

        self.assertEqual(
            (trial.name, trial.segments_ms, trial.trial_ms),
            (
                "autotune_custom",
                ((20.0, 120), (35.0, 80), (50.0, 150)),
                350,
            ),
        )

    def test_run_autotune_trial_sends_multi_speed_sequence(self):
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

        trial = module.build_autotune_trial("15:100,25:100,35:100")
        client = FakeClient(
            [
                module.TelemetrySample(15.0, 12.0, 12.2, 0.0, 0.0, 1.0, 0.0),
                module.TelemetrySample(25.0, 21.0, 21.2, 0.0, 0.0, 1.0, 0.0),
                module.TelemetrySample(35.0, 34.6, 34.8, 0.0, 0.0, 1.0, 0.0),
                module.TelemetrySample(0.0, 0.3, 0.2, 0.0, 0.0, 0.0, 1.0),
            ]
        )
        sleep_calls = []

        samples = module.run_autotune_trial(
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

    def test_build_isolated_wheel_pair_zeroes_the_other_side(self):
        module = load_module()
        candidate = module.PidGains(101.0, 21.0, 0.0)

        left_pair = module.build_isolated_wheel_pair(candidate, "left")
        right_pair = module.build_isolated_wheel_pair(candidate, "right")

        self.assertEqual(left_pair.left, candidate)
        self.assertEqual(left_pair.right, module.PidGains(0.0, 0.0, 0.0))
        self.assertEqual(right_pair.left, module.PidGains(0.0, 0.0, 0.0))
        self.assertEqual(right_pair.right, candidate)

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

    def test_run_autotune_trial_accepts_independent_left_right_gains(self):
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

        trial = module.build_autotune_trial()
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

        module.run_autotune_trial(
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
    def test_run_autotune_reports_worst_right_verification_score(self):
        module = load_module()
        air_dual = load_host_package_module("air_dual")

        original_run_autotune_trial = air_dual.run_autotune_trial
        original_optimize_single_wheel = air_dual._optimize_single_wheel
        original_optimize_dual_pair = air_dual._optimize_dual_pair
        original_score_wheel_multi_speed_trial = air_dual.score_wheel_multi_speed_trial
        original_apply_speed_gains = air_dual.apply_speed_gains

        main_samples = [module.TelemetrySample(15.0, 15.0, 15.0, 0.0, 0.0, 0.0, 1.0)]
        verify_samples = [module.TelemetrySample(15.0, 14.0, 12.0, 0.0, 0.0, 0.0, 1.0)]
        call_state = {"count": 0}

        class FakeClient(object):
            def __init__(self):
                self.commands = []

            def send_command(self, command):
                self.commands.append(command)

        class Args(object):
            autotune_sequence = module.DEFAULT_AUTOTUNE_SEQUENCE
            autotune_verify_sequence = module.DEFAULT_AUTOTUNE_VERIFY_SEQUENCE
            initial_kp = 100.0
            initial_ki = 20.0
            initial_kd = 0.0
            rest_seconds = 0.0
            autotune_tail_zero_ms = 0
            score_min_target_speed = module.DEFAULT_MIN_SCORE_TARGET_SPEED
            score_rise_weight = module.DEFAULT_SCORE_CONFIG.rise_weight
            score_overshoot_weight = module.DEFAULT_SCORE_CONFIG.overshoot_weight
            score_settle_weight = module.DEFAULT_SCORE_CONFIG.settle_weight
            score_steady_weight = module.DEFAULT_SCORE_CONFIG.steady_weight
            score_overshoot_gate = module.DEFAULT_SCORE_CONFIG.overshoot_gate
            score_overshoot_gate_penalty = module.DEFAULT_SCORE_CONFIG.overshoot_gate_penalty
            save_best = False

        best_pair = module.WheelPidGains(
            module.PidGains(101.0, 21.0, 0.0),
            module.PidGains(106.0, 18.0, 0.0),
        )

        def fake_run_autotune_trial(client, gains, trial, rest_seconds, tail_zero_ms=0, sleep_fn=None):
            del client, gains, trial, rest_seconds, tail_zero_ms, sleep_fn
            call_state["count"] += 1
            if call_state["count"] == 1:
                return main_samples
            return verify_samples

        def fake_optimize_single_wheel(client, args, trial, score_config, wheel_name, fixed_pair):
            del client, args, trial, score_config, fixed_pair
            if wheel_name == "left":
                return best_pair.left, 2.0
            return best_pair.right, 3.0

        def fake_optimize_dual_pair(client, args, trial, score_config, base_pair):
            del client, args, trial, score_config
            return base_pair, 1.0

        def fake_score_wheel_multi_speed_trial(samples, trial, wheel_name, score_config=None, min_target_speed=None):
            del trial, score_config, min_target_speed
            if samples is verify_samples and wheel_name == "right":
                return 9.0
            if samples is main_samples and wheel_name == "right":
                return 3.0
            if samples is verify_samples and wheel_name == "left":
                return 4.0
            return 2.0

        def fake_apply_speed_gains(client, gains):
            del gains
            client.send_command("APPLY")

        air_dual.run_autotune_trial = fake_run_autotune_trial
        air_dual._optimize_single_wheel = fake_optimize_single_wheel
        air_dual._optimize_dual_pair = fake_optimize_dual_pair
        air_dual.score_wheel_multi_speed_trial = fake_score_wheel_multi_speed_trial
        air_dual.apply_speed_gains = fake_apply_speed_gains

        try:
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = module.run_autotune(FakeClient(), Args())
        finally:
            air_dual.run_autotune_trial = original_run_autotune_trial
            air_dual._optimize_single_wheel = original_optimize_single_wheel
            air_dual._optimize_dual_pair = original_optimize_dual_pair
            air_dual.score_wheel_multi_speed_trial = original_score_wheel_multi_speed_trial
            air_dual.apply_speed_gains = original_apply_speed_gains

        self.assertEqual(result, 0)
        self.assertIn("best right kp=106.0000 ki=18.0000 kd=0.0000 score=9.0000", output.getvalue())

    def test_twiddle_optimize_dual_pair_moves_toward_joint_minimum(self):
        module = load_module()

        initial_pair = module.WheelPidGains(
            module.PidGains(90.0, 10.0, 0.0),
            module.PidGains(110.0, 30.0, 0.0),
        )
        delta_pair = module.WheelPidGains(
            module.PidGains(10.0, 5.0, 0.0),
            module.PidGains(10.0, 5.0, 0.0),
        )

        def evaluator(gains):
            return (
                (gains.left.kp - 100.0) ** 2
                + (gains.left.ki - 20.0) ** 2
                + (gains.right.kp - 105.0) ** 2
                + (gains.right.ki - 18.0) ** 2
            )

        best, score = module.twiddle_optimize_dual_pair(
            initial_pair,
            delta_pair,
            evaluator,
            iterations=10,
            tolerance=1.0,
        )

        self.assertLess(score, evaluator(initial_pair))
        self.assertAlmostEqual(best.left.kp, 100.0, delta=10.0)
        self.assertAlmostEqual(best.left.ki, 20.0, delta=5.0)
        self.assertAlmostEqual(best.right.kp, 105.0, delta=10.0)
        self.assertAlmostEqual(best.right.ki, 18.0, delta=5.0)

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
