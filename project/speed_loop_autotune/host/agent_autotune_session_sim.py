import sys
import argparse
import json
import pathlib

from . import agent_autotune, agent_autotune_sim, common


LAST_SUMMARY_FILENAME = "last_bridge_summary.json"
LAST_REQUEST_FILENAME = "last_decision_request.json"


def build_argument_parser():
    parser = argparse.ArgumentParser(description="Step-by-step simulated agent bridge runner")
    parser.add_argument(
        "--sim-root",
        required=True,
        help="Persistent simulation root used across start and submit calls.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Print the full JSON summary.",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    start_parser = subparsers.add_parser("start", help="Start or resume until the next decision boundary.")
    start_parser.add_argument(
        "action",
        nargs="?",
        default="",
        help="Optional batch-boundary action word such as continue_air.",
    )
    start_parser.add_argument(
        "--requested-stage",
        choices=("air_dual", "ground_dual"),
        default="",
        help="Optional explicit stage override.",
    )

    submit_parser = subparsers.add_parser("submit", help="Submit one raw agent decision and advance one round.")
    submit_parser.add_argument(
        "--request-id",
        default="",
        help="Optional request_id. Defaults to the current pending request.",
    )
    submit_parser.add_argument(
        "--raw-json",
        default="",
        help="Raw agent JSON text.",
    )
    submit_parser.add_argument(
        "--raw-file",
        default="",
        help="Path to a file containing raw agent JSON.",
    )

    return parser


def _normalize_global_flags(argv_items):
    normalized = list(argv_items)
    if "--json" in normalized:
        normalized = [item for item in normalized if item != "--json"]
        normalized.insert(0, "--json")
    return normalized


def _load_raw_agent_output(raw_json, raw_file):
    if raw_json:
        return raw_json
    if not raw_file:
        return ""
    try:
        return pathlib.Path(raw_file).read_text(encoding="utf-8")
    except OSError as exc:
        raise RuntimeError("Unable to read raw agent JSON file: {0}".format(raw_file)) from exc


def _prepare_sim_args(root_path):
    sim_args = agent_autotune_sim.build_argument_parser().parse_args(
        [
            "--sim-root",
            str(root_path),
        ]
    )
    sim_args.mode = common.normalize_mode_name(sim_args.mode)
    sim_args.profile_path = str(root_path / "logs" / "current_tuning_profile.json")
    return sim_args


def _save_summary_files(root_path, summary):
    common.write_json_file(root_path / LAST_SUMMARY_FILENAME, summary)
    if summary.get("decision_request") is not None:
        common.write_json_file(root_path / LAST_REQUEST_FILENAME, summary["decision_request"])
    elif (root_path / LAST_REQUEST_FILENAME).exists():
        (root_path / LAST_REQUEST_FILENAME).unlink()


def run_start(root_path, action="", requested_stage=""):
    sim_args = _prepare_sim_args(root_path)
    sim_client = agent_autotune_sim.SimClient(sim_args.sim_port)
    summary = agent_autotune.run_agent_autotune(
        sim_client,
        sim_args,
        explicit_action=action,
        requested_stage=requested_stage,
        stage_runner=agent_autotune_sim._make_stage_runner(root_path),
    )
    summary["simulation_root"] = str(root_path)
    _save_summary_files(root_path, summary)
    return summary


def run_submit(root_path, raw_agent_output, request_id=""):
    if not raw_agent_output:
        raise RuntimeError("submit requires --raw-json or --raw-file")
    sim_args = _prepare_sim_args(root_path)
    sim_client = agent_autotune_sim.SimClient(sim_args.sim_port)
    summary = agent_autotune.run_agent_autotune(
        sim_client,
        sim_args,
        raw_agent_output=raw_agent_output,
        request_id=request_id,
        stage_runner=agent_autotune_sim._make_stage_runner(root_path),
    )
    summary["simulation_root"] = str(root_path)
    _save_summary_files(root_path, summary)
    return summary


def main(argv=None):
    parser = build_argument_parser()
    if argv is None:
        argv_items = list(sys.argv[1:])
    else:
        argv_items = list(argv)
    argv_items = _normalize_global_flags(argv_items)
    args = parser.parse_args(argv_items)
    root_path = pathlib.Path(args.sim_root)
    root_path.mkdir(parents=True, exist_ok=True)

    if args.command == "start":
        summary = run_start(root_path, action=args.action, requested_stage=args.requested_stage)
    else:
        summary = run_submit(
            root_path,
            _load_raw_agent_output(args.raw_json, args.raw_file),
            request_id=args.request_id,
        )

    if not args.json and summary.get("final_workflow_status") == "waiting_user":
        text = agent_autotune.format_waiting_user_actions(summary)
        if text:
            print(text)
        return 0

    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0
