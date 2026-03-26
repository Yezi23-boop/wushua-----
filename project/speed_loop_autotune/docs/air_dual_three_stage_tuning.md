# Air Dual Three-Stage Tuning

This note is historical only.

- The old `air-dual` batch-tuning entrypoint has been removed.
- Current user-facing tuning now goes through the Agent workflow described in `docs/agent_autotune.md`.
- The only supported worker CLI for this stage is `air-dual-step`, and it is meant to be called by `host/agent_orchestrator.py` or the Codex skill.
- Keep this file only as background on an older search strategy. Do not use it as the current operating guide.
