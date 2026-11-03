import importlib

from fastapi.testclient import TestClient

EV = {"device_id": "d", "label": "calm", "confidence": 0.8, "probs": {"calm": 0.8}, "temp": 23, "hum": 50,
      "sound_rate": 0.0, "servo_angle": 30, "model": "edge-impulse", "report": "change"}


def client(monkeypatch, tmp_path, supabase=False):
    monkeypatch.setenv("LOCAL_DB", str(tmp_path / "t.db"))
    monkeypatch.setenv("SUPABASE_URL", "https://fake.supabase.co" if supabase else "")
    monkeypatch.setenv("SUPABASE_KEY", "sb_secret_x" if supabase else "")
    import app.main as m
    importlib.reload(m)
    return m, TestClient(m.app)


def test_local_only(monkeypatch, tmp_path):
    _, c = client(monkeypatch, tmp_path)
    r = c.post("/events", json=EV).json()
    assert r["ok"] and not r["synced"]
    assert c.get("/events").json()["items"][0]["label"] == "calm"


def test_supabase_push_and_headers(monkeypatch, tmp_path):
    m, c = client(monkeypatch, tmp_path, supabase=True)
    assert "Authorization" not in m.supabase_headers()  # sb_secret 키는 apikey 헤더만
    sent = []
    monkeypatch.setattr(m, "push_supabase", lambda p: (sent.append(p) or True, "201"))
    assert c.post("/events", json=EV).json()["synced"]
    assert c.get("/events").json()["items"][0]["synced"]


def test_retry_sync(monkeypatch, tmp_path):
    m, c = client(monkeypatch, tmp_path, supabase=True)
    monkeypatch.setattr(m, "push_supabase", lambda p: (False, "down"))
    c.post("/events", json=EV)
    monkeypatch.setattr(m, "push_supabase", lambda p: (True, "201"))
    assert c.post("/sync").json()["synced"] == 1


def test_invalid(monkeypatch, tmp_path):
    _, c = client(monkeypatch, tmp_path)
    assert c.post("/events", json={"label": "x"}).status_code == 422


def test_supabase_url_normalized(monkeypatch, tmp_path):
    import app.main as m
    for raw in ["https://x.supabase.co", "https://x.supabase.co/", "https://x.supabase.co/rest/v1/", " https://x.supabase.co/rest/v1 "]:
        assert m.normalize_url(raw) == "https://x.supabase.co"
