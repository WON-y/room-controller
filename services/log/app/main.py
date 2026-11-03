"""로그 컨테이너 (log)

comm 이 보낸 이벤트(상태 변경 / 1분 주기)를
  1) 로컬 SQLite 에 먼저 저장 (Supabase 장애 대비 outbox)
  2) Supabase REST API 로 업로드 → 성공하면 synced=1
"""
from __future__ import annotations

import json
import os
import re
import sqlite3

import httpx
from fastapi import FastAPI
from pydantic import BaseModel, Field

def normalize_url(url: str) -> str:
    """대시보드에서 복사한 'https://x.supabase.co/rest/v1/' 형태도 허용 (아래에서 /rest/v1 을 붙이므로 제거)."""
    return re.sub(r"/rest/v1/?$", "", url.strip().rstrip("/"))


SUPABASE_URL = normalize_url(os.getenv("SUPABASE_URL", ""))
SUPABASE_KEY = os.getenv("SUPABASE_KEY", "")
TABLE = os.getenv("SUPABASE_TABLE", "room_events")
DB_PATH = os.getenv("LOCAL_DB", "/data/room_events.db")

app = FastAPI(title="room-log")


class RoomEvent(BaseModel):
    device_id: str
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
    control: str = "auto"
    model: str
    report: str


def db() -> sqlite3.Connection:
    os.makedirs(os.path.dirname(DB_PATH) or ".", exist_ok=True)
    con = sqlite3.connect(DB_PATH)
    con.execute("CREATE TABLE IF NOT EXISTS events (id INTEGER PRIMARY KEY, created_at TEXT DEFAULT "
                "(strftime('%Y-%m-%dT%H:%M:%SZ','now')), payload TEXT, synced INTEGER DEFAULT 0)")
    return con


def supabase_enabled() -> bool:
    return bool(SUPABASE_URL and SUPABASE_KEY)


def supabase_headers() -> dict:
    h = {"apikey": SUPABASE_KEY, "Content-Type": "application/json", "Prefer": "return=minimal"}
    if SUPABASE_KEY.startswith("eyJ"):  # 레거시 service_role(JWT) 키일 때만
        h["Authorization"] = f"Bearer {SUPABASE_KEY}"
    return h


def push_supabase(payload: dict) -> tuple[bool, str]:
    try:
        r = httpx.post(f"{SUPABASE_URL}/rest/v1/{TABLE}", json=payload, headers=supabase_headers(), timeout=10)
        return r.status_code in (200, 201, 204), f"{r.status_code} {r.text[:200]}"
    except Exception as e:
        return False, str(e)


@app.get("/health")
def health():
    return {"ok": True, "supabase": supabase_enabled()}


@app.post("/events")
def create_event(ev: RoomEvent):
    payload = ev.model_dump()
    with db() as con:
        row_id = con.execute("INSERT INTO events (payload) VALUES (?)", (json.dumps(payload),)).lastrowid
    if not supabase_enabled():
        return {"ok": True, "id": row_id, "synced": False, "detail": "supabase not configured (local only)"}
    ok, detail = push_supabase(payload)
    if ok:
        with db() as con:
            con.execute("UPDATE events SET synced=1 WHERE id=?", (row_id,))
    return {"ok": True, "id": row_id, "synced": ok, "detail": detail}


@app.post("/sync")
def sync_pending():
    """업로드 실패분 재전송."""
    if not supabase_enabled():
        return {"synced": 0, "detail": "supabase not configured"}
    n = 0
    with db() as con:
        for row_id, payload in con.execute("SELECT id, payload FROM events WHERE synced=0").fetchall():
            if push_supabase(json.loads(payload))[0]:
                con.execute("UPDATE events SET synced=1 WHERE id=?", (row_id,))
                n += 1
    return {"synced": n}


@app.get("/events")
def list_events(limit: int = 30):
    with db() as con:
        rows = con.execute("SELECT id, created_at, payload, synced FROM events ORDER BY id DESC LIMIT ?",
                           (limit,)).fetchall()
    return {"items": [{"id": i, "created_at": c, "synced": bool(s), **json.loads(p)} for i, c, p, s in rows]}
