import type { DeviceOverview, ModuleLevel, ModuleStatus } from '../../../shared/types';
import { ago } from '../format';

const ICON: Record<ModuleLevel, string> = { ok: '✓', warn: '!', error: '✕', unknown: '?' };
const LABEL: Record<ModuleLevel, string> = { ok: 'OK', warn: 'Warning', error: 'Problem', unknown: 'Unknown' };

export function Level({ level }: { level: ModuleLevel }) {
  return (
    <span className={`level ${level}`}>
      <span className="ico" aria-hidden>
        {ICON[level]}
      </span>
      {LABEL[level]}
    </span>
  );
}

function ModuleCard({ m }: { m: ModuleStatus }) {
  return (
    <div className={`module ${m.level}`}>
      <div className="title">
        <h3>{m.name}</h3>
        <Level level={m.level} />
      </div>
      <div className="summary">{m.summary}</div>
      {m.issues.length > 0 && (
        <ul className="issues">
          {m.issues.map((issue) => (
            <li key={issue}>{issue}</li>
          ))}
        </ul>
      )}
      {m.details.length > 0 && (
        <dl>
          {m.details.map((d) => (
            <>
              <dt key={`${d.label}-t`}>{d.label}</dt>
              <dd key={`${d.label}-d`}>{d.value}</dd>
            </>
          ))}
        </dl>
      )}
    </div>
  );
}

export function ModulesPanel({ overview, now }: { overview: DeviceOverview | null; now: number }) {
  const problems = overview?.modules.filter((m) => m.level === 'error').length ?? 0;
  const warnings = overview?.modules.filter((m) => m.level === 'warn').length ?? 0;
  return (
    <div className="card">
      <div className="card-head">
        <h2>Modules</h2>
        <span className="small muted">
          {overview?.lastHealth ? `health report ${ago(overview.lastHealth.receivedAt, now)}` : 'no health report yet'}
        </span>
        <span className="small" style={{ marginLeft: 'auto' }}>
          {problems > 0 && <span className="level error"><span className="ico">✕</span>{problems}</span>}{' '}
          {warnings > 0 && <span className="level warn"><span className="ico">!</span>{warnings}</span>}
          {problems === 0 && warnings === 0 && overview && <span className="level ok"><span className="ico">✓</span>all good</span>}
        </span>
      </div>
      <div className="card-body">
        {overview ? (
          <div className="modules">
            {overview.modules.map((m) => (
              <ModuleCard key={m.key} m={m} />
            ))}
          </div>
        ) : (
          <div className="empty">Loading…</div>
        )}
      </div>
    </div>
  );
}
