// iplug.js — the JS<->C++ bridge, following iPlug2's own convention
// (see Examples/IPlugWebUI/resources/web/script.js). When this page runs
// inside the real plugin, the C++ WebViewEditorDelegate auto-injects a
// global IPlugSendMsg() function before this script runs. When there is no
// host (plain browser preview, e.g. this Browser pane), IPlugSendMsg is
// stubbed out so the UI is still fully click/drag-able during development.

window.IPLUG_HAS_HOST = typeof window.IPlugSendMsg === 'function';

if (!window.IPLUG_HAS_HOST) {
  console.log('[iplug.js] No native host detected -- running in standalone preview mode.');
  window.IPlugSendMsg = function (message) {
    console.log('[preview] IPlugSendMsg', message);
  };
}

// ---- FROM UI (JS -> C++) ----

function SAMFUI(msgTag, ctrlTag = -1, data = 0) {
  IPlugSendMsg({ msg: 'SAMFUI', msgTag, ctrlTag, data });
}

function SMMFUI(statusByte, dataByte1, dataByte2) {
  IPlugSendMsg({ msg: 'SMMFUI', statusByte, dataByte1, dataByte2 });
}

function SSMFUI(data = 0) {
  IPlugSendMsg({ msg: 'SSMFUI', data });
}

function EPCFUI(paramIdx) {
  if (paramIdx < 0) return;
  IPlugSendMsg({ msg: 'EPCFUI', paramIdx: parseInt(paramIdx) });
}

function BPCFUI(paramIdx) {
  if (paramIdx < 0) return;
  IPlugSendMsg({ msg: 'BPCFUI', paramIdx: parseInt(paramIdx) });
}

function SPVFUI(paramIdx, value) {
  if (paramIdx < 0) return;
  IPlugSendMsg({ msg: 'SPVFUI', paramIdx: parseInt(paramIdx), value });
}

// ---- FROM DELEGATE (C++ -> JS) ----
// These are called BY NAME from the native side. app.js provides the real
// implementations (OnParamChange / OnControlChange / OnMessage); these are
// just the required global entry points the host calls into.

function SPVFD(paramIdx, val) {
  if (typeof OnParamChange === 'function') OnParamChange(paramIdx, val);
}

function SCVFD(ctrlTag, val) {
  if (typeof OnControlChange === 'function') OnControlChange(ctrlTag, val);
}

function SCMFD(ctrlTag, msgTag, msg) {
  if (typeof OnControlMessage === 'function') OnControlMessage(ctrlTag, msgTag, msg);
}

function SAMFD(msgTag, dataSize, msg) {
  if (typeof OnMessage === 'function') OnMessage(msgTag, dataSize, msg);
}

function SMMFD(statusByte, dataByte1, dataByte2) {
  if (typeof OnMidiMsg === 'function') OnMidiMsg(statusByte, dataByte1, dataByte2);
}

function SSMFD(offset, size, msg) {
  // Sysex from delegate -- unused by this plugin.
}
