from fastapi.testclient import TestClient

import app.main as m

T = {"device_id": "d", "label": "noisy", "confidence": 0.9, "probs": {"noisy": 0.9}, "temp": 24, "hum": 55,
     "sound_rate": 0.4, "servo_angle": 60, "model": "rule"}


def test_telemetry_no_report(monkeypatch):
    sent = []
    async def fake(ev):
        sent.append(ev); return {"ok": True}
    monkeypatch.setattr(m, "forward_to_log", fake)
    c = TestClient(m.app)
    assert c.post("/api/telemetry", json=T).json() == {"override": None}
    assert c.get("/api/status").json()["online"]
    assert sent == []


def test_telemetry_report_forwards(monkeypatch):
    sent = []
    async def fake(ev):
        sent.append(ev); return {"ok": True}
    monkeypatch.setattr(m, "forward_to_log", fake)
    with TestClient(m.app) as c:
        c.post("/api/telemetry", json={**T, "report": "change"})
    assert sent and sent[0]["label"] == "noisy" and sent[0]["report"] == "change"


def test_override_roundtrip():
    c = TestClient(m.app)
    assert c.post("/api/override", json={"angle": 90}).json()["override"] == 90
    assert c.post("/api/telemetry", json=T).json()["override"] == 90
    c.post("/api/override", json={"angle": None})
    assert c.post("/api/override", json={"angle": 200}).status_code == 422


def test_invalid_telemetry():
    assert TestClient(m.app).post("/api/telemetry", json={"label": "x"}).status_code == 422


def test_serial_lines_buffered_and_sent_to_new_ui():
    c = TestClient(m.app)
    m.hub.serial.clear()
    assert c.post("/api/serial", json={"lines": ["DHT11 읽기: T=실패", "[bridge] COM3 연결됨"]}).json()["count"] == 2
    assert [i["line"] for i in c.get("/api/serial").json()["items"]] == ["DHT11 읽기: T=실패", "[bridge] COM3 연결됨"]
    with c.websocket_connect("/ws/ui") as ws:
        msgs = [ws.receive_json() for _ in range(2 if m.hub.latest is None else 3)]
    assert any(x["type"] == "serial" and len(x["items"]) == 2 for x in msgs)


def test_serial_buffer_is_bounded():
    c = TestClient(m.app)
    m.hub.serial.clear()
    for i in range(4):
        c.post("/api/serial", json={"lines": [f"l{i}-{j}" for j in range(100)]})
    assert len(m.hub.serial) == m.SERIAL_KEEP
