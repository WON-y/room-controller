"""ESP32 없이 시연/테스트용 가짜 디바이스: 펌웨어와 같은 형식으로 /api/telemetry 에 POST.

python tools/fake_device.py [--url http://localhost:8000] [--period 10]
상태를 calm → noisy → stuffy 순으로 period 초씩 바꿔가며 1초마다 전송.
"""
import argparse
import random
import time

import httpx

LABELS = ["calm", "noisy", "stuffy"]
ANGLES = {"calm": 0, "noisy": 45, "stuffy": 90}
PROFILE = {"calm": (23.0, 45, 0.02), "noisy": (24.0, 50, 0.4), "stuffy": (28.5, 75, 0.1)}  # temp, hum, sound


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://localhost:8000")
    ap.add_argument("--period", type=int, default=10)
    ap.add_argument("--cycles", type=int, default=0, help="0 = 무한")
    a = ap.parse_args()
    last, last_t, start, i, override = None, 0.0, time.time(), 0, None
    while True:
        idx = int((time.time() - start) // a.period)
        if a.cycles and idx >= a.cycles * len(LABELS):
            break
        label = LABELS[idx % len(LABELS)]
        t, h, s = PROFILE[label]
        conf = random.uniform(0.75, 0.97)
        probs = {l: (conf if l == label else (1 - conf) / 2) for l in LABELS}
        report = "change" if label != last else ("periodic" if time.time() - last_t >= 60 else None)
        if report:
            last, last_t = label, time.time()
        body = {"device_id": "fake-device", "label": label, "raw_label": label, "confidence": conf, "probs": probs,
                "temp": t + random.uniform(-0.3, 0.3), "hum": h + random.uniform(-2, 2), "sound_rate": s,
                "servo_angle": override if override is not None else ANGLES[label], "auto_angle": ANGLES[label],
                "control": "dashboard" if override is not None else "auto", "model": "fake", "report": report,
                "uptime_ms": i * 1000}
        try:
            override = httpx.post(f"{a.url}/api/telemetry", json=body, timeout=2).json().get("override")
            print(label, report or "", "override=", override)
        except Exception as e:
            print("error:", e)
        i += 1
        time.sleep(1)


if __name__ == "__main__":
    main()
