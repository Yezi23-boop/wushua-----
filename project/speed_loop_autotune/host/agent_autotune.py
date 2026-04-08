import json
import sys

from . import agent_orchestrator, common, decision_heuristic, vofa_autotune


def build_argument_parser():
    parser = vofa_autotune.build_argument_parser()
    parser.description = "Speed-loop agent autotune workflow runner"
    parser.add_argument(
        "action",
        nargs="?",
        default="",
        help="Optional batch-boundary action word such as continue_air or enter_ground.",
    )
    parser.add_argument(
        "--requested-stage",
        choices=("air_dual", "ground_dual"),
        default="",
        help="Optional explicit stage override. ground_dual still requires enter_ground.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Print the final workflow summary as JSON instead of action words.",
    )
    parser.add_argument(
        "--request-id",
        default="",
        help="Optional request_id used when submitting a raw agent decision.",
    )
    parser.add_argument(
        "--submit-raw-json",
        default="",
        help="Raw agent JSON text to submit for the current pending request.",
    )
    parser.add_argument(
        "--submit-raw-file",
        default="",
        help="Path to a file containing the raw agent JSON to submit.",
    )
    parser.add_argument(
        "--fallback-heuristic",
        action="store_true",
        help="Use the local heuristic fallback loop instead of stopping at decision_required.",
    )
    return parser


def _decision_to_raw_json(decision_payload):
    if isinstance(decision_payload, str):
        return decision_payload
    return json.dumps(decision_payload, ensure_ascii=False)


def _decision_summary_item(request_payload, raw_agent_output):
    stage_context = request_payload.get("context", {}).get("stage_context", {})
    item = {
        "request_id": request_payload.get("request_id", ""),
        "stage": stage_context.get("stage_name", ""),
        "batch_id": stage_context.get("batch_id", ""),
        "round_index": stage_context.get("round_index", 0),
        "raw_agent_output": raw_agent_output,
    }
    try:
        decision_payload = json.loads(raw_agent_output)
    except (TypeError, ValueError):
        return item
    if not isinstance(decision_payload, dict):
        return item
    item["decision_mode"] = decision_payload.get("decision_mode", "")
    item["primary_reason"] = decision_payload.get("primary_reason", "")
    item["candidate_pid"] = decision_payload.get("candidate_pid")
    return item


def _resolve_batch_size(batch_size):
    resolved_batch_size = batch_size
    if resolved_batch_size is None:
        resolved_batch_size = agent_orchestrator.DEFAULT_BATCH_SIZE
    if int(resolved_batch_size) != int(agent_orchestrator.DEFAULT_BATCH_SIZE):
        raise ValueError(
            "batch_size must remain {0}".format(agent_orchestrator.DEFAULT_BATCH_SIZE)
        )
    return int(resolved_batch_size)


def _load_pending_request_id(args):
    profile_path = common.resolve_profile_path(
        getattr(args, "profile_path", str(common.DEFAULT_TUNING_PROFILE_PATH))
    )
    profile = common.load_tuning_profile(str(profile_path), required=False)
    agent_tuning = profile.get("agent_tuning", {})
    pending_request = agent_tuning.get("pending_decision_request")
    if isinstance(pending_request, dict):
        request_id = pending_request.get("request_id", "")
        if isinstance(request_id, str):
            return request_id
    return ""


def _load_raw_agent_output(args, raw_agent_output):
    if raw_agent_output:
        return raw_agent_output
    raw_file_path = getattr(args, "submit_raw_file", "")
    if not raw_file_path:
        return ""
    try:
        return open(raw_file_path, "r", encoding="utf-8").read()
    except OSError as exc:
        raise RuntimeError("Unable to read raw agent JSON file: {0}".format(raw_file_path)) from exc


def _build_summary(port, explicit_action, requested_stage, state, rounds_run, operation):
    summary = {
        "port": port,
        "operation": operation,
        "explicit_action": explicit_action,
        "requested_stage": requested_stage,
        "rounds_run": list(rounds_run),
        "final_workflow_status": state.get("workflow_status", ""),
        "final_workflow_stage": state.get("workflow_stage", ""),
        "current_batch_id": state.get("current_batch_id", ""),
        "current_round_index": state.get("current_round_index", 0),
    }
    if state.get("pending_user_action") is not None:
        summary["pending_user_action"] = state.get("pending_user_action")
    if state.get("decision_request") is not None:
        request_payload = state.get("decision_request")
        stage_context = request_payload.get("context", {}).get("stage_context", {})
        summary["decision_request"] = request_payload
        summary["next_request"] = {
            "request_id": request_payload.get("request_id", ""),
            "stage_name": stage_context.get("stage_name", ""),
            "batch_id": stage_context.get("batch_id", ""),
            "round_index": stage_context.get("round_index", 0),
        }
    if state.get("batch_summary") is not None:
        summary["batch_summary"] = state.get("batch_summary")
    return summary


def format_waiting_user_actions(state):
    pending = state.get("pending_user_action")
    if not isinstance(pending, dict):
        return ""
    actions = pending.get("allowed_actions", [])
    lines = []
    for action in actions:
        lines.append(str(action))
    return "\n".join(lines)


def start_or_resume_agent_bridge(
    client,
    args,
    explicit_action="",
    requested_stage="",
    stage_runner=None,
    batch_size=None,
):
    resolved_batch_size = _resolve_batch_size(batch_size)
    port = getattr(client, "port", getattr(args, "port", ""))
    state = agent_orchestrator.start_or_resume_workflow(
        client,
        args,
        explicit_action=explicit_action,
        requested_stage=requested_stage,
        stage_runner=stage_runner,
        batch_size=resolved_batch_size,
    )
    return _build_summary(port, explicit_action, requested_stage, state, [], "start_or_resume")


def submit_raw_agent_decision(
    client,
    args,
    raw_agent_output,
    request_id="",
    stage_runner=None,
    batch_size=None,
):
    resolved_batch_size = _resolve_batch_size(batch_size)
    port = getattr(client, "port", getattr(args, "port", ""))
    resolved_request_id = request_id or _load_pending_request_id(args)
    if not resolved_request_id:
        raise RuntimeError("Missing pending request_id for raw agent submission")
    state = agent_orchestrator.submit_agent_response(
        client,
        args,
        resolved_request_id,
        "ok",
        raw_agent_output,
        stage_runner=stage_runner,
        batch_size=resolved_batch_size,
    )
    return _build_summary(port, "", "", state, [], "submit_decision")


def run_fallback_heuristic_batch(
    client,
    args,
    explicit_action="",
    requested_stage="",
    decision_builder=None,
    stage_runner=None,
    batch_size=None,
):
    resolved_batch_size = _resolve_batch_size(batch_size)
    builder = decision_builder or decision_heuristic.build_llm_decision
    port = getattr(client, "port", getattr(args, "port", ""))
    rounds_run = []
    state = agent_orchestrator.start_or_resume_workflow(
        client,
        args,
        explicit_action=explicit_action,
        requested_stage=requested_stage,
        stage_runner=stage_runner,
        batch_size=resolved_batch_size,
    )

    while state.get("workflow_status") == "decision_required":
        request_payload = state.get("decision_request")
        if not isinstance(request_payload, dict):
            raise RuntimeError("decision_required state missing decision_request")
        raw_agent_output = _decision_to_raw_json(builder(request_payload))
        rounds_run.append(_decision_summary_item(request_payload, raw_agent_output))
        state = agent_orchestrator.submit_agent_response(
            client,
            args,
            request_payload.get("request_id", ""),
            "ok",
            raw_agent_output,
            stage_runner=stage_runner,
            batch_size=resolved_batch_size,
        )

    return _build_summary(port, explicit_action, requested_stage, state, rounds_run, "fallback_heuristic")


def run_agent_autotune(
    client,
    args,
    explicit_action="",
    requested_stage="",
    decision_builder=None,
    raw_agent_output="",
    request_id="",
    stage_runner=None,
    batch_size=None,
):
    raw_payload = _load_raw_agent_output(args, raw_agent_output)
    if raw_payload:
        return submit_raw_agent_decision(
            client,
            args,
            raw_payload,
            request_id=request_id or getattr(args, "request_id", ""),
            stage_runner=stage_runner,
            batch_size=batch_size,
        )
    if decision_builder is not None or getattr(args, "fallback_heuristic", False):
        return run_fallback_heuristic_batch(
            client,
            args,
            explicit_action=explicit_action,
            requested_stage=requested_stage,
            decision_builder=decision_builder,
            stage_runner=stage_runner,
            batch_size=batch_size,
        )
    return start_or_resume_agent_bridge(
        client,
        args,
        explicit_action=explicit_action,
        requested_stage=requested_stage,
        stage_runner=stage_runner,
        batch_size=batch_size,
    )


def main(argv=None):
    parser = build_argument_parser()
    argv_items = sys.argv[1:] if argv is None else list(argv)
    args = parser.parse_args(argv_items)
    args.mode = common.normalize_mode_name(args.mode)

    try:
        port = common.detect_port(args.port)
    except (RuntimeError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2

    client = None
    try:
        client = common.VofaSerialClient(port, args.baudrate, args.timeout)
        summary = run_agent_autotune(
            client,
            args,
            explicit_action=args.action,
            requested_stage=args.requested_stage,
            raw_agent_output=args.submit_raw_json,
            request_id=args.request_id,
        )
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    finally:
        if client is not None:
            client.close()

    if not args.json and summary.get("final_workflow_status") == "waiting_user":
        text = format_waiting_user_actions(summary)
        if text:
            print(text)
        return 0

    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
