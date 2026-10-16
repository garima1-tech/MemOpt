// MemOpt dashboard - vanilla JavaScript, no external libraries.
'use strict';

// ------------------------------------------------------------ helpers ----
const $ = (sel) => document.querySelector(sel);
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const fmt = (v, d = 2) => (v === null || v === undefined || Number.isNaN(v)) ? 'n/a' : Number(v).toLocaleString(undefined, { maximumFractionDigits: d, minimumFractionDigits: 0 });
const badge = (kind) => `<span class="badge k-${esc(kind)}">${esc(kind)}</span>`;
const tier = (t) => `<span class="tier tier-${esc(t)}">${esc(t === 'NONE' ? 'unplaced' : t)}</span>`;
const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

async function api(method, path, body) {
  const opts = { method, headers: {} };
  if (body !== undefined) {
    opts.headers['Content-Type'] = 'application/json';
    opts.body = JSON.stringify(body);
  }
  let res;
  try {
    res = await fetch(path, opts);
  } catch (e) {
    throw new Error('Cannot reach the MemOpt server. Is "memopt serve" still running?');
  }
  let data = null;
  try { data = await res.json(); } catch (e) { /* not JSON */ }
  if (!res.ok) throw new Error((data && data.error) || `HTTP ${res.status}`);
  return data;
}

let toastTimer = null;
function toast(msg, isError = false) {
  const t = $('#toast');
  t.textContent = msg;
  t.className = 'toast' + (isError ? ' error' : '');
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, isError ? 9000 : 4000);
}

async function busy(btn, label, fn) {
  const old = btn.textContent;
  btn.disabled = true;
  btn.textContent = label;
  try { return await fn(); } catch (e) { toast(e.message, true); } finally { btn.disabled = false; btn.textContent = old; }
}

const kv = (pairs) => pairs.map(([k, v]) => `<div class="k">${esc(k)}</div><div class="v">${v}</div>`).join('');
const intVal = (sel) => parseInt($(sel).value, 10);

// --------------------------------------------------------------- tabs ----
function showTab(name) {
  document.querySelectorAll('.tab').forEach((b) => b.classList.toggle('active', b.dataset.tab === name));
  document.querySelectorAll('.panel').forEach((p) => p.classList.toggle('active', p.id === 'tab-' + name));
  if (name === 'placement' || name === 'profiling') loadPlacement();
  if (name === 'migrations') loadMigrations();
  if (name === 'overview') loadOverview();
}
document.querySelectorAll('.tab').forEach((b) => b.addEventListener('click', () => showTab(b.dataset.tab)));
document.querySelectorAll('[data-go]').forEach((b) => b.addEventListener('click', () => showTab(b.dataset.go)));

// ------------------------------------------------------------- charts ----
// Grouped horizontal bars: rows = [{label, bars:[{value, color, text}]}]
// Each row is scaled on its own, so rows with different units stay readable.
function barChart(el, rows, opts = {}) {
  const W = 520, rowH = 22, gap = 14, left = opts.left || 110, right = 70;
  const bars = rows.reduce((n, r) => n + r.bars.length, 0);
  const H = bars * rowH + rows.length * gap + 10;
  let y = 6, out = '';
  for (const r of rows) {
    const max = Math.max(1e-9, ...r.bars.map((b) => b.value || 0));
    out += `<text x="${left - 8}" y="${y + rowH * r.bars.length / 2 + 4}" text-anchor="end" class="lbl">${esc(r.label)}</text>`;
    for (const b of r.bars) {
      const w = Math.max(1, ((b.value || 0) / max) * (W - left - right));
      out += `<rect x="${left}" y="${y + 2}" width="${w}" height="${rowH - 6}" rx="3" fill="${b.color}"><title>${esc(b.text)}</title></rect>`;
      out += `<text x="${left + w + 6}" y="${y + rowH / 2 + 3}">${esc(b.text)}</text>`;
      y += rowH;
    }
    y += gap;
  }
  el.innerHTML = `<svg viewBox="0 0 ${W} ${H}" role="img">${out}</svg>` + (opts.legend || '');
}

function legend(items) {
  return `<div class="legend">${items.map(([c, t]) => `<span><i style="background:${c}"></i>${esc(t)}</span>`).join('')}</div>`;
}

// Line chart: series = [{name, color, values:[number|null]}], labels = x labels
function lineChart(el, labels, series) {
  const W = 520, H = 220, L = 60, R = 16, T = 12, B = 30;
  const vals = series.flatMap((s) => s.values.filter((v) => v !== null && v !== undefined));
  if (!vals.length) { el.innerHTML = '<p class="muted">No data.</p>'; return; }
  const max = Math.max(...vals) * 1.08, min = 0;
  const x = (i) => L + (labels.length === 1 ? 0 : i * (W - L - R) / (labels.length - 1));
  const y = (v) => T + (1 - (v - min) / (max - min || 1)) * (H - T - B);
  let out = `<line class="axis" x1="${L}" y1="${H - B}" x2="${W - R}" y2="${H - B}"/>`;
  for (let k = 0; k <= 4; k++) {
    const v = min + (max - min) * k / 4;
    out += `<line class="axis" x1="${L}" x2="${W - R}" y1="${y(v)}" y2="${y(v)}" stroke-dasharray="2 4"/><text x="${L - 6}" y="${y(v) + 4}" text-anchor="end">${fmt(v, 0)}</text>`;
  }
  labels.forEach((lb, i) => { out += `<text x="${x(i)}" y="${H - 10}" text-anchor="middle">${esc(lb)}</text>`; });
  for (const s of series) {
    const pts = s.values.map((v, i) => (v === null || v === undefined) ? null : [x(i), y(v)]).filter(Boolean);
    if (pts.length > 1) out += `<polyline fill="none" stroke="${s.color}" stroke-width="2.5" points="${pts.map((p) => p.join(',')).join(' ')}"/>`;
    s.values.forEach((v, i) => {
      if (v === null || v === undefined) return;
      out += `<circle cx="${x(i)}" cy="${y(v)}" r="4" fill="${s.color}"><title>${esc(s.name)} ${esc(labels[i])}: ${fmt(v, 0)}</title></circle>`;
    });
  }
  el.innerHTML = `<svg viewBox="0 0 ${W} ${H}" role="img">${out}</svg>` + legend(series.map((s) => [s.color, s.name]));
}

// Scatter of access count (x, log scale) vs AOL (y) for data units.
function scatter(el, units) {
  const pts = units.filter((u) => u.access_count > 0 && u.aol !== null && u.aol !== undefined);
  if (!pts.length) { el.innerHTML = '<p class="muted">Run a workload first (and use metric source “fallback” or a profiler that measures latency/MLP).</p>'; return; }
  const W = 520, H = 280, L = 44, R = 16, T = 12, B = 36;
  const xs = pts.map((u) => Math.log10(u.access_count)), ys = pts.map((u) => u.aol);
  const x0 = Math.floor(Math.min(...xs)), x1 = Math.ceil(Math.max(...xs) + 0.01);
  const y1 = Math.ceil(Math.max(...ys) + 1);
  const X = (v) => L + (v - x0) / (x1 - x0 || 1) * (W - L - R);
  const Y = (v) => T + (1 - v / y1) * (H - T - B);
  let out = `<line class="axis" x1="${L}" y1="${H - B}" x2="${W - R}" y2="${H - B}"/><line class="axis" x1="${L}" y1="${T}" x2="${L}" y2="${H - B}"/>`;
  for (let d = x0; d <= x1; d++) out += `<text x="${X(d)}" y="${H - B + 14}" text-anchor="middle">${fmt(Math.pow(10, d), 0)}</text>`;
  for (let v = 0; v <= y1; v += Math.max(1, Math.round(y1 / 5))) out += `<text x="${L - 6}" y="${Y(v) + 4}" text-anchor="end">${v}</text>`;
  out += `<text x="${(L + W - R) / 2}" y="${H - 4}" text-anchor="middle">accesses (row touches, log scale) → hotter</text>`;
  out += `<text transform="translate(12 ${(T + H - B) / 2}) rotate(-90)" text-anchor="middle">AOL ↑</text>`;
  for (const t of [window.memoptCfg?.aol_high, window.memoptCfg?.aol_low]) {
    if (t === undefined || t > y1) continue;
    out += `<line x1="${L}" x2="${W - R}" y1="${Y(t)}" y2="${Y(t)}" stroke="${css('--k-estimated')}" stroke-dasharray="4 4"/><text x="${W - R}" y="${Y(t) - 4}" text-anchor="end">AOL ${t}</text>`;
  }
  for (const u of pts) {
    const fast = u.memopt_proposed === 'FAST', hot = u.hotness_proposed === 'FAST';
    const fill = fast ? css('--fast') : css('--slow');
    out += `<circle cx="${X(Math.log10(u.access_count))}" cy="${Y(u.aol)}" r="${fast ? 6 : 4.5}" fill="${fill}" ${hot ? `stroke="${css('--hot')}" stroke-width="2.5"` : ''}><title>${esc(u.unit_id)}: ${u.access_count} accesses, AOL ${fmt(u.aol)}</title></circle>`;
  }
  el.innerHTML = `<svg viewBox="0 0 ${W} ${H}" role="img">${out}</svg>` +
    legend([[css('--fast'), 'MemOpt → FAST'], [css('--slow'), 'MemOpt → SLOW'], [css('--hot'), 'outline: hotness → FAST']]);
}

// ----------------------------------------------------------- overview ----
async function loadOverview() {
  try {
    const s = await api('GET', '/api/database/status');
    const pill = $('#conn-pill');
    pill.className = 'pill ' + (s.connected ? 'pill-ok' : 'pill-bad');
    pill.textContent = s.connected ? `● ${s.database} connected` : '● database not connected';
    $('#ov-db').textContent = s.connected ? s.database : 'not connected';
    $('#ov-db-sub').textContent = s.connected ? `PostgreSQL ${s.server_version} · ${s.user}@${s.host}:${s.port}${s.read_only ? ' · read-only' : ''}` : (s.error || '');
    $('#ov-rows').textContent = s.connected ? fmt(s.data_rows, 0) : '–';
    if (s.latest_workload) {
      $('#ov-wl').textContent = s.latest_workload.workload;
      $('#ov-wl-sub').textContent = `${s.latest_workload.success} ok / ${s.latest_workload.failure} failed · ${s.latest_workload.started_at}`;
    }
    if (s.latest_benchmark) {
      $('#ov-bench').textContent = s.latest_benchmark.ok ? 'completed' : 'failed';
      $('#ov-bench-sub').textContent = `${s.latest_benchmark.benchmark_id}`;
    }
    $('#ov-warnings').innerHTML = (s.warnings || []).length
      ? `<div class="card warn"><h2>Warnings</h2><ul>${s.warnings.map((w) => `<li>${esc(w)}</li>`).join('')}</ul></div>` : '';
  } catch (e) {
    $('#conn-pill').className = 'pill pill-bad';
    $('#conn-pill').textContent = '● server unreachable';
    toast(e.message, true);
  }
  try {
    const c = await api('GET', '/api/config');
    window.memoptCfg = c;
    $('#ov-config').innerHTML = kv([
      ['Metric source', `<code>${esc(c.metric_source)}</code> ${c.metric_source === 'fallback' ? badge('synthetic') + ' latency, ' + badge('estimated') + ' MLP' : ''}`],
      ['Profiler', `${esc(c.profiler)} – ${esc(c.profiler_note)}`],
      ['FAST capacity', `${c.fast_capacity_units} data units (of 34)`],
      ['ALTO thresholds', `AOL &gt; ${c.aol_high} → FAST, AOL &lt; ${c.aol_low} → SLOW, cooldown ${c.cooldown_epochs}, confirm ${c.confirm_epochs}, swap margin ${fmt(c.swap_margin * 100, 0)}%`],
      ['Model assumptions', `base latency ${c.base_latency_ns} ns · MLP seq ${c.mlp_sequential} / random ${c.mlp_random} / repeated ${c.mlp_repeated} · SLOW penalty ×${c.slow_tier_penalty}`],
      ['Note', esc(c.model_note)],
    ]);
  } catch (e) { /* shown above */ }
}

// ----------------------------------------------------------- database ----
$('#btn-db-test').addEventListener('click', (ev) => busy(ev.target, 'Testing…', async () => {
  try {
    const r = await api('POST', '/api/database/test');
    $('#db-status').innerHTML = kv([['Result', `<strong class="k-measured">${esc(r.message)}</strong>`], ['Server version', esc(r.server_version)], ['Backend PID', esc(r.backend_pid)]]);
    toast('Connection OK');
  } catch (e) {
    $('#db-status').innerHTML = kv([['Result', `<strong style="color:var(--danger)">Connection failed</strong>`], ['Details', esc(e.message)]]);
    throw e;
  } finally {
    loadOverview();
  }
}));

$('#btn-db-tables').addEventListener('click', (ev) => busy(ev.target, 'Checking…', async () => {
  const r = await api('GET', '/api/database/tables');
  $('#db-tables').innerHTML = `<table><thead><tr><th>Table</th><th>Purpose</th><th>Status</th><th class="num">Rows</th><th class="num">Expected</th></tr></thead><tbody>${
    r.tables.map((t) => `<tr><td><code>${esc(t.name)}</code></td><td>${esc(t.kind)}</td><td>${t.exists ? '<span class="k-measured">present</span>' : '<strong style="color:var(--danger)">MISSING</strong>'}</td><td class="num">${t.rows >= 0 ? fmt(t.rows, 0) : '–'}</td><td class="num">${t.expected_rows ? fmt(t.expected_rows, 0) : '–'}</td></tr>`).join('')
  }</tbody></table>${r.hint ? `<p><strong>${esc(r.hint)}</strong></p>` : ''}`;
}));

// ----------------------------------------------------------- workload ----
function renderWorkload(r) {
  $('#wl-summary').innerHTML = kv([
    ['Workload', `<strong>${esc(r.workload)}</strong> · ${r.repetitions} measured repetition(s), ${r.warmup_runs} warm-up`],
    ['Queries', `${fmt(r.success + r.failure, 0)} executed · <span class="k-measured">${fmt(r.success, 0)} ok</span> · ${r.failure ? `<strong style="color:var(--danger)">${r.failure} failed</strong>` : '0 failed'}`],
    ['Measured time', `${fmt(r.total_ms, 1)} ms total · ${fmt(r.avg_query_ms, 4)} ms average per query ${badge('measured')}`],
    ['Started', esc(r.started_at)],
  ]) + (r.errors.length ? `<div class="k">Errors</div><div class="v">${r.errors.map(esc).join('<br>')}</div>` : '');
  $('#wl-table').innerHTML = `<div class="scroll"><table><thead><tr><th>Query</th><th class="num">Executions</th><th class="num">Total ms</th><th class="num">Avg ms</th><th class="num">Min ms</th><th class="num">Max ms</th><th class="num">Rows</th><th class="num">OK</th><th class="num">Failed</th></tr></thead><tbody>${
    r.queries.map((q) => `<tr><td><code>${esc(q.name)}</code></td><td class="num">${q.executions}</td><td class="num">${fmt(q.total_ms, 2)}</td><td class="num">${fmt(q.avg_ms, 4)}</td><td class="num">${fmt(q.min_ms, 4)}</td><td class="num">${fmt(q.max_ms, 4)}</td><td class="num">${fmt(q.rows, 0)}</td><td class="num">${q.success}</td><td class="num">${q.failure}</td></tr>`).join('')
  }</tbody></table></div>`;
  barChart($('#wl-chart'), r.queries.map((q) => ({ label: q.name.replace(/_/g, ' ').slice(0, 26), bars: [{ value: q.avg_ms, color: css('--accent'), text: `${fmt(q.avg_ms, 4)} ms avg` }] })), { left: 190 });
}

$('#btn-wl-run').addEventListener('click', (ev) => busy(ev.target, 'Running SQL…', async () => {
  const r = await api('POST', '/api/workload/run', { workload: $('#wl-type').value, repetitions: intVal('#wl-reps'), warmup: intVal('#wl-warm') });
  renderWorkload(r);
  toast(`Workload finished: ${r.success} queries ok, ${r.failure} failed`);
  loadOverview();
}));

// ---------------------------------------------------------- profiling ----
function metricCell(m) {
  return `${m.value === null ? '<span class="muted">n/a</span>' : fmt(m.value, 2)} ${badge(m.kind)}`;
}

$('#btn-pf-run').addEventListener('click', (ev) => busy(ev.target, 'Profiling…', async () => {
  const r = await api('POST', '/api/profiling/run', { workload: $('#pf-type').value, repetitions: intVal('#pf-reps') });
  $('#pf-head').innerHTML = kv([
    ['Profiler', esc(r.profiler)],
    ['Observed process', esc(r.scope)],
    ['Status', esc(r.status)],
    ['Query duration', `${fmt(r.query_duration.queries, 0)} queries · ${fmt(r.query_duration.avg_ms, 4)} ms avg · ${fmt(r.query_duration.total_ms, 1)} ms total ${badge('measured')} <span class="muted">(${esc(r.query_duration.source)})</span>`],
  ]);
  $('#pf-table').innerHTML = `<table><thead><tr><th>Counter</th><th class="num">Value</th><th>Unit</th><th>Kind</th><th>Source</th><th>Status</th></tr></thead><tbody>${
    r.counters.map((m) => `<tr><td><code>${esc(m.name)}</code></td><td class="num">${m.value === null ? 'n/a' : fmt(m.value, 2)}</td><td>${esc(m.unit)}</td><td>${badge(m.kind)}</td><td>${esc(m.source)}</td><td class="reason">${esc(m.status)}</td></tr>`).join('')
  }</tbody></table>`;
  toast('Profiling finished');
}));

// ----------------------------------------------------------- placement ---
let lastPlacement = null;

function renderPlacement(p) {
  lastPlacement = p;
  const onlyActive = $('#pl-only-active').checked;
  const units = p.units.filter((u) => !onlyActive || u.access_count > 0);
  scatter($('#pl-scatter'), p.units);

  // Profiling tab: AOL inputs.
  $('#pf-units').innerHTML = p.has_workload_data ? `<div class="scroll"><table><thead><tr><th>Unit</th><th class="num">Accesses</th><th>Latency (ns)</th><th>MLP</th><th>AOL</th><th>Status</th></tr></thead><tbody>${
    p.units.filter((u) => u.access_count > 0).map((u) => `<tr><td><code>${esc(u.unit_id)}</code></td><td class="num">${u.access_count}</td><td>${metricCell(u.latency)}</td><td>${metricCell(u.mlp)}</td><td>${u.aol === null ? 'n/a' : fmt(u.aol)} ${badge(u.aol_kind)}</td><td class="reason">${esc(u.aol_status)}</td></tr>`).join('')
  }</tbody></table></div>` : '<p class="muted">Run a workload first.</p>';

  // FAST picks comparison.
  const hot = p.units.filter((u) => u.hotness_proposed === 'FAST').map((u) => u.unit_id);
  const mem = p.units.filter((u) => u.memopt_proposed === 'FAST').map((u) => u.unit_id);
  const li = (arr, other) => arr.length ? arr.map((id) => `<li class="${other.includes(id) ? '' : 'only'}">${esc(id)}</li>`).join('') : '<li class="muted">– simulate first –</li>';
  $('#pl-picks').innerHTML = `<div class="pick-list"><div><h3 style="color:var(--hot)">Hotness baseline</h3><ul>${li(hot, mem)}</ul></div><div><h3 style="color:var(--accent)">MemOpt (AOL)</h3><ul>${li(mem, hot)}</ul></div></div>
    <p class="muted">Bold = chosen by only one policy. Folders now: FAST ${p.fast_in_folders}, SLOW ${p.slow_in_folders} (capacity ${p.fast_capacity}).</p>`;

  if (!p.has_workload_data) { $('#pl-table').innerHTML = '<p class="muted">Run a workload first.</p>'; return; }
  const maxAcc = Math.max(1, ...units.map((u) => u.access_count));
  $('#pl-table').innerHTML = `<table><thead><tr>
      <th>Unit</th><th>Keys</th><th class="num">Accesses</th><th>Pattern</th><th>Latency ns</th><th>MLP</th><th>AOL</th>
      <th class="num">Hotness rank</th><th class="num">MemOpt rank</th><th>Current</th><th>Hotness →</th><th>MemOpt →</th><th>MemOpt reason</th></tr></thead><tbody>${
    units.map((u) => {
      const a = u.access_count || 1;
      const mix = `<div class="mix" title="seq ${u.seq_touches} / random ${u.random_touches} / repeated ${u.repeated_touches}"><span class="s" style="width:${u.seq_touches / a * 100}%"></span><span class="r" style="width:${u.random_touches / a * 100}%"></span><span class="p" style="width:${u.repeated_touches / a * 100}%"></span></div>`;
      const dis = u.hotness_proposed !== '-' && u.memopt_proposed !== '-' && u.hotness_proposed !== u.memopt_proposed;
      return `<tr class="${dis ? 'disagree' : ''}"><td><code>${esc(u.unit_id)}</code></td><td>${esc(u.key_range)}</td>
        <td class="num"><span title="${u.access_count} / ${maxAcc}">${fmt(u.access_count, 0)}</span></td><td>${mix}</td>
        <td>${metricCell(u.latency)}</td><td>${metricCell(u.mlp)}</td><td>${u.aol === null ? 'n/a' : `<strong>${fmt(u.aol)}</strong>`}</td>
        <td class="num">${u.hotness_rank ?? '–'}</td><td class="num">${u.memopt_rank ?? '–'}</td>
        <td>${tier(u.current_tier)}</td><td>${tier(u.hotness_proposed)}</td><td>${tier(u.memopt_proposed)}</td><td class="reason">${esc(u.memopt_reason)}</td></tr>`;
    }).join('')
  }</tbody></table>${legend([[css('--k-simulated'), 'sequential'], [css('--hot'), 'random'], [css('--k-synthetic'), 'repeated']])}
  <p class="muted">Highlighted rows: hotness and MemOpt disagree. Hotness rank = order by accesses; MemOpt rank = order by accesses × AOL.</p>`;
}

async function loadPlacement() {
  try { renderPlacement(await api('GET', '/api/placement')); } catch (e) { toast(e.message, true); }
}
$('#pl-only-active').addEventListener('change', () => lastPlacement && renderPlacement(lastPlacement));

$('#btn-pl-sim').addEventListener('click', (ev) => busy(ev.target, 'Simulating…', async () => {
  const r = await api('POST', '/api/placement/simulate', { policy: $('#pl-policy').value, mode: $('#pl-mode').value, dry_run: $('#pl-dry').checked });
  $('#pl-summary').textContent = r.summary;
  await loadPlacement();
  toast('Placement simulated');
}));

$('#btn-pl-reset').addEventListener('click', (ev) => {
  if (!confirm('Remove all .unit files from fast_memory/ and slow_memory/?')) return;
  busy(ev.target, 'Resetting…', async () => {
    const r = await api('POST', '/api/tiers/reset');
    $('#pl-summary').textContent = r.message;
    await loadPlacement();
  });
});

// ---------------------------------------------------------- migrations ---
function migrationTable(list) {
  if (!list.length) return '<p class="muted">No migrations.</p>';
  return `<div class="scroll"><table><thead><tr><th>Time</th><th>Policy</th><th class="num">Epoch</th><th>Unit</th><th>Old</th><th>New</th><th>Trigger</th><th class="num">Value</th><th>Mode</th><th>Reason</th></tr></thead><tbody>${
    list.map((m) => `<tr><td>${esc(m.timestamp)}</td><td>${esc(m.policy)}</td><td class="num">${m.epoch}</td><td><code>${esc(m.unit_id)}</code></td><td>${tier(m.old_tier)}</td><td>${tier(m.new_tier)}</td><td>${esc(m.trigger_metric)}</td><td class="num">${fmt(m.trigger_value)}</td><td>${m.dry_run ? 'dry run' : (m.applied ? 'applied' : 'simulated')}</td><td class="reason">${esc(m.reason)}</td></tr>`).join('')
  }</tbody></table></div>`;
}

async function loadMigrations() {
  try {
    const r = await api('GET', '/api/migrations');
    $('#mg-live').innerHTML = migrationTable(r.live);
    $('#mg-bench').innerHTML = `<h3>Hotness baseline (${r.benchmark_hotness.length})</h3>${migrationTable(r.benchmark_hotness)}<h3>MemOpt / ALTO-inspired (${r.benchmark_memopt.length})</h3>${migrationTable(r.benchmark_memopt)}`;
  } catch (e) { toast(e.message, true); }
}

// ----------------------------------------------------------- benchmark ---
function renderBenchmark(b) {
  $('#bm-out').classList.add('show');
  const H = b.hotness, M = b.memopt, cHot = css('--hot'), cMem = css('--accent');
  const pct = (v) => v === null || v === undefined ? 'n/a' : `${v > 0 ? '+' : ''}${fmt(v, 2)} %`;
  $('#bm-cards').innerHTML = `
    <div class="card stat"><span class="label">Benchmark</span><span class="value">${esc(b.epochs)} × ${esc(b.repetitions)}</span><span class="sub">epochs × repetitions · ${esc(b.benchmark_id)}</span></div>
    <div class="card stat"><span class="label">Queries per policy</span><span class="value">${fmt(H.success + H.failure, 0)}</span><span class="sub">${H.failure + M.failure} failed in total ${badge('measured')}</span></div>
    <div class="card stat"><span class="label">Query time, MemOpt vs hotness</span><span class="value">${pct(b.time_diff_pct)}</span><span class="sub">same SQL → this is noise ${badge('measured')}</span></div>
    <div class="card stat"><span class="label">Stall cost, MemOpt vs hotness</span><span class="value">${pct(b.cost_diff_pct)}</span><span class="sub">model output ${badge('simulated')}</span></div>`;
  const lg = legend([[cHot, 'hotness'], [cMem, 'MemOpt']]);
  barChart($('#bm-chart-time'), [
    { label: 'total ms', bars: [{ value: H.total_workload_ms, color: cHot, text: `${fmt(H.total_workload_ms, 1)} ms` }, { value: M.total_workload_ms, color: cMem, text: `${fmt(M.total_workload_ms, 1)} ms` }] },
    { label: 'avg query ms', bars: [{ value: H.avg_query_ms, color: cHot, text: `${fmt(H.avg_query_ms, 4)} ms` }, { value: M.avg_query_ms, color: cMem, text: `${fmt(M.avg_query_ms, 4)} ms` }] },
  ], { legend: lg });
  barChart($('#bm-chart-cost'), [
    { label: 'stall cost', bars: [{ value: H.simulated_stall_cost, color: cHot, text: fmt(H.simulated_stall_cost, 0) }, { value: M.simulated_stall_cost, color: cMem, text: fmt(M.simulated_stall_cost, 0) }] },
  ], { legend: lg });
  const labels = H.epoch_cost.map((_, i) => `epoch ${i + 1}`);
  lineChart($('#bm-chart-epoch'), labels, [{ name: 'hotness', color: cHot, values: H.epoch_cost }, { name: 'MemOpt', color: cMem, values: M.epoch_cost }]);
  barChart($('#bm-chart-mig'), [
    { label: 'migrations', bars: [{ value: H.migration_count, color: cHot, text: `${H.migration_count}` }, { value: M.migration_count, color: cMem, text: `${M.migration_count}` }] },
    { label: 'FAST units', bars: [{ value: H.fast_units, color: cHot, text: `${H.fast_units}` }, { value: M.fast_units, color: cMem, text: `${M.fast_units}` }] },
    { label: 'SLOW units', bars: [{ value: H.slow_units, color: cHot, text: `${H.slow_units}` }, { value: M.slow_units, color: cMem, text: `${M.slow_units}` }] },
  ], { legend: lg });
  const row = (p) => `<tr><td><strong>${esc(p.policy)}</strong></td><td class="num">${fmt(p.total_workload_ms, 1)}</td><td class="num">${fmt(p.avg_query_ms, 4)}</td><td class="num">${p.success}</td><td class="num">${p.failure}</td><td class="num">${p.fast_units}</td><td class="num">${p.slow_units}</td><td class="num">${p.migration_count}</td><td class="num">${fmt(p.simulated_stall_cost, 0)}</td><td>${badge(p.metric_basis)}</td><td class="reason">${esc(p.profile.profiler)}: ${esc(p.profile.status)}</td><td><code>${p.final_fast_units.map(esc).join(' ')}</code></td></tr>`;
  $('#bm-table').innerHTML = `<table><thead><tr><th>Policy</th><th class="num">Total ms</th><th class="num">Avg query ms</th><th class="num">OK</th><th class="num">Failed</th><th class="num">FAST</th><th class="num">SLOW</th><th class="num">Migrations</th><th class="num">Sim. stall cost</th><th>AOL basis</th><th>Profiler</th><th>Final FAST units</th></tr></thead><tbody>${row(H)}${row(M)}</tbody></table>
    <p class="muted">CSV saved on the server: <code>${esc(b.csv_path || 'not saved')}</code></p>`;
  $('#bm-notes').innerHTML = b.notes.map((n) => `<li>${esc(n)}</li>`).join('') + (b.error ? `<li><strong>Error:</strong> ${esc(b.error)}</li>` : '');
}

$('#btn-bm-run').addEventListener('click', (ev) => busy(ev.target, 'Running benchmark…', async () => {
  const r = await api('POST', '/api/benchmark/run', { epochs: intVal('#bm-epochs'), repetitions: intVal('#bm-reps'), warmup: intVal('#bm-warm') });
  renderBenchmark(r);
  toast('Benchmark finished');
  loadOverview();
}));

// ------------------------------------------------------------- startup ---
(async function init() {
  await loadOverview();
  try { renderWorkload(await api('GET', '/api/workload/latest')); } catch (e) { /* none yet */ }
  try { renderBenchmark(await api('GET', '/api/benchmark/latest')); } catch (e) { /* none yet */ }
})();
