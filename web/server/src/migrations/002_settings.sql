-- Runtime settings changed from the dashboard, not by a deploy. Today:
-- access_key_hash (the shared viewer key, scrypt-hashed).
CREATE TABLE IF NOT EXISTS settings (
  key         text PRIMARY KEY,
  value       text NOT NULL,
  updated_at  timestamptz NOT NULL DEFAULT now(),
  updated_by  text
);
