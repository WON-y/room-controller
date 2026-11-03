import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from serial_bridge import override_command, parse_line  # noqa: E402


def test_telemetry_line():
    kind, v = parse_line(b'@{"label":"calm","temp":23.5,"report":"change"}\r\n')
    assert kind == "telemetry" and v["label"] == "calm" and v["report"] == "change"


def test_plain_log_line():
    assert parse_line("[rule] calm (80%) T=0.0 H=0\r\n") == ("log", "[rule] calm (80%) T=0.0 H=0")
    assert parse_line("DHT11 읽기: T=실패".encode()) == ("log", "DHT11 읽기: T=실패")


def test_truncated_json_falls_back_to_log():
    assert parse_line(b'@{"label":"ca')[0] == "log"


def test_blank_line():
    assert parse_line(b"\r\n") is None


def test_override_command():
    assert override_command(None, None) is None
    assert override_command(None, 90) == b"O90\n"
    assert override_command(90, 90) is None
    assert override_command(90, None) == b"O-1\n"
