// app.js — UI logic for Drift. Talks to the host exclusively
// through the bridge functions defined in iplug.js (SPVFUI/BPCFUI/EPCFUI).
//
// Param order here MUST match EParams in Drift.h exactly -- indices
// are positional, not name-based, on the C++ side.

const PARAMS = [
  /* 0  */ { name: 'kParamPulses',          type: 'int',     min: 1,  max: 32,  def: 5 },
  /* 1  */ { name: 'kParamSequenceMode',    type: 'enum',    options: ['Euclidean', 'Density', 'Chaos'], def: 0 },
  /* 2  */ { name: 'kParamSequenceAmount',  type: 'percent', def: 50 },
  /* 3  */ { name: 'kParamProbability',     type: 'percent', def: 100 },
  /* 4  */ { name: 'kParamSwing',           type: 'percent', def: 0 },
  /* 5  */ { name: 'kParamClockAlign',      type: 'enum',    options: ['Hard', 'Soft'], def: 0 },
  /* 6  */ { name: 'kParamClockSoftAmount', type: 'percent', def: 25 },
  /* 7  */ { name: 'kParamKey',             type: 'enum',    options: ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'], def: 0 },
  /* 8  */ { name: 'kParamScaleMode',       type: 'enum',    options: ['Ionian','Dorian','Phrygian','Lydian','Mixolydian','Aeolian','Locrian','Harmonic Minor','Melodic Minor','Byzantine','Persian','Neapolitan Minor','Neapolitan Major','Hungarian Minor'], def: 0 },
  /* 9  */ { name: 'kParamChordType',       type: 'enum',    options: ['Single','Power','Octave','Major','Minor','Dim','Aug','Sus2','Sus4','Maj7','Min7','Dom7','Min9','Maj9','Dom9','Min11','Maj6','Min6','HalfDim7','Dim7'], def: 12 },
  /* 10 */ { name: 'kParamChordVoices',     type: 'int',     min: 1, max: 6, def: 4 },
  /* 11 */ { name: 'kParamHarmonyDrift',    type: 'percent', def: 20 },
  /* 12 */ { name: 'kParamSteps',           type: 'int',     min: 1, max: 32, def: 16 },
  /* 13 */ { name: 'kParamRotation',        type: 'int',     min: 0, max: 31, def: 0 },
  /* 14 */ { name: 'kParamRootNote',        type: 'pitch',   min: 0, max: 128, def: 60 },
  /* 15 */ { name: 'kParamVoicing',         type: 'enum',    options: ['Close','Drop2','Drop3','Spread','Wide'], def: 0 },
  /* 16 */ { name: 'kParamNoteLength',      type: 'int',     min: 1, max: 256, def: 32 },
  /* 17 */ { name: 'kParamGate',            type: 'percent', def: 85 },
  /* 18 */ { name: 'kParamVelocity',        type: 'percent', def: 60 },
  /* 19 */ { name: 'kParamMidiChannel',     type: 'int',     min: 1, max: 16, def: 1 },
  /* 20 */ { name: 'kParamGlobalTranspose', type: 'int',     min: -24, max: 24, def: 0 },
  /* 21 */ { name: 'kParamMod1Source',      type: 'enum',    options: ['Off','Velocity','Gate','Probability','Dens/Chaos','Drift','Random'], def: 0 },
  /* 22 */ { name: 'kParamMod1CC',          type: 'int',     min: 0, max: 127, def: 1 },
  /* 23 */ { name: 'kParamMod2Source',      type: 'enum',    options: ['Off','Velocity','Gate','Probability','Dens/Chaos','Drift','Random'], def: 0 },
  /* 24 */ { name: 'kParamMod2CC',          type: 'int',     min: 0, max: 127, def: 74 },
  /* 25 */ { name: 'kParamExportBars',      type: 'int',     min: 1, max: 16, def: 4 },
  /* 26 */ { name: 'kParamMonoMode',        type: 'bool',    def: 0 },
  /* 27 */ { name: 'kParamMaxVoices',       type: 'int',     min: 1, max: 16, def: 8 },
  /* 28 */ { name: 'kParamMod3Source',      type: 'enum',    options: ['Off','Velocity','Gate','Probability','Dens/Chaos','Drift','Random'], def: 0 },
  /* 29 */ { name: 'kParamMod3CC',          type: 'int',     min: 0, max: 127, def: 11 },
  /* 30 */ { name: 'kParamMod4Source',      type: 'enum',    options: ['Off','Velocity','Gate','Probability','Dens/Chaos','Drift','Random'], def: 0 },
  /* 31 */ { name: 'kParamMod4CC',          type: 'int',     min: 0, max: 127, def: 7 },
  /* 32 */ { name: 'kParamMod5Source',      type: 'enum',    options: ['Off','Velocity','Gate','Probability','Dens/Chaos','Drift','Random'], def: 0 },
  /* 33 */ { name: 'kParamMod5CC',          type: 'int',     min: 0, max: 127, def: 10 },
  /* 34 */ { name: 'kParamMod6Source',      type: 'enum',    options: ['Off','Velocity','Gate','Probability','Dens/Chaos','Drift','Random'], def: 0 },
  /* 35 */ { name: 'kParamMod6CC',          type: 'int',     min: 0, max: 127, def: 71 },
  /* 36 */ { name: 'kParamMod7Source',      type: 'enum',    options: ['Off','Velocity','Gate','Probability','Dens/Chaos','Drift','Random'], def: 0 },
  /* 37 */ { name: 'kParamMod7CC',          type: 'int',     min: 0, max: 127, def: 91 },
  /* 38 */ { name: 'kParamMod8Source',      type: 'enum',    options: ['Off','Velocity','Gate','Probability','Dens/Chaos','Drift','Random'], def: 0 },
  /* 39 */ { name: 'kParamMod8CC',          type: 'int',     min: 0, max: 127, def: 93 },
  /* 40 */ { name: 'kParamSendClock',       type: 'bool',    def: 0 },
  /* 41 */ { name: 'kParamUIViewMode',      type: 'bool',    def: 0 }, // 0 = Full, 1 = Compact -- hidden/meta, UI layout state only
  /* 42 */ { name: 'kParamPatternInvert',   type: 'bool',    def: 0 },
  /* 43 */ { name: 'kParamTrigEvery',       type: 'int',     min: 1, max: 8, def: 1 },
  /* 44 */ { name: 'kParamTrigOffset',      type: 'int',     min: 0, max: 7, def: 0 },
  /* 45 */ { name: 'kParamRotationDriftPeriod', type: 'int', min: 0, max: 128, def: 0 },
  /* 46 */ { name: 'kParamRatchetCount',    type: 'int',     min: 1, max: 8, def: 1 },
  /* 47 */ { name: 'kParamChordPriority',   type: 'bool',    def: 0 },
  /* 48 */ { name: 'kParamDriftGravity',    type: 'percent', def: 0 },
  /* 49 */ { name: 'kParamAccentEvery',     type: 'int',     min: 1, max: 8, def: 4 },
  /* 50 */ { name: 'kParamAccentAmount',    type: 'percent', def: 0 },
  /* 51 */ { name: 'kParamFreeze',          type: 'bool',    def: 0 },
  /* 52 */ { name: 'kParamExportVariations',type: 'int',     min: 1, max: 8, def: 1 },
  /* 53 */ { name: 'kParamArpMode',         type: 'enum',    options: ['Off','Up','Down','Up-Down'], def: 0 },
  /* 54 */ { name: 'kParamDeterministicExport', type: 'bool', def: 0 },
];

const NAME_TO_IDX = {};
PARAMS.forEach((p, i) => { NAME_TO_IDX[p.name] = i; });

// Current REAL (non-normalized) value per param, indexed positionally.
const state = PARAMS.map(p => p.def);

function paramIdx(name) { return NAME_TO_IDX[name]; }

function realToNorm(p, real) {
  switch (p.type) {
    case 'percent': return clamp01(real / 100);
    case 'bool': return real ? 1 : 0;
    case 'enum': return clamp01(real / (p.options.length - 1));
    default: return clamp01((real - p.min) / (p.max - p.min));
  }
}

function normToReal(p, norm) {
  norm = clamp01(norm);
  switch (p.type) {
    case 'percent': return Math.round(norm * 100);
    case 'bool': return norm >= 0.5 ? 1 : 0;
    case 'enum': return Math.round(norm * (p.options.length - 1));
    default: return Math.round(p.min + norm * (p.max - p.min));
  }
}

function clamp01(v) { return Math.max(0, Math.min(1, v)); }

function formatValue(p, real) {
  switch (p.type) {
    case 'percent': return real + '%';
    case 'bool': return real ? 'On' : 'Off';
    case 'enum': return p.options[real];
    case 'pitch': return midiNoteName(real);
    default: return String(real);
  }
}

function midiNoteName(n) {
  const names = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
  const octave = Math.floor(n / 12) - 1;
  return names[((n % 12) + 12) % 12] + octave;
}

// ---- Setting a param value (always the single source of truth) ----

function setParam(idx, real, { fromHost = false, sendToHost = true } = {}) {
  const p = PARAMS[idx];
  real = Math.max(p.min ?? 0, Math.min(p.max ?? (p.type === 'enum' ? p.options.length - 1 : (p.type === 'percent' ? 100 : 1)), real));
  state[idx] = real;

  if (sendToHost && !fromHost && window.IPLUG_HAS_HOST) {
    SPVFUI(idx, realToNorm(p, real));
  }

  refreshControlsFor(idx);
  onParamStateChanged(idx);
}

// A single control gesture (drag/click) should be wrapped in Begin/End so
// the host knows automation is happening, matching the three-step protocol
// the webview-ui skill documents.
function gestureBegin(idx) { if (window.IPLUG_HAS_HOST) BPCFUI(idx); }
function gestureEnd(idx) { if (window.IPLUG_HAS_HOST) EPCFUI(idx); }

// ==========================================================================
// Generic control widgets -- bound declaratively via data-param attributes
// ==========================================================================

function initKnobs(root) {
  root.querySelectorAll('.knob[data-param]').forEach(el => {
    const idx = paramIdx(el.dataset.param);
    if (idx === undefined) return;
    wrapKnobWithLabel(el, idx);
    bindKnobDrag(el, idx);
  });
}

function wrapKnobWithLabel(knobEl, idx) {
  if (knobEl.dataset.wrapped || 'noLabel' in knobEl.dataset) return;
  knobEl.dataset.wrapped = '1';
  const label = knobEl.dataset.label || PARAMS[idx].name;
  const cell = document.createElement('div');
  cell.className = 'knob-cell';
  knobEl.parentNode.insertBefore(cell, knobEl);
  cell.appendChild(knobEl);
  const labelP = document.createElement('p');
  labelP.className = 'knob-cell-label';
  labelP.textContent = label;
  const valueP = document.createElement('p');
  valueP.className = 'knob-cell-value mono';
  valueP.dataset.knobValueFor = idx;
  cell.appendChild(labelP);
  cell.appendChild(valueP);
}

// Shared drag-gesture chrome for both knobs and sliders: listener wiring
// (pointer + touch + blur-cancel), double-click-to-default, Alt+wheel
// nudging, and keyboard arrows. The two controls only differ in how a
// pointer position/delta turns into a norm -- that's the one thing callers
// supply (`computeNorm`) plus an optional per-control hook that runs once at
// gesture start (`onGestureStart`; the knob uses it to capture a drag
// baseline, the slider uses it to jump straight to the click position).
function bindPointerGesture(el, idx, computeNorm, { onGestureStart, onLiveChange, defaultNorm } = {}) {
  let dragging = false;

  const currentNorm = () => {
    if (onLiveChange && idx === -1) {
      return (parseFloat(el.style.getPropertyValue('--pct')) || 0) / 100;
    }
    return realToNorm(PARAMS[idx] || { type: 'percent' }, state[idx]);
  };

  const applyNorm = norm => {
    if (onLiveChange) onLiveChange(norm);
    else setParam(idx, normToReal(PARAMS[idx], norm));
  };

  const onMove = e => {
    if (!dragging) return;
    applyNorm(computeNorm(e));
  };

  const onUp = () => {
    if (!dragging) return;
    dragging = false;
    window.removeEventListener('mousemove', onMove);
    window.removeEventListener('mouseup', onUp);
    window.removeEventListener('touchmove', onMove);
    window.removeEventListener('touchend', onUp);
    window.removeEventListener('blur', onUp);
    gestureEnd(idx);
  };

  const onDown = e => {
    dragging = true;
    gestureBegin(idx);
    onGestureStart?.(e, applyNorm, currentNorm);
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);
    window.addEventListener('touchmove', onMove, { passive: false });
    window.addEventListener('touchend', onUp);
    window.addEventListener('blur', onUp);
    if (e.cancelable) e.preventDefault();
  };

  const onDblClick = () => {
    gestureBegin(idx);
    if (onLiveChange) {
      if (defaultNorm !== undefined) onLiveChange(defaultNorm);
    } else {
      setParam(idx, PARAMS[idx].def);
    }
    gestureEnd(idx);
  };

  // Wheel-to-adjust, gated behind Option/Alt: the Full view's card stack
  // scrolls vertically on its own, so an unmodified wheel/trackpad swipe
  // over a knob has to keep scrolling the page, not silently eat the
  // gesture and nudge a param instead.
  const onWheel = e => {
    if (!e.altKey) return;
    e.preventDefault();
    const step = e.shiftKey ? 0.05 : 0.01;
    const dir = e.deltaY < 0 ? 1 : -1;
    const newNorm = clamp01(currentNorm() + dir * step);
    gestureBegin(idx);
    applyNorm(newNorm);
    gestureEnd(idx);
  };

  el.addEventListener('mousedown', onDown);
  el.addEventListener('touchstart', onDown, { passive: false });
  el.addEventListener('dblclick', onDblClick);
  el.addEventListener('wheel', onWheel, { passive: false });
  bindKeyboard(el, idx, onLiveChange);

  return { currentNorm };
}

function bindKnobDrag(el, idx, onLiveChange, defaultNorm) {
  const DRAG_RANGE = 150;
  let startY = 0, startNorm = 0, currentNorm;

  const computeNorm = e => {
    const clientY = e.touches ? e.touches[0].clientY : e.clientY;
    // Shift = fine-tune: same physical drag distance covers a much smaller
    // value range, useful for dialing in sensitive meta-controls like
    // Chaos/Density. No effect on touch (TouchEvent has no shiftKey).
    const range = e.shiftKey ? DRAG_RANGE * 5 : DRAG_RANGE;
    const delta = (startY - clientY) / range;
    return clamp01(startNorm + delta);
  };

  const onGestureStart = e => {
    startY = e.touches ? e.touches[0].clientY : e.clientY;
    startNorm = currentNorm();
  };

  ({ currentNorm } = bindPointerGesture(el, idx, computeNorm, { onGestureStart, onLiveChange, defaultNorm }));
}

function initSliders(root) {
  root.querySelectorAll('.slider[data-param]').forEach(el => {
    const idx = paramIdx(el.dataset.param);
    if (idx === undefined) return;
    bindHorizontalSlider(el, idx);
  });
}

function bindHorizontalSlider(el, idx, isVertical = false) {
  const computeNorm = e => {
    const rect = el.getBoundingClientRect();
    const clientX = e.touches ? e.touches[0].clientX : e.clientX;
    const clientY = e.touches ? e.touches[0].clientY : e.clientY;
    if (isVertical) return clamp01(1 - (clientY - rect.top) / rect.height);
    return clamp01((clientX - rect.left) / rect.width);
  };

  // Unlike the knob's relative drag, a slider jumps straight to wherever you
  // clicked -- so the gesture-start hook applies a value immediately instead
  // of just recording a baseline.
  const onGestureStart = (e, applyNorm) => applyNorm(computeNorm(e));

  bindPointerGesture(el, idx, computeNorm, { onGestureStart });
}

function bindKeyboard(el, idx, onLiveChange) {
  el.tabIndex = 0;
  el.addEventListener('keydown', e => {
    const keys = ['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'];
    if (!keys.includes(e.key)) return;
    e.preventDefault();
    let norm;
    if (onLiveChange && idx === -1) {
      norm = (parseFloat(el.style.getPropertyValue('--pct')) || 0) / 100;
    } else {
      norm = realToNorm(PARAMS[idx] || { type: 'percent' }, state[idx]);
    }
    const step = e.shiftKey ? 0.05 : 0.01;
    const dir = (e.key === 'ArrowUp' || e.key === 'ArrowRight') ? 1 : -1;
    const newNorm = clamp01(norm + dir * step);
    gestureBegin(idx);
    if (onLiveChange) onLiveChange(newNorm);
    else setParam(idx, normToReal(PARAMS[idx], newNorm));
    gestureEnd(idx);
  });
}

function initSwitches(root) {
  root.querySelectorAll('.switch[data-param]').forEach(el => {
    const idx = paramIdx(el.dataset.param);
    if (idx === undefined) return;
    el.tabIndex = 0;
    el.addEventListener('click', () => {
      gestureBegin(idx);
      setParam(idx, state[idx] ? 0 : 1);
      gestureEnd(idx);
    });
    el.addEventListener('keydown', e => {
      if (e.key === ' ' || e.key === 'Enter') {
        e.preventDefault();
        el.click();
      }
    });
  });
}

function initDropdowns(root) {
  root.querySelectorAll('select.dropdown[data-param]').forEach(el => {
    const idx = paramIdx(el.dataset.param);
    if (idx === undefined) return;
    const p = PARAMS[idx];

    el.innerHTML = '';
    if (p.type === 'enum') {
      p.options.forEach((opt, i) => {
        const o = document.createElement('option');
        o.value = i;
        o.textContent = opt;
        el.appendChild(o);
      });
    } else if (p.type === 'pitch') {
      for (let n = p.min; n <= p.max; n++) {
        const o = document.createElement('option');
        o.value = n;
        o.textContent = midiNoteName(n);
        el.appendChild(o);
      }
    }

    el.addEventListener('change', () => {
      gestureBegin(idx);
      setParam(idx, parseInt(el.value, 10));
      gestureEnd(idx);
    });
  });
}

function initCCFields(root) {
  root.querySelectorAll('input.cc-field[data-param]').forEach(el => {
    const idx = paramIdx(el.dataset.param);
    if (idx === undefined) return;
    el.addEventListener('change', () => {
      let v = parseInt(el.value, 10);
      if (isNaN(v)) v = PARAMS[idx].def;
      gestureBegin(idx);
      setParam(idx, v);
      gestureEnd(idx);
    });
    el.addEventListener('keydown', e => {
      if (e.key === 'Enter') el.blur();
    });
  });
}

function initPillSelectors(root) {
  root.querySelectorAll('.pill-selector[data-param]').forEach(el => {
    const idx = paramIdx(el.dataset.param);
    if (idx === undefined) return;
    const options = el.dataset.options.split(',');
    el.tabIndex = 0;
    el.innerHTML = '';
    options.forEach((label, i) => {
      const pill = document.createElement('div');
      pill.className = 'pill-option';
      pill.textContent = label;
      pill.addEventListener('click', () => {
        gestureBegin(idx);
        setParam(idx, i);
        gestureEnd(idx);
      });
      el.appendChild(pill);
    });
    el.addEventListener('keydown', e => {
      if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
        e.preventDefault();
        const dir = e.key === 'ArrowRight' ? 1 : -1;
        const next = (state[idx] + dir + options.length) % options.length;
        gestureBegin(idx);
        setParam(idx, next);
        gestureEnd(idx);
      }
    });
  });
}

// ---- Push current state into every bound control (used on load + after any param change) ----
//
// refreshControlsFor runs on every param change, including once per
// mousemove tick while a knob/slider is being dragged -- so it's a hot path.
// controlCache holds the actual element references (built once, in
// buildControlCache() below) instead of re-running querySelectorAll's
// attribute-selector match against the whole document on every tick.

const controlCache = PARAMS.map(() => ({ bound: [], knobValueEls: [], readoutEls: [] }));

function buildControlCache() {
  PARAMS.forEach((p, idx) => {
    controlCache[idx].bound = Array.from(document.querySelectorAll(`[data-param="${p.name}"]`));
    controlCache[idx].knobValueEls = Array.from(document.querySelectorAll(`[data-knob-value-for="${idx}"]`));
    controlCache[idx].readoutEls = Array.from(document.querySelectorAll(`[data-readout="${p.name}"]`));
  });
}

function refreshControlsFor(idx) {
  const p = PARAMS[idx];
  const real = state[idx];
  const norm = realToNorm(p, real);
  const pct = Math.round(norm * 100);
  const cache = controlCache[idx];

  cache.bound.forEach(el => {
    if (el.classList.contains('knob') || el.classList.contains('slider')) {
      el.style.setProperty('--pct', pct);
      if (el.classList.contains('knob')) updateKnobIndicator(el, pct);
    } else if (el.classList.contains('switch')) {
      el.classList.toggle('on', real >= 1);
      const onLabel = el.dataset.onLabel, offLabel = el.dataset.offLabel;
    } else if (el.tagName === 'SELECT') {
      el.value = real;
    } else if (el.classList.contains('cc-field')) {
      el.value = real;
    } else if (el.classList.contains('pill-selector')) {
      Array.from(el.children).forEach((pill, i) => pill.classList.toggle('active', i === real));
    }
  });

  cache.knobValueEls.forEach(elm => { elm.textContent = formatValue(p, real); });
  cache.readoutEls.forEach(elm => { elm.textContent = formatValue(p, real); });
}

function refreshAllControls() {
  PARAMS.forEach((_, idx) => refreshControlsFor(idx));
}

// ==========================================================================
// Reactions to param changes (visualizer, soft-amount visibility, footer, ...)
// ==========================================================================

function onParamStateChanged(idx) {
  const name = PARAMS[idx].name;

  if (['kParamSteps', 'kParamPulses', 'kParamRotation', 'kParamSequenceMode'].includes(name)) {
    // Sync Density slider in Compact View
    const steps = state[paramIdx('kParamSteps')] || 1;
    const pulses = state[paramIdx('kParamPulses')] || 0;
    const density = Math.round((pulses / steps) * 100);
    const dEl = document.getElementById('compact-density-slider');
    const dVal = document.getElementById('compact-density-value');
    if (dEl) dEl.style.setProperty('--pct', density);
    if (dVal) dVal.textContent = density + '%';
  }
  if (name === 'kParamClockAlign') {
    document.getElementById('soft-amount-row').hidden = state[paramIdx('kParamClockAlign')] === 0;
  }
  if (['kParamKey', 'kParamScaleMode', 'kParamChordType', 'kParamChordVoices', 'kParamMonoMode'].includes(name)) {
    updateSummaryReadouts();
  }
  if (name === 'kParamExportBars') {
    document.getElementById('bars-readout').textContent = state[paramIdx('kParamExportBars')] + ' BARS';
  }
  if (name === 'kParamExportVariations') {
    document.getElementById('variations-readout').textContent = state[paramIdx('kParamExportVariations')];
  }
  if (name === 'kParamMidiChannel') {
    document.getElementById('compact-midi-badge').textContent = 'CH ' + state[paramIdx('kParamMidiChannel')];
  }
  if (name === 'kParamUIViewMode') {
    showView(state[idx] ? 'compact' : 'full');
  }
}

function updateSummaryReadouts() {
  const keyNames = PARAMS[paramIdx('kParamKey')].options;
  const scaleNames = PARAMS[paramIdx('kParamScaleMode')].options;
  const key = keyNames[state[paramIdx('kParamKey')]];
  const scale = scaleNames[state[paramIdx('kParamScaleMode')]];
  const mono = state[paramIdx('kParamMonoMode')];
  const voices = state[paramIdx('kParamChordVoices')];
  const texture = mono ? 'Mono' : (voices <= 1 ? 'Single' : voices === 2 ? 'Dyad' : voices === 3 ? 'Triad' : 'Chord');
  const text = `${key} ${scale} · ${texture}`;
  const el1 = document.getElementById('footer-summary');
  const el2 = document.getElementById('compact-summary');
  if (el1) el1.textContent = text;
  if (el2) el2.textContent = text;
}

// ==========================================================================
// Tempo-relative motion. Full's breathing loop is 8 "half-tempo beats" and
// Compact's is 12 -- i.e. the whole UI breathes at half the host's BPM, not
// literally on the beat. Defaults to 120bpm (matching Figma's original
// 8s/12s exactly) until real host tempo is wired up in the C++ bridge; call
// this again with the live tempo once that lands.
// ==========================================================================

let masterBpm = 120;
let fullCycleSeconds = 8;     // kept in sync with the --full-cycle CSS var
let compactCycleSeconds = 12; // kept in sync with the --compact-cycle CSS var

function updateMasterTempo(bpm) {
  masterBpm = Math.max(20, bpm || 120);
  const halfBpm = masterBpm / 2;
  const secondsPerHalfBeat = 60 / halfBpm;
  fullCycleSeconds = secondsPerHalfBeat * 8;
  compactCycleSeconds = secondsPerHalfBeat * 12;
  document.documentElement.style.setProperty('--full-cycle', fullCycleSeconds.toFixed(3) + 's');
  document.documentElement.style.setProperty('--compact-cycle', compactCycleSeconds.toFixed(3) + 's');
}

// ==========================================================================
// Compact-mode meta-controls (Chaos knob + Density slider) -- same mapping
// as the native IGraphics build: Chaos jointly randomizes Rotation +
// Probability, Density sets Pulses as a pulses:steps ratio.
// ==========================================================================

// Positions the pointer dot around any hero knob (Compact's Chaos, Full's
// Drift -- both share the same .knob-canvas markup now) to track its live
// value. Matches conic-gradient()'s own convention (0% at 12 o'clock,
// sweeping clockwise), so the dot always sits exactly at the end of the
// colored arc. Looked up via closest('.knob-canvas') rather than a fixed
// ID, so it works for any knob wrapped in that markup, not just Chaos.
function updateKnobIndicator(knobEl, pct) {
  const canvas = knobEl.closest('.knob-canvas');
  if (!canvas) return;
  const dot = canvas.querySelector('.indicator-dot');
  if (!dot) return;
  const angle = (pct / 100) * 2 * Math.PI - Math.PI / 2;
  const radius = 50; // knob-xl is 100px across, canvas is 140px
  const cx = 70, cy = 70;
  dot.style.left = (cx + radius * Math.cos(angle)) + 'px';
  dot.style.top = (cy + radius * Math.sin(angle)) + 'px';
}

function initCompactControls() {
  const chaosKnob = document.getElementById('compact-chaos-knob');
  const densitySlider = document.getElementById('compact-density-slider');

  let chaosNorm = 0.2;
  bindKnobDrag(chaosKnob, -1, norm => {
    chaosNorm = norm;
    chaosKnob.style.setProperty('--pct', Math.round(norm * 100));
    updateKnobIndicator(chaosKnob, norm * 100);
    document.getElementById('compact-chaos-value').textContent = Math.round(norm * 100) + '%';

    const stepsIdx = paramIdx('kParamSteps');
    const maxSteps = Math.max(1, state[stepsIdx]);
    const spread = Math.round(norm * (maxSteps - 1));
    const rotation = spread > 0 ? Math.round((Math.random() * (2 * spread + 1)) - spread) : 0;
    const rotIdx = paramIdx('kParamRotation');
    setParam(rotIdx, ((rotation % maxSteps) + maxSteps) % maxSteps);

    const probIdx = paramIdx('kParamProbability');
    setParam(probIdx, Math.round(100 * (1 - norm * 0.7)));
  }, 0.2);

  let densityNorm = 0.31;
  bindKnobDrag(densitySlider, -1, norm => {
    densityNorm = norm;
    densitySlider.style.setProperty('--pct', Math.round(norm * 100));
    document.getElementById('compact-density-value').textContent = Math.round(norm * 100) + '%';

    const stepsIdx = paramIdx('kParamSteps');
    const maxSteps = Math.max(1, state[stepsIdx]);
    const pulses = Math.max(1, Math.min(maxSteps, Math.round(norm * maxSteps)));
    setParam(paramIdx('kParamPulses'), pulses);
  }, 0.31);

  chaosKnob.style.setProperty('--pct', 20);
  updateKnobIndicator(chaosKnob, 20);
  densitySlider.style.setProperty('--pct', 31);
}

// ==========================================================================
// View switching
// ==========================================================================

function showView(name) {
  // Set display directly rather than the `hidden` attribute -- the ID-scoped
  // `display: flex` rules in style.css outrank the UA [hidden] stylesheet
  // rule in specificity, so `.hidden = true` alone silently does nothing.
  const full = document.getElementById('view-full');
  const compact = document.getElementById('view-compact');
  full.style.display = name === 'full' ? 'flex' : 'none';
  compact.style.display = name === 'compact' ? 'flex' : 'none';
}

// ==========================================================================
// Drag-to-DAW export. IGraphics::InitiateExternalFileDragDrop() isn't
// available here (native-only API, this is a WebView-only NO_IGRAPHICS
// build) -- but WebKit (what WKWebView on macOS is) separately supports
// dragging a file OUT to the Finder/another app via the 'DownloadURL' drag
// data flavor: set dataTransfer.setData('DownloadURL', 'mime:name:fileUrl')
// on dragstart and the OS treats the drag as if it originated from a real
// file. It's the same mechanism sites like Gmail use for "drag this
// attachment to your desktop." Unverified against a real DAW from this
// environment -- the Finder-reveal fallback (Drift.cpp's OnMessage)
// still fires unconditionally, so nothing regresses if this doesn't pan out.
// ==========================================================================

const exportBtns = ['drag-export-btn-full', 'drag-export-btn-compact'].map(id => document.getElementById(id));
let exportFeedbackTimer = null;
let lastExportedFilePath = null; // absolute path from the last successful export, for the DownloadURL drag below

function setExportButtonState(state) {
  exportBtns.forEach(btn => {
    if (!btn) return;
    btn.classList.remove('export-ready', 'export-failed');
    if (state === 'ready') btn.classList.add('export-ready');
    else if (state === 'failed') btn.classList.add('export-failed');
  });
}

function requestExport() {
  SAMFUI(100 /* msgTag: export request, arbitrary constant agreed with C++ */);
  if (!window.IPLUG_HAS_HOST) {
    console.log('[preview] Drag-export clicked -- no host to render a file.');
  }
}

// dragstart handler for the export buttons -- only does anything once a
// file actually exists (i.e. after at least one successful export; see
// lastExportedFilePath). Before that, dragging the button does nothing
// beyond the browser's default (no-op) drag, which is fine: the button's
// own click handler (requestExport) is still the way to render the first take.
function onExportDragStart(e) {
  if (!lastExportedFilePath) return;
  const fileName = lastExportedFilePath.split('/').pop();
  const fileUrl = 'file://' + lastExportedFilePath;
  e.dataTransfer.setData('DownloadURL', `audio/midi:${fileName}:${fileUrl}`);
  e.dataTransfer.effectAllowed = 'copy';
}

// Reseeds the generative wander (RNG, Chaos state, harmony position) to a
// fresh starting point -- see Drift::OnMessage. Fire-and-forget, no
// success/fail round trip needed the way export has one.
function requestReroll() {
  SAMFUI(104 /* msgTag: reroll request, arbitrary constant agreed with C++ */);
  if (!window.IPLUG_HAS_HOST) {
    console.log('[preview] Reroll clicked -- no host to reseed.');
  }
}

// Called from OnMessage() below once the C++ side confirms the render
// actually finished (SendArbitraryMsgFromDelegate(101/102), Drift.cpp
// OnMessage) -- the click alone doesn't mean the file exists yet. `filePath`
// is only present on success (101); see onExportDragStart above.
function onExportResult(success, filePath) {
  clearTimeout(exportFeedbackTimer);
  setExportButtonState(success ? 'ready' : 'failed');
  exportFeedbackTimer = setTimeout(() => setExportButtonState(null), 1600);
  if (success && filePath) lastExportedFilePath = filePath;
}

// ==========================================================================
// Boot
// ==========================================================================

// Small random phase offset per element so reloading the plugin, or having
// multiple instances open, doesn't look like every ring/glow is breathing in
// perfect unison -- purely cosmetic, doesn't touch any bound param.
function randomizeAmbientPhase() {
  const selectors = [
    '.ambient-ring-outer', '.ambient-ring-mid', '.ambient-ring-inner', '.knob-canvas .knob-xl',
  ];
  document.querySelectorAll(selectors.join(',')).forEach(el => {
    const existing = getComputedStyle(el).animationName;
    if (existing === 'none') return;
    el.style.animationDelay = (-Math.random() * 3).toFixed(2) + 's';
  });
}

// Slow, randomly-applied glow on a trio of rings: every 6-12s, reshuffle
// which of the 3 glow, weighted toward exactly one, occasionally none, and
// -- per spec -- never all three at once (cap of 2). The actual fade
// in/out is handled by the CSS `transition` on .ambient-ring-*; this just
// toggles the .ring-glow class on a slow, irregular schedule. Every
// .knob-canvas's ring trio runs its own independent timer.
function setupRingGlow(elements, minMs, maxMs) {
  const rings = elements.filter(Boolean);
  if (rings.length === 0) return;

  // Rings involved in the most recent non-empty pulse -- only updated when
  // something actually glows, so a quiet (count===0) round doesn't erase the
  // memory of what to avoid repeating next.
  let lastPulse = new Set();

  function reshuffle() {
    const roll = Math.random();
    const count = roll < 0.15 ? 0 : (roll < 0.7 ? 1 : Math.min(2, rings.length));

    let glowing = new Set();
    if (count > 0) {
      // Prefer rings that weren't part of the last pulse, so it visibly
      // rotates around the trio instead of occasionally re-picking the same
      // ring -- which, since its class wouldn't change, would just hold
      // steady for that interval instead of actually pulsing. Falls back to
      // a repeat only if count exceeds how many "fresh" rings exist (e.g.
      // count=2 right after a 2-ring pulse leaves just 1 fresh candidate).
      const fresh = rings.filter(el => !lastPulse.has(el));
      const stale = rings.filter(el => lastPulse.has(el));
      const candidates = [...fresh].sort(() => Math.random() - 0.5)
        .concat([...stale].sort(() => Math.random() - 0.5));
      glowing = new Set(candidates.slice(0, count));
      lastPulse = glowing;
    }

    rings.forEach(el => el.classList.toggle('ring-glow', glowing.has(el)));
    setTimeout(reshuffle, minMs + Math.random() * (maxMs - minMs));
  }
  reshuffle();
}

function boot() {
  updateMasterTempo(120); // default until real host tempo is wired up
  initKnobs(document);
  initSliders(document);
  initSwitches(document);
  initDropdowns(document);
  initCCFields(document);
  initPillSelectors(document);
  initCompactControls();

  // Every control-creating init*() above has run (knob labels/value readouts,
  // dropdown/pill children), so the DOM is stable -- safe to snapshot now.
  buildControlCache();

  // Routed through kParamUIViewMode (not a direct showView() call) so the
  // choice rides the host's normal param state save/recall and survives the
  // plugin window being closed and reopened -- onParamStateChanged below
  // does the actual showView() once the param is set.
  const viewModeIdx = paramIdx('kParamUIViewMode');
  document.getElementById('btn-goto-compact').addEventListener('click', () => setParam(viewModeIdx, 1));
  document.getElementById('btn-goto-full').addEventListener('click', () => setParam(viewModeIdx, 0));
  document.getElementById('drag-export-btn-full').addEventListener('click', requestExport);
  document.getElementById('drag-export-btn-compact').addEventListener('click', requestExport);
  exportBtns.forEach(btn => {
    if (!btn) return;
    btn.draggable = true;
    btn.addEventListener('dragstart', onExportDragStart);
  });
  document.getElementById('btn-reroll').addEventListener('click', requestReroll);

  document.getElementById('bars-dec').addEventListener('click', () => {
    const idx = paramIdx('kParamExportBars');
    gestureBegin(idx); setParam(idx, state[idx] - 1); gestureEnd(idx);
  });
  document.getElementById('bars-inc').addEventListener('click', () => {
    const idx = paramIdx('kParamExportBars');
    gestureBegin(idx); setParam(idx, state[idx] + 1); gestureEnd(idx);
  });

  document.getElementById('variations-dec').addEventListener('click', () => {
    const idx = paramIdx('kParamExportVariations');
    gestureBegin(idx); setParam(idx, state[idx] - 1); gestureEnd(idx);
  });
  document.getElementById('variations-inc').addEventListener('click', () => {
    const idx = paramIdx('kParamExportVariations');
    gestureBegin(idx); setParam(idx, state[idx] + 1); gestureEnd(idx);
  });

  refreshAllControls();
  updateSummaryReadouts();
  document.getElementById('bars-readout').textContent = state[paramIdx('kParamExportBars')] + ' BARS';
  document.getElementById('variations-readout').textContent = state[paramIdx('kParamExportVariations')];
  document.getElementById('compact-midi-badge').textContent = 'CH ' + state[paramIdx('kParamMidiChannel')];

  randomizeAmbientPhase();

  // Every .knob-canvas (Full's Drift knob, Compact's Chaos knob) gets its
  // own independent glow scheduler over its own ring trio.
  document.querySelectorAll('.knob-canvas').forEach(canvas => {
    setupRingGlow([
      canvas.querySelector('.ambient-ring-outer'),
      canvas.querySelector('.ambient-ring-mid'),
      canvas.querySelector('.ambient-ring-inner'),
    ], 6000, 12000);
  });

  showView('full');
}

// ---- Delegate callbacks required by iplug.js ----

function OnParamChange(paramIdx_, normValue) {
  const p = PARAMS[paramIdx_];
  if (!p) return;
  setParam(paramIdx_, normToReal(p, normValue), { fromHost: true, sendToHost: false });
}

function OnControlChange(ctrlTag, value) {}
function OnControlMessage(ctrlTag, msgTag, msg) {}
function OnMidiMsg(statusByte, d1, d2) {}

// The PARAMS array above is positional, not name-based -- it has to match
// EParams in Drift.h index-for-index (see the comment at the top of
// this file). Nothing on the C++ side enforces that; if the two ever drift
// (a param inserted/removed/reordered on one side and not the other), every
// knob below the drift point silently controls the wrong parameter. The
// real host already sends its actual param list on load (see below) --
// checking the one thing that's cheap and unambiguous to check here (the
// count) turns a silent mismatch into a loud one instead of catching it by
// finding the wrong knob attached to the wrong wire the hard way.
function checkParamRegistrySync(hostParams) {
  if (!Array.isArray(hostParams)) return;
  if (hostParams.length !== PARAMS.length) {
    console.error(
      `[app.js] PARAM REGISTRY MISMATCH: host reports ${hostParams.length} params, ` +
      `local PARAMS array has ${PARAMS.length}. EParams in Drift.h and the ` +
      `PARAMS array in app.js have drifted -- every control is likely bound to the wrong parameter.`
    );
  }
}

function OnMessage(msgTag, dataSize, data) {
  // Real host sends full param metadata here on load (msgTag == -1).
  if (msgTag === -1 && dataSize > 0) {
    try {
      const json = JSON.parse(window.atob(data));
      checkParamRegistrySync(json.params);
    } catch (e) { /* ignore */ }
  } else if (msgTag === 101) {
    // data is the raw exported file's absolute path (not base64-JSON like
    // msgTag -1/103 -- see Drift.cpp's OnMessage, which sends the
    // WDL_String's raw bytes directly). SendArbitraryMsgFromDelegate always
    // base64-encodes the payload regardless of what it semantically is, so
    // this still needs the same atob() decode.
    let filePath = null;
    try { filePath = window.atob(data); } catch (e) { /* ignore */ }
    onExportResult(true, filePath);
  } else if (msgTag === 102) {
    onExportResult(false);
  } else if (msgTag === 103) {
    // Live host tempo (see Drift::OnIdle) -- keeps the ambient
    // breathing animation's cycle length tempo-relative instead of the
    // hardcoded 120bpm fallback used until the first update arrives.
    const bpm = parseFloat(window.atob(data));
    if (!isNaN(bpm)) updateMasterTempo(bpm);
  }
}

document.addEventListener('DOMContentLoaded', boot);
