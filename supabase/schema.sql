-- Supabase SQL Editor 에서 실행
create table if not exists public.room_events (
  id           bigint generated always as identity primary key,
  created_at   timestamptz not null default now(),
  device_id    text not null,
  label        text not null,           -- calm | noisy | stuffy (다수결 후)
  raw_label    text,                    -- 모델 원시 예측
  confidence   real not null,
  probs        jsonb,                   -- {"empty":0.1, ...}
  anomaly      real default 0,
  temp         real not null,
  hum          real not null,
  sound_rate   real not null,           -- 최근 3초 소리 감지 비율 0~1
  servo_angle  int not null,
  auto_angle   int,                     -- 모델이 정한 각도 (수동 조작 중이면 servo_angle 과 다름)
  control      text not null default 'auto', -- auto | dashboard | joystick
  model        text not null,           -- edge-impulse | rule | fake
  report       text not null            -- change | periodic
);
alter table public.room_events enable row level security;
