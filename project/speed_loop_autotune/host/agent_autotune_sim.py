import argparse
import json
import pathlib
import tempfile

from . import agent_autotune, common, decision_heuristic, vofa_autotune


def _pid_pair(kp, ki, kd=0.0):
    return {
        "left": {"kp": float(kp), "ki": float(ki), "kd": float(kd)},
        "right": {"kp": float(kp), "ki": float(ki), "kd": float(kd)},
    }


class SimClient(object):
    def __init__(self, port_name):
        self.port = port_name

    def close(self):
        return None


def build_argument_parser():
    parser = agent_autotune.build_argument_parser()
    parser.description = "Simulated speed-loop agent bridge runner"
    parser.add_argument(
        "--sim-root",
        default="",
        help="Optional output root for simulated logs. Defaults to a temporary directory.",
    )
    parser.add_argument(
        "--keep-temp",
        action="store_true",
        help="Keep the temporary simulation directory when --sim-root is not provided.",
    )
    parser.add_argument(
        "--auto-sim-agent",
        action="store_true",
        help="Auto-submit decisions from a simulated agent so the script can run a full 10-round mock batch.",
    )
    parser.add_argument(
        "--sim-port",
        default="SIM-COM",
        help="Displayed port name used by the simulated client.",
    )
    return parser


def _make_stage_runner(root_path):
    def _runner(_client, run_args, mode_name, _context):
        profile = common.load_tuning_profile(str(run_args.profile_path), required=False)

        if mode_name == common.MODE_PWM_MAP:
            profile["shared_targets"] = {
                "bands": {"low": 15.0, "mid": 25.0, "high": 35.0, "top": 45.0},
                "default_sequences": {
                    "air_primary": [{"target_speed": 15.0, "hold_ms": 300}],
                    "air_verify": [{"target_speed": 15.0, "hold_ms": 200}],
                    "ground_forward": [{"target_speed": 25.0, "hold_ms": 200}],
                },
            }
            profile["pwm_map"] = {
                "raw_csv_path": str(root_path / "logs" / "sim_pwm_map.csv"),
                "left": {"deadzone_break_pwm": 1800, "max_steady_encoder": 96.0},
                "right": {"deadzone_break_pwm": 1850, "max_steady_encoder": 99.0},
            }
            common.save_tuning_profile(profile, str(run_args.profile_path))
            return {"mode": mode_name}

        if mode_name == common.MODE_PWM_IDENTIFY:
            profile["pwm_identify"] = {
                "seed_pi": _pid_pair(90.0, 18.0),
            }
            common.save_tuning_profile(profile, str(run_args.profile_path))
            return {"mode": mode_name}

        candidate = common.load_json_dict(run_args.candidate_json)
        baseline = common.load_json_dict(run_args.baseline_json)
        round_index = int(run_args.round_index)
        round_score = 40.0 - float(round_index)
        result = {
            "mode": mode_name,
            "batch_id": run_args.batch_id,
            "round_index": round_index,
            "candidate_pid": candidate,
            "baseline_pid": baseline,
            "a_score": round_score,
            "b_score": round_score + 0.2,
            "combined_score": round_score,
            "band_scores": {"low": round_score, "mid": round_score, "high": round_score, "top": round_score},
            "stage_reached": "step_completed",
            "overshoot_flag": 0,
            "persistent_overshoot_flag": 0,
            "speed_drop_flag": 0,
            "stop_clean_flag": 1,
            "pwm_saturation_ratio": 0.0,
            "waveform_path": pathlib.Path(run_args.waveform_path).as_posix(),
            "waveform_digest": {"tail_jitter": 0.1, "peak_windows": [{"run_index": 1, "peak_speed": 25.0}]},
            "result_path": pathlib.Path(run_args.result_json).as_posix(),
            "timestamp": "2026-04-07 22:30:00",
        }
        if mode_name == common.MODE_AIR_DUAL_STEP:
            result["left_score"] = round_score
            result["right_score"] = round_score + 0.4
        else:
            result["trial_name"] = "ground_forward"
            result["segments_ms"] = [[15.0, 200], [25.0, 200]]
        pathlib.Path(run_args.waveform_path).parent.mkdir(parents=True, exist_ok=True)
        pathlib.Path(run_args.waveform_path).write_text('{"sample_index": 0}\n', encoding="utf-8")
        common.write_json_file(run_args.result_json, result)
        return result

    return _runner


def _simulate_agent_decision(request_payload):
    return json.dumps(
        decision_heuristic.build_llm_decision(request_payload),
        ensure_ascii=False,
    )


def run_simulation(args):
    temp_dir = None
    if args.sim_root:
        root_path = pathlib.Path(args.sim_root)
        root_path.mkdir(parents=True, exist_ok=True)
    else:
        temp_dir = tempfile.TemporaryDirectory()
        root_path = pathlib.Path(temp_dir.name)

    try:
        profile_path = root_path / "logs" / "current_tuning_profile.json"
        args.profile_path = str(profile_path)
        sim_client = SimClient(args.sim_port)
        stage_runner = _make_stage_runner(root_path)

        summary = agent_autotune.run_agent_autotune(
            sim_client,
            args,
            explicit_action=args.action,
            requested_stage=args.requested_stage,
            decision_builder=decision_heuristic.build_llm_decision if args.auto_sim_agent else None,
            raw_agent_output=args.submit_raw_json,
            request_id=args.request_id,
            stage_runner=stage_runner,
        )
        summary["simulation_root"] = str(root_path)
        summary["simulation_kept"] = bool(args.sim_root or args.keep_temp)
        return summary
    finally:
        if temp_dir is not None and not args.keep_temp:
            temp_dir.cleanup()


def main(argv=None):
    parser = build_argument_parser()
    args = parser.parse_args(list(argv) if argv is not None else None)
    args.mode = common.normalize_mode_name(args.mode)
    summary = run_simulation(args)

    if not args.json and summary.get("final_workflow_status") == "waiting_user":
        text = agent_autotune.format_waiting_user_actions(summary)
        if text:
            print(text)
        return 0

    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0

