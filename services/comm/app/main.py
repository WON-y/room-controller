"""통신 컨테이너 (comm)

  ESP32 ──POST /api/telemetry (1초마다)──▶ comm ──ws /ws/ui──▶ 브라우저 대시보드
                       ◀── {"override": 각도|null} ──┘   │
                                                        └─ report 플래그가 있으면 POST log:8001/events → Supabase
"""
from __future__ import annotations

import asyncio
import os
import time
from collections import deque

import httpx
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field

LOG_URL = os.getenv("LOG_URL", "http://log:8001")
OFFLINE_AFTER_S = 5.0
SERIAL_KEEP = 300
STATIC = os.path.join(os.path.dirname(__file__), "static")

app = FastAPI(title="room-comm")


class Telemetry(BaseModel):
    device_id: str = "esp32"
    label: str
    raw_label: str | None = None
    confidence: float
    probs: dict[str, float] = Field(default_factory=dict)
    anomaly: float = 0.0
    temp: float
    hum: float
    sound_rate: float
    servo_angle: int
    auto_angle: int | None = None
    control: str = "auto"  # auto | dashboard | joystick
    model: str = "rule"
    report: str | None = None  # "change" | "periodic" | None
    uptime_ms: int = 0


class SerialLines(BaseModel):
    lines: list[str] = Field(max_length=500)


class Override(BaseModel):
    angle: int | None = Field(default=None, ge=0, le=180)


class Hub:
    def __init__(self):
        self.ui: set[WebSocket] = set()
        self.latest: dict | None = None
        self.last_seen = 0.0
        self.override: int | None = None
        self.last_log_result: dict | None = None
        self.serial: deque[dict] = deque(maxlen=SERIAL_KEEP)  # 최근 시리얼 로그 (새 브라우저 접속 시 전송)

    def status(self) -> dict:
        online = self.latest is not None and time.time() - self.last_seen < OFFLINE_AFTER_S
        return {"type": "status", "online": online, "override": self.override, "last_log": self.last_log_result}

    async def broadcast(self, msg: dict):
        for ws in list(self.ui):
            try:
                await ws.send_json(msg)
            except Exception:
                self.ui.discard(ws)


hub = Hub()


async def forward_to_log(event: dict) -> dict:
    try:
        async with httpx.AsyncClient(timeout=5) as c:
            r = await c.post(f"{LOG_URL}/events", json=event)
            return r.json()
    except Exception as e:
        return {"ok": False, "error": str(e)}


async def _log_and_notify(event: dict):
    hub.last_log_result = await forward_to_log(event)
    await hub.broadcast({"type": "log_saved", "result": hub.last_log_result, "event": event})


@app.get("/")
async def index():
    return FileResponse(os.path.join(STATIC, "index.html"))


@app.get("/health")
async def health():
    return {"ok": True}


@app.post("/api/telemetry")
async def telemetry(t: Telemetry):
    data = t.model_dump()
    hub.latest, hub.last_seen = data, time.time()
    await hub.broadcast({"type": "telemetry", **data})
    if t.report:
        asyncio.create_task(_log_and_notify({k: v for k, v in data.items() if k not in ("uptime_ms",)}))
    return {"override": hub.override}


@app.post("/api/serial")
async def serial_lines(body: SerialLines):
    """tools/serial_bridge.py 가 보내는 ESP32 시리얼 로그 (telemetry 이외의 줄) → 대시보드."""
    now = time.time()
    items = [{"t": now, "line": line[:500]} for line in body.lines]
    hub.serial.extend(items)
    if items:
        await hub.broadcast({"type": "serial", "items": items})
    return {"ok": True, "count": len(items)}


@app.get("/api/serial")
async def serial_recent(limit: int = 100):
    return {"items": list(hub.serial)[-limit:]}


@app.post("/api/override")
async def set_override(o: Override):
    hub.override = o.angle
    await hub.broadcast(hub.status())
    return {"override": hub.override}


@app.get("/api/status")
async def status():
    return {**hub.status(), "latest": hub.latest}


@app.get("/api/log-health")
async def log_health():
    """대시보드 상단 Supabase 연결 표시용."""
    try:
        async with httpx.AsyncClient(timeout=3) as c:
            return (await c.get(f"{LOG_URL}/health")).json()
    except Exception as e:
        return {"ok": False, "supabase": False, "error": str(e)}


@app.get("/api/events")
async def events(limit: int = 30):
    try:
        async with httpx.AsyncClient(timeout=5) as c:
            return (await c.get(f"{LOG_URL}/events", params={"limit": limit})).json()
    except Exception as e:
        return {"items": [], "error": str(e)}


@app.websocket("/ws/ui")
async def ws_ui(ws: WebSocket):
    await ws.accept()
    hub.ui.add(ws)
    await ws.send_json(hub.status())
    if hub.latest:
        await ws.send_json({"type": "telemetry", **hub.latest})
    if hub.serial:
        await ws.send_json({"type": "serial", "items": list(hub.serial)})
    try:
        while True:
            await ws.receive_text()  # keepalive
    except WebSocketDisconnect:
        hub.ui.discard(ws)
