import importlib.util
import pathlib
import unittest


MODULE_PATH = pathlib.Path(__file__).resolve().parents[1] / "tools" / "vofa_autotune.py"


def load_module():
    spec = importlib.util.spec_from_file_location("vofa_autotune", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError("Unable to load tools/vofa_autotune.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


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


class GroundLoadTests(unittest.TestCase):
    def test_build_ground_load_trials_matches_small_track_plan(self):
        module = load_module()

        trials = module.build_ground_load_trials()

        self.assertEqual(
            [(trial.name, trial.segments_ms, trial.trial_ms) for trial in trials],
            [
                ("sequence_25_35_45_35_25", ((25.0, 200), (35.0, 200), (45.0, 200), (35.0, 200), (25.0, 200)), 1000),
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


class SearchTests(unittest.TestCase):
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
