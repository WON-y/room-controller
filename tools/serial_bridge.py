"""ESP32 USB 시리얼 → 대시보드(comm) 브리지. WiFi 없이 USB 만 연결해도 대시보드/Supabase 가 동작한다.

python tools/serial_bridge.py [--port COM3] [--url http://localhost:8000]

  '@{json}' 줄  → POST /api/telemetry  (report 가 있으면 comm → log → Supabase 저장)
  그 외 줄       → POST /api/serial     (대시보드 '시리얼 로그' 패널, 진단 펌웨어 출력도 여기로)
  대시보드 수동 각도 → 시리얼로 'O<각도>' / 'O-1' 전송 (펌웨어가 받아서 서보 제어)

포트가 끊겨도 자동으로 다시 찾는다 (Espressif USB VID 0x303A 우선).
※ 실행 중에는 시리얼 포트를 점유하므로 pio device monitor / 업로드 전에 Ctrl+C 로 끌 것.
"""
from __future__ import annotations

import argparse
import json
import os
import time

import httpx
import serial
from serial.tools import list_ports

ESPRESSIF_VID = 0x303A


def parse_line(raw: bytes | str) -> tuple[str, object] | None:
    """시리얼 한 줄 → ("telemetry", dict) | ("log", str) | None(빈 줄)."""
    line = raw.decode("utf-8", "replace") if isinstance(raw, bytes) else raw
    line = line.strip("\r\n\x00 ")
    if not line:
        return None
    if line.startswith("@{"):
        try:
            return "telemetry", json.loads(line[1:])
        except json.JSONDecodeError:
            pass  # 잘린 줄 등은 로그로 표시
    return "log", line


def override_command(prev: int | None, new: int | None) -> bytes | None:
    """대시보드 override 가 바뀌었을 때만 펌웨어로 보낼 명령."""
    if new == prev:
        return None
    return f"O{-1 if new is None else int(new)}\n".encode()


def find_port(preferred: str | None) -> str | None:
    ports = list(list_ports.comports())
    if preferred:
        return preferred if any(p.device == preferred for p in ports) else None
    esp = [p.device for p in ports if p.vid == ESPRESSIF_VID]
    return esp[0] if esp else None


class Bridge:
    def __init__(self, url: str):
        self.http = httpx.Client(base_url=url, timeout=3)
        self.override: int | None = None
        self.pending_logs: list[str] = []

    def handle(self, ser, raw: bytes):
        parsed = parse_line(raw)
        if parsed is None:
            return
        kind, value = parsed
        if kind == "log":
            print(value)
            self.pending_logs.append(value)
            return
        self.flush_logs()
        try:
            r = self.http.post("/api/telemetry", json=value)
            if r.status_code != 200:
                print(f"[bridge] telemetry 거부 {r.status_code}: {r.text[:200]}")
                return
            new = r.json().get("override")
            cmd = override_command(self.override, new)
            if cmd:
                ser.write(cmd)
                print(f"[bridge] 대시보드 수동 각도 → {new}")
            self.override = new
            print(f"[telemetry] {value.get('label')} T={value.get('temp')} H={value.get('hum')} "
                  f"snd={value.get('sound_rate')} {value.get('report') or ''}")
        except httpx.HTTPError as e:
            print(f"[bridge] comm 연결 실패 ({e}) — docker compose up 했는지 확인")

    def flush_logs(self):
        if not self.pending_logs:
            return
        lines, self.pending_logs = self.pending_logs, []
        try:
            self.http.post("/api/serial", json={"lines": lines})
        except httpx.HTTPError:
            pass

    def notice(self, msg: str):
        print(f"[bridge] {msg}")
        self.pending_logs.append(f"[bridge] {msg}")
        self.flush_logs()


def default_url() -> str:
    """프로젝트 루트 .env 의 DASHBOARD_PORT 를 따름 (docker-compose 와 같은 값)."""
    port = os.getenv("DASHBOARD_PORT")
    env = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".env")
    if not port and os.path.exists(env):
        for line in open(env, encoding="utf-8"):
            if line.startswith("DASHBOARD_PORT="):
                port = line.split("=", 1)[1].strip()
    return f"http://localhost:{port or 8000}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="예: COM3 (생략 시 ESP32 자동 검색)")
    ap.add_argument("--url", default=default_url(), help="대시보드 주소 (기본: .env 의 DASHBOARD_PORT)")
    ap.add_argument("--baud", type=int, default=115200)
    a = ap.parse_args()
    b = Bridge(a.url)
    waiting = False
    while True:
        port = find_port(a.port)
        if not port:
            if not waiting:
                b.notice("ESP32 포트를 찾는 중… (USB 연결/케이블 확인)")
                waiting = True
            time.sleep(1)
            continue
        waiting = False
        try:
            with serial.Serial(port, a.baud, timeout=0.5) as ser:
                b.notice(f"{port} 연결됨")
                b.override = None  # 재연결 시 현재 override 를 다시 보내도록
                last_flush = time.time()
                while True:
                    raw = ser.readline()
                    if raw:
                        b.handle(ser, raw)
                    if time.time() - last_flush > 0.3:
                        b.flush_logs()
                        last_flush = time.time()
        except (serial.SerialException, OSError) as e:
            b.notice(f"{port} 연결 끊김 ({e.__class__.__name__}) — 다시 찾는 중")
            time.sleep(1)
        except KeyboardInterrupt:
            break


if __name__ == "__main__":
    main()
