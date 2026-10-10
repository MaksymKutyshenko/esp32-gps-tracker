CREATE TABLE IF NOT EXISTS devices (
  id          text PRIMARY KEY,
  name        text,
  first_seen  timestamptz NOT NULL DEFAULT now(),
  last_seen   timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS positions (
  id           bigserial PRIMARY KEY,
  device_id    text NOT NULL REFERENCES devices(id),
  fix_time     timestamptz NOT NULL,
  received_at  timestamptz NOT NULL DEFAULT now(),
  lat          double precision NOT NULL,
  lon          double precision NOT NULL,
  altitude_m   real,
  speed_kn     real,
  bearing_deg  real,
  hdop         real,
  battery_pct  smallint,
  extra        jsonb NOT NULL DEFAULT '{}'::jsonb
);
-- The firmware re-sends a record whose 200 was lost: one row per fix.
CREATE UNIQUE INDEX IF NOT EXISTS positions_device_fix ON positions (device_id, fix_time DESC);

CREATE TABLE IF NOT EXISTS health_reports (
  id            bigserial PRIMARY KEY,
  device_id     text NOT NULL REFERENCES devices(id),
  -- When the server got it, or the device's own clock when it sent ts=.
  received_at   timestamptz NOT NULL DEFAULT now(),
  uptime_s      integer,
  reset_reason  text,
  state         text,
  battery_mv    integer,
  firmware      text,
  metrics       jsonb NOT NULL DEFAULT '{}'::jsonb,
  stack_free    integer[]
);
CREATE INDEX IF NOT EXISTS health_device_time ON health_reports (device_id, received_at DESC);
