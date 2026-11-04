// ============================================================
// HIDE · Hardware Input Dynamic Emulator · lógica do frontend
// ============================================================
const APP_VERSION = '1.0';
const FIRMWARE_VERSION = '1.0';

const reduceMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
const recoilPanel = document.getElementById('recoil-panel');
const cpsPanel = document.getElementById('cps-panel');
const state = { dx: 0, dy: 0, cps: 13 };

// -------- Som --------
const Sound = (() => {
  let ctx=null, enabled=true, lastTick=0;
  try { enabled = localStorage.getItem('sound') !== 'off'; } catch {}
  function tone(freq,{to=null,type='sine',duration=.08,volume=.06,delay=0}={}){
    if(!enabled) return;
    ctx = ctx || new (window.AudioContext||window.webkitAudioContext)();
    if(ctx.state==='suspended') ctx.resume();
    const start=ctx.currentTime+delay, osc=ctx.createOscillator(), gain=ctx.createGain();
    osc.type=type; osc.frequency.setValueAtTime(freq,start);
    if(to) osc.frequency.exponentialRampToValueAtTime(to,start+duration);
    gain.gain.setValueAtTime(.0001,start);
    gain.gain.exponentialRampToValueAtTime(volume,start+.005);
    gain.gain.exponentialRampToValueAtTime(.0001,start+duration);
    osc.connect(gain).connect(ctx.destination);
    osc.start(start); osc.stop(start+duration+.05);
  }
  return {
    get enabled(){ return enabled; },
    setEnabled(v){ enabled=v; try{ localStorage.setItem('sound', v?'on':'off'); }catch{} },
    tick(r){ const n=performance.now(); if(n-lastTick<40) return; lastTick=n; tone(500+r*900,{type:'triangle',duration:.03,volume:.03}); },
    click(){ tone(1100,{to:600,type:'triangle',duration:.05,volume:.05}); },
    tab(){ tone(660,{to:880,type:'triangle',duration:.07,volume:.04}); },
    on(){ tone(520,{duration:.07}); tone(780,{duration:.09,delay:.06}); },
    off(){ tone(780,{duration:.07}); tone(520,{duration:.09,delay:.06}); },
    warn(){ tone(220,{type:'square',duration:.08,volume:.025}); tone(220,{type:'square',duration:.08,volume:.025,delay:.12}); },
    success(){ [523,659,784].forEach((f,i)=>tone(f,{duration:.12,volume:.05,delay:i*.07})); },
    error(){ tone(330,{to:180,type:'sawtooth',duration:.22,volume:.03}); }
  };
})();

// -------- Sliders --------
function decimalsOf(i){ const s=i.step||'1'; return s.includes('.')?s.split('.')[1].length:0; }
function updateFill(s){ const min=+s.min,max=+s.max,r=(+s.value-min)/(max-min); s.style.setProperty('--ratio',r); return r; }
function flashValue(i){ const b=i.closest('.value-box'); if(!b) return; b.classList.add('live'); clearTimeout(b.liveTimer); b.liveTimer=setTimeout(()=>b.classList.remove('live'),500); }
function syncInput(id,v){
  const i=document.getElementById(id); i.value=Number(v).toFixed(decimalsOf(i));
  const r=updateFill(document.getElementById(id+'-slider'));
  flashValue(i); Sound.tick(r); readValues();
}
function syncSlider(id,v,clamp=false){
  const s=document.getElementById(id+'-slider'), i=document.getElementById(id); let n=parseFloat(v);
  if(Number.isNaN(n)){ if(!clamp) return; n=+s.value; }
  if(clamp){ n=Math.min(+s.max,Math.max(+s.min,n)); i.value=n.toFixed(decimalsOf(i)); }
  s.value=n; updateFill(s); readValues();
}
function readValues(){
  const v = id => parseFloat(document.getElementById(id+'-slider').value);
  const f = n => (n<0?'−':'+')+Math.abs(n).toFixed(2);
  state.dx = v('x-right') - v('x-left');
  state.dy = v('y-down') - v('y-up');
  state.cps = v('cps');
  document.getElementById('vector-readout').textContent = `Δx ${f(state.dx)} · Δy ${f(state.dy)} counts/s`;
  cpsPanel.classList.toggle('over-limit', state.cps > 15);
}
document.querySelectorAll('input[type="range"]').forEach(slider=>{
  const id = slider.id.replace(/-slider$/,''), input = document.getElementById(id);
  updateFill(slider);
  slider.addEventListener('input', ()=> syncInput(id, slider.value));
  input.addEventListener('input', ()=> syncSlider(id, input.value));
  input.addEventListener('change', ()=> { syncSlider(id, input.value, true); Sound.click(); });
});

// -------- Abas --------
const indicator = document.querySelector('.profile-indicator');
function moveIndicator(){
  const a = document.querySelector('.tab-btn.active');
  indicator.style.width = a.offsetWidth+'px';
  indicator.style.transform = `translateX(${a.offsetLeft}px)`;
}
function selectTab(id){
  document.querySelectorAll('.tab-btn').forEach(t=>{
    const on = t.dataset.profile===id;
    t.classList.toggle('active', on);
    t.setAttribute('aria-selected', on);
  });
  moveIndicator();
}
function switchTab(btn){
  if(btn.classList.contains('active')) return;
  profiles[activeProfile] = readProfile();
  activeProfile = btn.dataset.profile;
  selectTab(activeProfile);
  applyProfile(profiles[activeProfile], false);
  Sound.tab();
  updateConfigStatus();
}
document.querySelectorAll('.tab-btn').forEach(t=> t.addEventListener('click', ()=>switchTab(t)));

// -------- Painéis on/off --------
function syncPanel(t){ document.getElementById(t.dataset.panel).classList.toggle('is-on', t.checked); }
document.querySelectorAll('[data-panel]').forEach(t=>{
  syncPanel(t);
  t.addEventListener('change', ()=>{ syncPanel(t); t.checked ? Sound.on() : Sound.off(); });
});

// -------- Override CPS --------
function applyOverride(on){
  const s = document.getElementById('cps-slider'), i = document.getElementById('cps');
  const max = on ? 20 : 15;
  s.max = max; i.max = max;
  document.getElementById('cps-max-label').textContent = max;
  document.getElementById('warning-box').classList.toggle('active', on);
  if(!on && +i.value > 15){ i.value = 15; s.value = 15; }
  updateFill(s); readValues();
}
document.getElementById('override-checkbox').addEventListener('change', ()=>{
  const on = document.getElementById('override-checkbox').checked;
  applyOverride(on);
  on ? Sound.warn() : Sound.off();
});

// -------- Conversões CPS ↔ intervalo --------
function cpsToInterval(cps){ return Math.max(50, Math.round(1000/cps)); }

// -------- Conversor RPM -> CPS --------
function rpmToCps(rpm){ return Math.max(1, Math.round(rpm / 60)); }
const rpmInput = document.getElementById('rpm-input');
const rpmResult = document.getElementById('rpm-result');
rpmInput.addEventListener('input', function(){
  const rpm = parseFloat(this.value);
  rpmResult.textContent = (Number.isFinite(rpm) && rpm > 0) ? rpmToCps(rpm) : '--';
});
document.getElementById('rpm-apply-btn').addEventListener('click', function(){
  const rpm = parseFloat(rpmInput.value);
  if (!Number.isFinite(rpm) || rpm <= 0) { showToast('Digite um RPM válido.', 'error'); return; }
  let cps = rpmToCps(rpm);
  if (cps > 20) cps = 20;
  if (cps > 15) {
    document.getElementById('override-checkbox').checked = true;
    applyOverride(true);
  }
  setRange('cps', cps);
  updateConfigStatus();
  Sound.click();
  showToast(`Aplicado: ${rpm} RPM = ${cps} CPS`);
});

// ============================================================
// ORGANIC DEFAULTS · espelham os #define do firmware
// ============================================================
const DEFAULT_ORGANIC = {
  // Fast-fire
  uniform:        { enabled: true },
  gaussian:       { enabled: true },
  randomwalk:     { enabled: true },
  jitterUniform:  { enabled: true },

  // Recoil Control
  reaction: {
    enabled: true,
    minMs: 100,
    maxMs: 380,
    medianMs: 200,
    sigma: 0.25,
    distractPct: 3,
    distractAdd: 250
  },
  lognormal: {
    enabled: true,
    stepMedianMs: 50,
    stepSigma: 0.35,
    stepMinMs: 25,
    stepMaxMs: 150
  },
  ornsteinUhlenbeck: { enabled: true },
  pink: {
    enabled: true,
    tauAMs: 800,
    tauBMs: 400,
    tauCMs: 200,
    weightA: 0.25,
    weightB: 0.33,
    weightC: 0.42,
    rateMod: 0.30
  },
  smoothstep: { enabled: true },
  bezier2:    { enabled: true },
  jitterGauss: {
    enabled: true,
    scale: 0.05
  },
  tremor10hz: {
    enabled: true,
    hz: 10.0,
    amp: 0.4
  },
  overshoot: {
    enabled: true,
    pct: 15,
    min: 0.03,
    max: 0.12,
    correction: 0.5
  },
  fitts: {
    enabled: true,
    scale: 0.30,
    max: 0.50
  },
  fatigue: {
    enabled: true,
    halfMs: 60000,
    max: 0.60
  },
  pause: {
    enabled: true,
    pct: 2,
    minMs: 30,
    maxMs: 90,
    reactiveDriftPx: 6.0,
    reactivePausePct: 15,
    reactiveDamp: 0.70
  }
};

let organicSettings = JSON.parse(JSON.stringify(DEFAULT_ORGANIC));

// -------- Path helpers (para orgânico nested) --------
function getPath(obj, path) {
  return path.split('.').reduce((o, k) => o == null ? undefined : o[k], obj);
}
function setPath(obj, path, value) {
  const keys = path.split('.');
  const last = keys.pop();
  const target = keys.reduce((o, k) => {
    if (!o[k] || typeof o[k] !== 'object') o[k] = {};
    return o[k];
  }, obj);
  target[last] = value;
}

// -------- Leitura / escrita do DOM orgânico --------
function readOrganicFromDOM() {
  document.querySelectorAll('[data-organic-toggle]').forEach(t => {
    const key = t.dataset.organicToggle;
    if (organicSettings[key]) organicSettings[key].enabled = t.checked;
  });
  document.querySelectorAll('[data-organic-val]').forEach(i => {
    const path = i.dataset.organicVal;
    const num = parseFloat(i.value);
    if (!Number.isFinite(num)) return;
    setPath(organicSettings, path, num);
  });
}

function writeOrganicToDOM() {
  document.querySelectorAll('[data-organic-toggle]').forEach(t => {
    const key = t.dataset.organicToggle;
    if (organicSettings[key]) t.checked = !!organicSettings[key].enabled;
  });
  document.querySelectorAll('[data-organic-val]').forEach(i => {
    const path = i.dataset.organicVal;
    const v = getPath(organicSettings, path);
    if (v !== undefined && v !== null) i.value = v;
  });
}

// Merge profundo de organic recebido com defaults (garante campos faltantes)
function mergeOrganic(src) {
  const out = JSON.parse(JSON.stringify(DEFAULT_ORGANIC));
  if (!src || typeof src !== 'object') return out;
  for (const key in out) {
    if (!(key in src)) continue;
    const s = src[key];
    if (!s || typeof s !== 'object') continue;
    if (typeof s.enabled === 'boolean') out[key].enabled = s.enabled;
    for (const p in out[key]) {
      if (p === 'enabled') continue;
      if (typeof s[p] === 'number' && Number.isFinite(s[p])) out[key][p] = s[p];
    }
  }
  return out;
}

// Bind inputs
document.querySelectorAll('[data-organic-toggle], [data-organic-val]').forEach(el => {
  el.addEventListener('change', () => { readOrganicFromDOM(); updateConfigStatus(); });
});

// ============================================================
// RESETS
// ============================================================

// Reset Bio-Emulation
document.getElementById('reset-organic-btn').addEventListener('click', () => {
  organicSettings = JSON.parse(JSON.stringify(DEFAULT_ORGANIC));
  writeOrganicToDOM();
  updateConfigStatus();
  Sound.success();
  showToast('Bio-Emulation restaurada para os valores do firmware.');
});

// Reset Fast-fire (13 CPS)
document.getElementById('reset-fastfire-btn').addEventListener('click', () => {
  setRange('cps', 13);
  document.getElementById('override-checkbox').checked = false;
  applyOverride(false);
  updateConfigStatus();
  Sound.success();
  showToast('Fast-fire resetado para 13 CPS.');
});

// Reset Recoil (tudo zero)
document.getElementById('reset-recoil-btn').addEventListener('click', () => {
  setRange('y-down', 0);
  setRange('y-up', 0);
  setRange('x-left', 0);
  setRange('x-right', 0);
  document.getElementById('update-delay').value = 0;
  document.getElementById('trigger').value = 'secondary+primary';
  readValues();
  updateConfigStatus();
  Sound.success();
  showToast('Recoil resetado: todos os valores zerados.');
});

// ============================================================
// CONFIG · leitura, aplicação, migração
// ============================================================
const CONFIG_FORMAT = 'pico-hid-config';
const CONFIG_VERSION = 3;
const FILE_TYPES = [{ description:'Config do Pico (JSON)', accept:{ 'application/json':['.json'] } }];
const PROFILE_IDS = [...document.querySelectorAll('.tab-btn')].map(t=>t.dataset.profile);

function clampToInput(i,v){
  const n = Math.round(Number(v)), min = +i.min;
  if(!Number.isFinite(n)) return min;
  return Math.min(+i.max, Math.max(min, n));
}

function readProfile(){
  readOrganicFromDOM();
  const v = id => parseFloat(document.getElementById(id+'-slider').value);
  const c = id => document.getElementById(id).checked;
  const d = document.getElementById('update-delay');
  return {
    recoilControl: {
      enabled: c('recoil-enabled'),
      yDown: v('y-down'), yUp: v('y-up'),
      xLeft: v('x-left'), xRight: v('x-right'),
      updateDelay: clampToInput(d, d.value),
      trigger: document.getElementById('trigger').value
    },
    clicksPerSecond: {
      enabled: c('cps-master'),
      cps: v('cps'),
      advanced: c('override-checkbox')
    },
    organic: JSON.parse(JSON.stringify(organicSettings))
  };
}

function setRange(id, v){
  const s=document.getElementById(id+'-slider'), i=document.getElementById(id);
  s.value=v;
  i.value=parseFloat(s.value).toFixed(decimalsOf(i));
  updateFill(s);
}

/**
 * Aplica um perfil.
 * @param {object} p
 * @param {boolean} keepOrganicIfMissing
 *   - false (default, ex: carregar arquivo): se `organic` ausente, usa DEFAULT_ORGANIC
 *   - true  (ex: "Ler do Pico"): se `organic` ausente, mantém o estado atual
 */
function applyProfile(p, keepOrganicIfMissing = false){
  const r = p.recoilControl || {}, c = p.clicksPerSecond || {};
  const trig = document.getElementById('trigger'), delay = document.getElementById('update-delay');

  document.getElementById('recoil-enabled').checked = !!r.enabled;
  document.getElementById('cps-master').checked = !!c.enabled;
  document.querySelectorAll('[data-panel]').forEach(syncPanel);

  setRange('y-down', r.yDown ?? 0);
  setRange('y-up', r.yUp ?? 0);
  setRange('x-left', r.xLeft ?? 0);
  setRange('x-right', r.xRight ?? 0);
  delay.value = clampToInput(delay, r.updateDelay ?? 0);
  if([...trig.options].some(o=>o.value===r.trigger)) trig.value = r.trigger;

  document.getElementById('override-checkbox').checked = !!c.advanced;
  applyOverride(!!c.advanced);
  setRange('cps', c.cps ?? 13);

  // Organic: merge com defaults para campos faltantes
  if (p.organic) {
    organicSettings = mergeOrganic(p.organic);
  } else if (!keepOrganicIfMissing) {
    organicSettings = JSON.parse(JSON.stringify(DEFAULT_ORGANIC));
  }
  // se keepOrganicIfMissing=true e sem p.organic, mantém organicSettings atual
  writeOrganicToDOM();

  readValues();
}

const defaultProfile = readProfile();
const profiles = {};
PROFILE_IDS.forEach(id => { profiles[id] = readProfile(); });
let activeProfile = document.querySelector('.tab-btn.active').dataset.profile;

const loadedConfig = { name:null, snapshot:null, statusKey:'' };

function currentProfiles(){ return { ...profiles, [activeProfile]: readProfile() }; }
function buildConfig(){
  return { format: CONFIG_FORMAT, version: CONFIG_VERSION, activeProfile, profiles: currentProfiles() };
}

function normalizeProfile(src){
  if(!src) return structuredClone(defaultProfile);
  if(src.recoilControl && src.clicksPerSecond) return src;
  const legacy = src;
  return {
    recoilControl: {
      enabled: legacy.motion?.enabled ?? true,
      yDown: legacy.motion?.yDown ?? 0,
      yUp: legacy.motion?.yUp ?? 0,
      xLeft: legacy.motion?.xLeft ?? 0,
      xRight: legacy.motion?.xRight ?? 0,
      updateDelay: legacy.motion?.updateDelay ?? 0,
      trigger: legacy.motion?.trigger ?? 'secondary+primary'
    },
    clicksPerSecond: {
      enabled: legacy.pulse?.enabled ?? false,
      cps: legacy.pulse?.frequency ?? 13,
      advanced: legacy.pulse?.advanced ?? false
    },
    organic: legacy.organic || null
  };
}

/**
 * @param {object} data  JSON recebido de arquivo ou do Pico
 * @param {boolean} keepOrganicIfMissing  ver applyProfile
 */
function applyConfig(data, keepOrganicIfMissing = false){
  if(!data || data.format !== CONFIG_FORMAT || !data.profiles)
    throw new Error('Formato de config inválido');
  PROFILE_IDS.forEach(id=>{
    applyProfile(normalizeProfile(data.profiles[id]), keepOrganicIfMissing);
    profiles[id] = readProfile();
  });
  activeProfile = PROFILE_IDS.includes(data.activeProfile) ? data.activeProfile : PROFILE_IDS[0];
  selectTab(activeProfile);
  applyProfile(profiles[activeProfile], keepOrganicIfMissing);
}
function markClean(name, snapshot = JSON.stringify(currentProfiles())){
  loadedConfig.name = name;
  loadedConfig.snapshot = snapshot;
  updateConfigStatus();
}
function updateConfigStatus(){
  const name = loadedConfig.name;
  const dirty = JSON.stringify(currentProfiles()) !== loadedConfig.snapshot;
  const stateKey = !name ? (dirty?'new':'none') : (dirty?'modified':'saved');
  if(stateKey+name === loadedConfig.statusKey) return;
  loadedConfig.statusKey = stateKey+name;
  const labels = {
    none:['Nenhuma config carregada','Valores padrão'],
    new:['Config nova','Não salva'],
    modified:[name,'Alterações não salvas'],
    saved:[name,'Salva']
  };
  const el = document.getElementById('config-status');
  el.dataset.state = stateKey;
  el.title = name ? `Config carregada: ${name}` : '';
  document.getElementById('config-name').textContent = labels[stateKey][0];
  document.getElementById('config-badge').textContent = labels[stateKey][1];
  el.classList.remove('changed'); void el.offsetWidth; el.classList.add('changed');
}

// -------- Arquivo --------
function pickFile(){
  return new Promise(resolve=>{
    const inp = document.createElement('input');
    inp.type='file'; inp.accept='.json,application/json';
    inp.addEventListener('change', ()=>resolve(inp.files[0]||null));
    inp.addEventListener('cancel', ()=>resolve(null));
    inp.click();
  });
}
async function chooseFile(){
  if('showOpenFilePicker' in window){
    const [h] = await window.showOpenFilePicker({ types: FILE_TYPES });
    return h.getFile();
  }
  return pickFile();
}
async function writeFile(suggestedName, text){
  if('showSaveFilePicker' in window){
    const h = await window.showSaveFilePicker({ suggestedName, types: FILE_TYPES });
    const w = await h.createWritable(); await w.write(text); await w.close();
    return h.name;
  }
  const url = URL.createObjectURL(new Blob([text], { type:'application/json' }));
  const a = document.createElement('a'); a.href=url; a.download=suggestedName; a.click();
  setTimeout(()=>URL.revokeObjectURL(url), 1000);
  return suggestedName;
}

async function loadConfig(){
  let file;
  try { file = await chooseFile(); }
  catch(err){ if(err.name!=='AbortError') showToast('Não foi possível abrir o seletor de arquivos.','error'); return; }
  if(!file) return;
  try { applyConfig(JSON.parse(await file.text()), false); } // ← arquivo: sem organic → default
  catch(err){ Sound.error(); showToast(`${file.name} não é uma config válida.`,'error'); return; }
  markClean(file.name); Sound.success(); showToast(`Config carregada: ${file.name}`);
}
async function saveConfig(){
  const data = buildConfig(); let name;
  try { name = await writeFile(loadedConfig.name || 'config-pico.json', JSON.stringify(data, null, 2)); }
  catch(err){ if(err.name==='AbortError') return; Sound.error(); showToast('Não foi possível salvar.','error'); return; }
  markClean(name, JSON.stringify(data.profiles)); Sound.success(); showToast(`Config salva: ${name}`);
}

document.getElementById('load-btn').addEventListener('click', loadConfig);
document.getElementById('save-btn').addEventListener('click', saveConfig);
['input','change'].forEach(t => document.querySelector('.panels').addEventListener(t, updateConfigStatus));

// -------- Navegação --------
const modulesButton = document.getElementById('system-modules-btn');
const dashboardView = document.getElementById('dashboard-view');
const modulesView = document.getElementById('modules-view');
function setActiveView(v){
  const isModules = v==='modules';
  dashboardView.hidden = isModules;
  modulesView.hidden = !isModules;
  modulesButton.setAttribute('aria-pressed', String(isModules));
}
modulesButton.addEventListener('click', ()=> setActiveView(modulesView.hidden ? 'modules' : 'dashboard'));

// ============================================================
// WEB SERIAL
// ============================================================
const statusEl = document.getElementById('status');
const statusText = document.getElementById('status-text');
const connectBtn = document.getElementById('connect-btn');
const pullBtn = document.getElementById('pull-btn');
const pushBtn = document.getElementById('push-btn');
const terminal = document.getElementById('terminal');

let serialPort = null, serialWriter = null, serialReader = null, connected = false;

function setStatus(s){
  statusEl.dataset.state = s;
  statusText.textContent = s==='connected' ? 'Conectado' : s==='connecting' ? 'Conectando…' : 'Desconectado';
  connected = (s === 'connected');
  pullBtn.hidden = !connected;
  pushBtn.hidden = !connected;
  connectBtn.textContent = connected ? 'Desconectar' : 'Conectar dispositivo';
  connectBtn.classList.toggle('primary', !connected);
  connectBtn.classList.toggle('danger', connected);
}

function logLine(dir, msg){
  const div = document.createElement('div');
  div.className = dir;
  div.textContent = `${dir==='tx'?'→ Pico':'← Pico'}: ${msg}`;
  terminal.appendChild(div);
  terminal.scrollTop = terminal.scrollHeight;
  if(terminal.children.length > 60) terminal.removeChild(terminal.firstChild);
}

async function sendSerial(obj){
  if(!serialWriter) throw new Error('Sem conexão serial');
  const line = JSON.stringify(obj)+'\n';
  await serialWriter.write(new TextEncoder().encode(line));
  logLine('tx', JSON.stringify(obj));
}

function handleSerialLine(line){
  logLine('rx', line);
  let data;
  try { data = JSON.parse(line); } catch { return; }
  if(data.format === CONFIG_FORMAT){
    try {
      // ⬇ FIX BUG 1: keepOrganicIfMissing=true → mantém organic se firmware não enviar
      applyConfig(data, true);
      markClean('Pico', JSON.stringify(currentProfiles()));
      Sound.success();
      showToast('Config recebida do Pico');
    } catch(err){ showToast('Config do Pico inválida','error'); }
    return;
  }
  if(data.ok === true){
    Sound.success();
    showToast(data.msg || 'Pico: OK');
    return;
  }
  if(data.ok === false){
    Sound.error();
    showToast('Pico: '+(data.error||'erro'),'error');
  }
}

async function readLoop(){
  const decoder = new TextDecoderStream();
  serialPort.readable.pipeTo(decoder.writable).catch(()=>{});
  serialReader = decoder.readable.getReader();
  let buffer = '';
  try {
    while(true){
      const { value, done } = await serialReader.read();
      if(done) break;
      buffer += value;
      let idx;
      while((idx = buffer.indexOf('\n')) >= 0){
        const line = buffer.slice(0, idx).trim();
        buffer = buffer.slice(idx+1);
        if(line) handleSerialLine(line);
      }
    }
  } catch(err){
  } finally {
    try { serialReader.releaseLock(); } catch {}
    serialReader = null;
  }
}

async function connectSerial(){
  if(connected){ return disconnectSerial(); }
  if(!('serial' in navigator)){
    Sound.error();
    showToast('Web Serial indisponível. Use Chrome ou Edge.','error');
    return;
  }
  try {
    setStatus('connecting');
    serialPort = await navigator.serial.requestPort();
    await serialPort.open({ baudRate: 115200 });
    serialWriter = serialPort.writable.getWriter();
    setStatus('connected');
    Sound.success();
    showToast('Conectado ao Pico (115200 baud)');
    readLoop();
  } catch(err){
    setStatus('disconnected');
    if(err.name !== 'NotFoundError'){
      Sound.error();
      showToast('Falha na conexão: '+err.message,'error');
    }
  }
}

async function disconnectSerial(){
  try {
    if(serialReader){ try{ await serialReader.cancel(); }catch{} }
    if(serialWriter){ try{ await serialWriter.close(); }catch{} serialWriter = null; }
    if(serialPort){ try{ await serialPort.close(); }catch{} serialPort = null; }
  } catch {}
  setStatus('disconnected');
  Sound.click();
  showToast('Desconectado');
}

connectBtn.addEventListener('click', connectSerial);

pullBtn.addEventListener('click', async ()=>{
  try { await sendSerial({ cmd:'get' }); Sound.click(); }
  catch(err){ showToast('Falha ao enviar: '+err.message,'error'); }
});

pushBtn.addEventListener('click', async ()=>{
  const cfg = buildConfig();
  const payload = {
    cmd: 'set',
    config: {
      format: CONFIG_FORMAT,
      version: CONFIG_VERSION,
      activeProfile,
      profiles: Object.fromEntries(Object.entries(cfg.profiles).map(([k,p])=>[k,{
        recoilControl: p.recoilControl,
        clicksPerSecond: {
          enabled: p.clicksPerSecond.enabled,
          cps: p.clicksPerSecond.cps,
          advanced: p.clicksPerSecond.advanced,
          intervalMs: cpsToInterval(p.clicksPerSecond.cps)
        },
        organic: p.organic
      }]))
    }
  };
  try { await sendSerial(payload); Sound.click(); }
  catch(err){ showToast('Falha ao enviar: '+err.message,'error'); }
});

navigator.serial?.addEventListener('disconnect', ()=>{
  if(connected){ setStatus('disconnected'); showToast('Pico desconectado','error'); }
});

// -------- Toast / sons --------
let toastTimer;
function showToast(msg, type='info'){
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.className = `toast show ${type}`;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(()=>t.classList.remove('show'), 3500);
}

const soundToggle = document.getElementById('sound-toggle');
soundToggle.setAttribute('aria-pressed', Sound.enabled);
soundToggle.addEventListener('click', ()=>{
  Sound.setEnabled(!Sound.enabled);
  soundToggle.setAttribute('aria-pressed', Sound.enabled);
  Sound.on();
});

// -------- Cabeçalho: versões (fonte única) + painel de especificações --------
const appVersionEl = document.getElementById('app-version');
const fwVersionEl = document.getElementById('firmware-version');
if (appVersionEl) appVersionEl.textContent = 'v' + APP_VERSION;
if (fwVersionEl) fwVersionEl.textContent = 'v' + FIRMWARE_VERSION;

const footerAppEl = document.getElementById('footer-app-version');
const footerFwEl = document.getElementById('footer-firmware-version');
if (footerAppEl) footerAppEl.textContent = 'v' + APP_VERSION;
if (footerFwEl) footerFwEl.textContent = 'v' + FIRMWARE_VERSION;

const hwInfoBtn = document.getElementById('hw-info-btn');
const hwInfoPanel = document.getElementById('hw-info-panel');
const hwInfoClose = document.getElementById('hw-info-close');

function openHwInfo(){
  hwInfoPanel.hidden = false;
  hwInfoBtn.setAttribute('aria-expanded', 'true');
  hwInfoClose.focus();
}
function closeHwInfo(returnFocus = false){
  hwInfoPanel.hidden = true;
  hwInfoBtn.setAttribute('aria-expanded', 'false');
  if (returnFocus) hwInfoBtn.focus();
}
// Guarda defensiva: só registra listeners se o markup do painel info existir.
if (hwInfoBtn && hwInfoPanel && hwInfoClose) {
  hwInfoBtn.addEventListener('click', ()=>{
    if (hwInfoPanel.hidden) { openHwInfo(); Sound.click(); }
    else closeHwInfo();
  });
  hwInfoClose.addEventListener('click', ()=> closeHwInfo(true));
  document.addEventListener('keydown', (e)=>{
    if (e.key === 'Escape' && !hwInfoPanel.hidden) closeHwInfo(true);
  });
  document.addEventListener('click', (e)=>{
    if (!hwInfoPanel.hidden && !hwInfoPanel.contains(e.target) && !hwInfoBtn.contains(e.target)) closeHwInfo(false);
  });
}
document.querySelectorAll('.btn:not(.primary)').forEach(b=> b.addEventListener('click', Sound.click));
document.getElementById('trigger').addEventListener('change', Sound.click);
const delayInput = document.getElementById('update-delay');
delayInput.addEventListener('change', ()=>{ delayInput.value = clampToInput(delayInput, delayInput.value); Sound.click(); });

// ============================================================
// Canvas / preview
// ============================================================
const rootStyle = getComputedStyle(document.documentElement);
const colors = {
  ridge: rootStyle.getPropertyValue('--ridge').trim(),
  copper: rootStyle.getPropertyValue('--copper').trim(),
  faint: rootStyle.getPropertyValue('--text-faint').trim(),
  line: rootStyle.getPropertyValue('--line-strong').trim(),
  grid: '#13120e'
};
const vector = { canvas: document.getElementById('vector-canvas'), x:0, y:0 };
const scope  = { canvas: document.getElementById('cps-canvas'), offset:0 };

function setupCanvas(t){
  const dpr = devicePixelRatio || 1, r = t.canvas.getBoundingClientRect();
  t.canvas.width = Math.round(r.width*dpr);
  t.canvas.height = Math.round(r.height*dpr);
  t.ctx = t.canvas.getContext('2d');
  t.ctx.setTransform(dpr,0,0,dpr,0,0);
  t.w = r.width; t.h = r.height;
}
function dot(ctx,x,y,r){ ctx.beginPath(); ctx.arc(x,y,r,0,Math.PI*2); ctx.fill(); }

function drawVector(time){
  const { ctx, w, h } = vector;
  const cx=w/2, cy=h/2, radius=Math.min(w,h)/2-8, scale=radius/200;
  const on = recoilPanel.classList.contains('is-on');
  const color = on ? colors.ridge : colors.faint;
  vector.x += (state.dx*scale - vector.x)*.15;
  vector.y += (state.dy*scale - vector.y)*.15;
  ctx.clearRect(0,0,w,h);
  ctx.strokeStyle = colors.line; ctx.lineWidth = 1;
  ctx.beginPath(); ctx.arc(cx,cy,radius,0,Math.PI*2); ctx.stroke();
  ctx.beginPath(); ctx.arc(cx,cy,radius/2,0,Math.PI*2); ctx.stroke();
  ctx.strokeStyle = colors.grid;
  ctx.beginPath();
  ctx.moveTo(cx-radius,cy); ctx.lineTo(cx+radius,cy);
  ctx.moveTo(cx,cy-radius); ctx.lineTo(cx,cy+radius);
  ctx.stroke();
  const ex = cx+vector.x, ey = cy+vector.y;
  ctx.strokeStyle = color; ctx.fillStyle = color; ctx.lineWidth = 2; ctx.lineCap = 'round';
  ctx.beginPath(); ctx.moveTo(cx,cy); ctx.lineTo(ex,ey); ctx.stroke();
  dot(ctx,ex,ey,4);
  if(on && !reduceMotion && Math.hypot(vector.x,vector.y)>4){
    for(let i=0;i<3;i++){
      const t = (time/1400 + i/3)%1;
      ctx.globalAlpha = Math.sin(t*Math.PI);
      dot(ctx, cx+vector.x*t, cy+vector.y*t, 2);
    }
    ctx.globalAlpha = 1;
  }
  ctx.fillStyle = colors.faint; dot(ctx,cx,cy,2.5);
}

function drawScope(delta){
  const { ctx, w, h } = scope;
  const on = cpsPanel.classList.contains('is-on');
  const color = !on ? colors.faint : state.cps > 15 ? colors.copper : colors.ridge;
  const high = Math.round(h*0.3), low = Math.round(h*0.7);
  ctx.clearRect(0,0,w,h);
  ctx.strokeStyle = colors.grid; ctx.lineWidth = 1;
  ctx.beginPath();
  for(let i=1;i<10;i++){ const x = Math.round((w/10)*i)+.5; ctx.moveTo(x,0); ctx.lineTo(x,h); }
  ctx.moveTo(0,Math.round(h/2)+.5); ctx.lineTo(w,Math.round(h/2)+.5);
  ctx.stroke();
  ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.lineJoin = 'round';
  ctx.beginPath();
  if(on){
    const cps = Math.max(1, state.cps);
    const period = w / cps;
    const pulse = period * .4;
    if(!reduceMotion) scope.offset = (scope.offset + (delta/1000)*w) % period;
    ctx.moveTo(-period, low);
    for(let x = -scope.offset; x < w; x += period){
      ctx.lineTo(x, low); ctx.lineTo(x, high);
      ctx.lineTo(x+pulse, high); ctx.lineTo(x+pulse, low);
    }
    ctx.lineTo(w, low);
  } else {
    ctx.moveTo(0, low); ctx.lineTo(w, low);
  }
  ctx.stroke();
}

let lastFrame = performance.now();
function frame(time){
  const delta = Math.min(100, time - lastFrame);
  lastFrame = time;
  drawVector(time);
  drawScope(delta);
  requestAnimationFrame(frame);
}

function layout(){
  setupCanvas(vector);
  setupCanvas(scope);
  moveIndicator();
}
readValues();
layout();
markClean(null);
requestAnimationFrame(()=>indicator.classList.add('ready'));
requestAnimationFrame(frame);
addEventListener('resize', layout);
if(document.fonts) document.fonts.ready.then(moveIndicator);
