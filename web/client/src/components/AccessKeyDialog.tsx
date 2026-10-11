import { useEffect, useState } from 'react';

import type { AccessKeyInfo } from '../../../shared/types';
import { ApiError, getAccessKeyInfo, rotateAccessKey } from '../api';
import { fmtTime } from '../format';

// Owner-only: replace the shared viewer key. The new key is shown once,
// with a share link; every viewer signed in with the old key is signed out.
export function AccessKeyDialog({ onClose }: { onClose: () => void }) {
  const [info, setInfo] = useState<AccessKeyInfo | null>(null);
  const [custom, setCustom] = useState('');
  const [fresh, setFresh] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [copied, setCopied] = useState<string | null>(null);

  useEffect(() => {
    getAccessKeyInfo().then(setInfo).catch((e: Error) => setError(e.message));
  }, []);

  const rotate = async (key?: string) => {
    if (!confirm('Replace the shared access key? Everyone signed in with the current key will be signed out.')) return;
    setBusy(true);
    setError(null);
    try {
      const result = await rotateAccessKey(key);
      setFresh(result.key);
      setInfo(result);
      setCustom('');
    } catch (e) {
      setError(e instanceof ApiError ? e.message : 'Failed');
    } finally {
      setBusy(false);
    }
  };

  const link = fresh ? `${location.origin}/#key=${encodeURIComponent(fresh)}` : null;
  const copy = async (text: string, what: string) => {
    try {
      await navigator.clipboard.writeText(text);
      setCopied(what);
      setTimeout(() => setCopied(null), 1500);
    } catch {
      setError('Clipboard unavailable, select and copy by hand');
    }
  };

  return (
    <div className="overlay" onClick={onClose}>
      <div className="dialog card" onClick={(e) => e.stopPropagation()} role="dialog" aria-label="Shared access key">
        <div className="card-head">
          <h2>Shared access key</h2>
          <button className="btn icon" style={{ marginLeft: 'auto' }} onClick={onClose} aria-label="Close">
            ✕
          </button>
        </div>
        <div className="card-body">
          <p className="small muted">
            Anyone with this key can watch the tracker. The key itself is stored only as a hash, so it cannot be shown again:
            generate a new one when you need to hand it out again or want to cut someone off.
          </p>
          {info && (
            <p className="small">
              {info.configured
                ? `Current key set ${info.updatedAt ? fmtTime(info.updatedAt) : ''}${info.updatedBy ? ` by ${info.updatedBy}` : ''}.`
                : 'No viewer key is set yet: only the owner can sign in.'}
            </p>
          )}
          {fresh ? (
            <div className="fresh">
              <div className="label">New key (shown once)</div>
              <div className="keyrow">
                <code className="mono">{fresh}</code>
                <button className="btn" onClick={() => copy(fresh, 'key')}>
                  {copied === 'key' ? 'Copied' : 'Copy'}
                </button>
              </div>
              <div className="label">Share link</div>
              <div className="keyrow">
                <code className="mono">{link}</code>
                <button className="btn" onClick={() => link && copy(link, 'link')}>
                  {copied === 'link' ? 'Copied' : 'Copy'}
                </button>
              </div>
            </div>
          ) : (
            <div className="actions">
              <button className="btn primary" onClick={() => rotate()} disabled={busy}>
                Generate a new key
              </button>
              <form
                className="keyform"
                onSubmit={(e) => {
                  e.preventDefault();
                  void rotate(custom.trim());
                }}
              >
                <input
                  placeholder="…or set your own (12+ characters)"
                  value={custom}
                  onChange={(e) => setCustom(e.target.value)}
                  disabled={busy}
                  minLength={12}
                />
                <button className="btn" type="submit" disabled={busy || custom.trim().length < 12}>
                  Set
                </button>
              </form>
            </div>
          )}
          {error && <p className="error">{error}</p>}
        </div>
      </div>
    </div>
  );
}
