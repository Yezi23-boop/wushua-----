import time


START_STATE_IDLE = 0
START_STATE_ACCEPTED = 1
START_STATE_RUNNING = 2

DEFAULT_READY_MATCH_COUNT = 2
DEFAULT_START_RETRY_LIMIT = 2
DEFAULT_START_SEQ_MODULO = 65535

_NEXT_START_SEQ = 0


def _next_start_seq():
    global _NEXT_START_SEQ

    _NEXT_START_SEQ += 1
    if _NEXT_START_SEQ > DEFAULT_START_SEQ_MODULO:
        _NEXT_START_SEQ = 1
    return _NEXT_START_SEQ


def allocate_start_seq():
    return _next_start_seq()


def build_pwm_command_pair(wheel_name, pwm_value):
    if wheel_name == "left":
        return (int(pwm_value), 0)
    if wheel_name == "right":
        return (0, int(pwm_value))
    raise ValueError("wheel_name must be 'left' or 'right'")


def build_pwm_command_text(wheel_name, pwm_value):
    left_pwm, right_pwm = build_pwm_command_pair(wheel_name, pwm_value)
    return ("L_TEST_PWM={0}".format(left_pwm), "R_TEST_PWM={0}".format(right_pwm))


def build_open_loop_capture_events(wheel_name, hold_ms, keepalive_ms):
    events = []
    left_zero_command, right_zero_command = build_pwm_command_text(wheel_name, 0)

    if keepalive_ms > 0:
        current_ms = int(keepalive_ms)
        while current_ms < int(hold_ms):
            events.append((float(current_ms) / 1000.0, "START"))
            current_ms += int(keepalive_ms)

    events.append((float(hold_ms) / 1000.0, left_zero_command))
    events.append((float(hold_ms) / 1000.0, right_zero_command))
    return events


def _sample_matches_expected(sample, expected_left_pwm, expected_right_pwm, request_seq):
    if abs(sample.mode_id - 2.0) > 0.5:
        return 0
    if abs(sample.start_seq - float(request_seq)) > 0.5:
        return 0
    if abs(sample.start_state - float(START_STATE_RUNNING)) > 0.5:
        return 0
    if abs(sample.left_cmd_pwm - float(expected_left_pwm)) > 0.5:
        return 0
    if abs(sample.right_cmd_pwm - float(expected_right_pwm)) > 0.5:
        return 0
    return 1


def _sample_matches_setup(sample, expected_left_pwm, expected_right_pwm, request_seq):
    if abs(sample.mode_id - 2.0) > 0.5:
        return 0
    if abs(sample.left_cmd_pwm - float(expected_left_pwm)) > 0.5:
        return 0
    if abs(sample.right_cmd_pwm - float(expected_right_pwm)) > 0.5:
        return 0
    if abs(getattr(sample, "start_seq_cmd", 0.0) - float(request_seq)) > 0.5:
        return 0
    return 1


def _format_sample_value(value):
    if value is None:
        return "n/a"
    return str(value)


class OpenLoopStartupTimeout(RuntimeError):
    def __init__(self, wheel_name, pwm_value, request_seq, attempt_index, phase, last_sample):
        self.wheel_name = wheel_name
        self.pwm_value = int(pwm_value)
        self.request_seq = int(request_seq)
        self.attempt_index = int(attempt_index)
        self.phase = phase
        self.last_sample = last_sample
        RuntimeError.__init__(self, self._build_message())

    def _build_message(self):
        last_sample = self.last_sample
        if last_sample is None:
            details = "mode_id=n/a start_seq_cmd=n/a start_seq=n/a start_state=n/a cmd_pwm=n/a stop_flag=n/a"
        else:
            if self.wheel_name == "left":
                command_pwm = last_sample.left_cmd_pwm
            else:
                command_pwm = last_sample.right_cmd_pwm
            details = (
                "mode_id={0} start_seq_cmd={1} start_seq={2} start_state={3} cmd_pwm={4} stop_flag={5}".format(
                    _format_sample_value(last_sample.mode_id),
                    _format_sample_value(getattr(last_sample, "start_seq_cmd", None)),
                    _format_sample_value(last_sample.start_seq),
                    _format_sample_value(last_sample.start_state),
                    _format_sample_value(command_pwm),
                    _format_sample_value(last_sample.stop_flag),
                )
            )

        return (
            "open-loop {0} confirmation failed "
            "wheel={1} pwm={2} request_seq={3} attempt={4} {5}"
        ).format(
            self.phase,
            self.wheel_name,
            self.pwm_value,
            self.request_seq,
            self.attempt_index,
            details,
        )


def wait_for_confirmed_open_loop_setup(
    client,
    wheel_name,
    pwm_value,
    request_seq,
    wait_seconds,
    poll_seconds,
    attempt_index,
    ready_match_count=DEFAULT_READY_MATCH_COUNT,
):
    deadline = time.monotonic() + float(wait_seconds)
    consecutive = 0
    last_sample = None
    expected_left_pwm, expected_right_pwm = build_pwm_command_pair(wheel_name, pwm_value)

    if not hasattr(client, "read_samples"):
        raise RuntimeError("client missing read_samples for startup confirmation")

    while time.monotonic() < deadline:
        samples = client.read_samples(poll_seconds)
        for sample in samples:
            last_sample = sample
            if _sample_matches_setup(sample, expected_left_pwm, expected_right_pwm, request_seq):
                consecutive += 1
                if consecutive >= int(ready_match_count):
                    return
            else:
                consecutive = 0

    raise OpenLoopStartupTimeout(wheel_name, pwm_value, request_seq, attempt_index, "setup", last_sample)


def wait_for_confirmed_open_loop_start(
    client,
    wheel_name,
    pwm_value,
    request_seq,
    wait_seconds,
    poll_seconds,
    attempt_index,
    ready_match_count=DEFAULT_READY_MATCH_COUNT,
):
    deadline = time.monotonic() + float(wait_seconds)
    consecutive = 0
    prefetched_samples = []
    last_sample = None
    expected_left_pwm, expected_right_pwm = build_pwm_command_pair(wheel_name, pwm_value)

    if not hasattr(client, "read_samples"):
        raise RuntimeError("client missing read_samples for startup confirmation")

    while time.monotonic() < deadline:
        samples = client.read_samples(poll_seconds)
        for sample in samples:
            last_sample = sample
            if _sample_matches_expected(sample, expected_left_pwm, expected_right_pwm, request_seq):
                prefetched_samples.append(sample)
                consecutive += 1
                if consecutive >= int(ready_match_count):
                    return prefetched_samples
            else:
                consecutive = 0

    raise OpenLoopStartupTimeout(wheel_name, pwm_value, request_seq, attempt_index, "start", last_sample)


def _cleanup_open_loop_trial(client):
    client.send_command("AT_TEST_MODE=0")
    client.send_command("AT_RESET")


def run_confirmed_open_loop_trial(
    client,
    wheel_name,
    pwm_value,
    hold_ms,
    tail_zero_ms,
    rest_seconds,
    wait_seconds,
    poll_seconds,
    keepalive_ms,
    preserve_ready_samples,
    drain_after_ready,
    skip_start_confirmation,
    sleep_fn=time.sleep,
):
    active_command, idle_command = build_pwm_command_text(wheel_name, pwm_value)
    capture_events = build_open_loop_capture_events(wheel_name, hold_ms, keepalive_ms)
    attempt_index = 1

    while attempt_index <= DEFAULT_START_RETRY_LIMIT:
        request_seq = allocate_start_seq()
        prefetched_samples = []
        samples = []

        client.send_command("AT_RESET")
        sleep_fn(rest_seconds)
        client.send_command("AT_TEST_MODE=1")
        client.send_command(active_command)
        client.send_command(idle_command)
        client.send_command("AT_START_SEQ={0}".format(int(request_seq)))
        if hasattr(client, "drain_input"):
            client.drain_input()

        try:
            wait_for_confirmed_open_loop_setup(
                client,
                wheel_name,
                pwm_value,
                request_seq,
                wait_seconds,
                poll_seconds,
                attempt_index,
            )
            client.send_command("START")
            if not skip_start_confirmation:
                prefetched_samples = wait_for_confirmed_open_loop_start(
                    client,
                    wheel_name,
                    pwm_value,
                    request_seq,
                    wait_seconds,
                    poll_seconds,
                    attempt_index,
                )
            if (skip_start_confirmation or drain_after_ready) and hasattr(client, "drain_input"):
                client.drain_input()
            samples = client.capture_trial(
                float(hold_ms + tail_zero_ms) / 1000.0,
                events=capture_events,
            )
            _cleanup_open_loop_trial(client)
            if preserve_ready_samples and prefetched_samples:
                return list(prefetched_samples) + list(samples)
            return list(samples)
        except OpenLoopStartupTimeout:
            _cleanup_open_loop_trial(client)
            if attempt_index >= DEFAULT_START_RETRY_LIMIT:
                raise
        attempt_index += 1

    raise RuntimeError("open-loop start retry loop exited unexpectedly")
