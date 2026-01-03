const statusEl = document.getElementById("status");
const dictEl = document.getElementById("dict");
const dictStatusEl = document.getElementById("dictStatus");
const guessEl = document.getElementById("guess");
const targetEl = document.getElementById("target");
const patternOut = document.getElementById("patternOut");
const patternInputEl = document.getElementById("patternInput");
const bestGuessOut = document.getElementById("bestGuessOut");
const entropySparkEl = document.getElementById("entropySpark");
const entropyBarsEl = document.getElementById("entropyBars");
const speedCountEl = document.getElementById("speedCount");
const speedOutEl = document.getElementById("speedOut");
const speedTestBtn = document.getElementById("speedTestBtn");
const stressCountEl = document.getElementById("stressCount");
const stressOutEl = document.getElementById("stressOut");
const toggleThreadsBtn = document.getElementById("toggleThreads");
const speedTest100Btn = document.getElementById("speedTest100Btn");
const coiStatusEl = document.getElementById("coiStatus");
const resetSwBtn = document.getElementById("resetSw");
const simdToggleEl = document.getElementById("simdToggle");
const lexicalWeightEl = document.getElementById("lexicalWeight");
const lexicalValueEl = document.getElementById("lexicalValue");
const threadStatusEl = document.getElementById("threadStatus");
const threadMeterEl = document.getElementById("threadMeter");
const techOpenBtn = document.getElementById("techOpen");
const techCloseBtn = document.getElementById("techClose");
const techModal = document.getElementById("techModal");
const connWordsEl = document.getElementById("connWords");
const connOut = document.getElementById("connOut");
const connMeta = document.getElementById("connMeta");
const logEl = document.getElementById("log");
const wordleHardEl = document.getElementById("wordleHard");
const connectionsHardEl = document.getElementById("connectionsHard");
const chartEl = document.getElementById("connChart");
const bestGuessBtn = document.getElementById("bestGuessBtn");
const solveConnBtn = document.getElementById("solveConn");

const MAX_LOG_LINES = 120;
const COI_RELOAD_KEY = "coiReloaded";
const COI_PENDING_KEY = "coiPending";
const COI_START_KEY = "coiStart";
const COI_WAIT_MS = 8000;
const SPEED_BASELINE_KEY = "speedBaseline";
const LEXICAL_WEIGHT_KEY = "lexicalWeight";
const ENVELOPE_SIGMA = 2.0;
const THREAD_POOL_SIZE = 4;

const sampleDict = [
  "raise",
  "crane",
  "slate",
  "arise",
  "crate",
  "slant",
  "trice",
  "trace",
  "irate",
  "stare",
  "share",
  "spare",
].join("\n");

const sampleConnections = [
  "bee",
  "tee",
  "cue",
  "sea",
  "brie",
  "feta",
  "gouda",
  "cheddar",
  "pinch",
  "nick",
  "swipe",
  "lift",
  "hand",
  "back",
  "arm",
  "face",
].join(" ");

dictEl.value = sampleDict;
connWordsEl.value = sampleConnections;

let Module = null;
let lastConnPoints = null;
let pcaWorker = null;
let pcaWorkerReady = false;
let pendingSimdEnabled = true;

const GROUP_COLORS = ["#fbbf24", "#22c55e", "#3b82f6", "#a855f7"];

function logLine(message) {
  const time = new Date().toLocaleTimeString();
  const next = `[${time}] ${message}\n` + logEl.textContent;
  const lines = next.split("\n").filter((line) => line.length > 0);
  logEl.textContent = lines.slice(0, MAX_LOG_LINES).join("\n");
}

function updateCoiStatus() {
  if (!coiStatusEl) {
    return;
  }
  const enabled = Boolean(window.crossOriginIsolated);
  coiStatusEl.textContent = `COI: ${enabled ? "on" : "off"}`;
  coiStatusEl.classList.toggle("on", enabled);
  coiStatusEl.classList.toggle("off", !enabled);
}

function initThreadMeter() {
  if (!threadMeterEl || !threadStatusEl) {
    return;
  }
  threadMeterEl.textContent = "";
  const maxThreads = useThreads ? THREAD_POOL_SIZE : 1;
  for (let i = 0; i < maxThreads; i++) {
    const bar = document.createElement("div");
    bar.className = "thread-bar";
    threadMeterEl.appendChild(bar);
  }
  threadStatusEl.textContent = `Threads: 0/${maxThreads}`;
}

function setThreadActivity(active) {
  if (!threadMeterEl || !threadStatusEl) {
    return;
  }
  const maxThreads = useThreads ? THREAD_POOL_SIZE : 1;
  const activeCount = active ? maxThreads : 0;
  threadStatusEl.textContent = `Threads: ${activeCount}/${maxThreads}`;
  threadStatusEl.classList.toggle("on", activeCount > 0);
  threadStatusEl.classList.toggle("off", activeCount === 0);
  const bars = Array.from(threadMeterEl.children);
  bars.forEach((bar, idx) => {
    bar.classList.toggle("active", idx < activeCount);
  });
}

function applySimdSetting(enabled, silent = false) {
  pendingSimdEnabled = enabled;
  if (!Module || !Module.wordleSetSimdEnabled) {
    return;
  }
  Module.wordleSetSimdEnabled(Boolean(enabled));
  if (!silent) {
    const mode = enabled ? "SIMD" : "scalar";
    logLine(`Wordle filtering set to ${mode} mode.`);
  }
}

function ensureWordleLoaded() {
  if (!Module) {
    return false;
  }
  if (Module.wordleRemainingCount && Module.wordleRemainingCount() > 0) {
    return true;
  }
  const words = dictEl.value
    .split(/\s+/)
    .map((w) => w.trim().toLowerCase())
    .filter((w) => w.length === 5);
  if (words.length === 0) {
    return false;
  }
  Module.loadWordleDict(dictEl.value);
  Module.wordleReset();
  return Module.wordleRemainingCount() > 0;
}

function measureFilterThroughput(simdEnabled, guess) {
  applySimdSetting(simdEnabled, true);
  Module.wordleReset();
  const before = Module.wordleRemainingCount();
  if (before <= 0) {
    return null;
  }
  const start = performance.now();
  Module.wordleApplyFeedback(guess, "22222");
  const elapsed = performance.now() - start;
  Module.wordleReset();
  const throughput = before / Math.max(0.001, elapsed * 1000);
  return { throughput, elapsed, count: before };
}

async function runSimdComparison() {
  if (!Module || !Module.wordleSetSimdEnabled) {
    return;
  }
  if (!ensureWordleLoaded()) {
    logLine("Load a dictionary to benchmark SIMD vs scalar.");
    return;
  }
  const words = dictEl.value
    .split(/\s+/)
    .map((w) => w.trim().toLowerCase())
    .filter((w) => w.length === 5);
  if (words.length === 0) {
    logLine("No valid words for SIMD benchmark.");
    return;
  }
  const guess = words[0];
  await new Promise((r) => requestAnimationFrame(r));
  const scalar = measureFilterThroughput(false, guess);
  const simd = measureFilterThroughput(true, guess);
  applySimdSetting(pendingSimdEnabled, true);
  if (scalar && simd) {
    logLine(
      `Throughput scalar: ${scalar.throughput.toFixed(
        2
      )} words/us | SIMD: ${simd.throughput.toFixed(2)} words/us`
    );
  }
}

function renderEntropyBars(items) {
  entropyBarsEl.textContent = "";
  if (!items || items.length === 0) {
    return;
  }
  const maxEntropy = Math.max(...items.map((item) => item.entropy || 0), 0.01);
  items.forEach((item) => {
    const row = document.createElement("div");
    row.className = "bar-row";
    const label = document.createElement("div");
    label.className = "bar-label";
    label.textContent = item.word.toUpperCase();
    const track = document.createElement("div");
    track.className = "bar-track";
    const fill = document.createElement("div");
    fill.className = "bar-fill";
    fill.style.width = `${Math.max(
      8,
      (item.entropy / maxEntropy) * 100
    )}%`;
    track.appendChild(fill);
    const value = document.createElement("div");
    value.textContent = `${item.entropy.toFixed(2)}b`;
    row.appendChild(label);
    row.appendChild(track);
    row.appendChild(value);
    entropyBarsEl.appendChild(row);
  });
}

function clearEntropySpark() {
  if (!entropySparkEl) {
    return;
  }
  const info = prepareCanvas(entropySparkEl);
  if (!info) {
    return;
  }
  const { ctx, width, height } = info;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#0b1220";
  ctx.fillRect(0, 0, width, height);
}

function renderEntropySpark(counts, total) {
  if (!entropySparkEl) {
    return;
  }
  if (!Array.isArray(counts) || counts.length === 0 || total <= 0) {
    clearEntropySpark();
    return;
  }
  const info = prepareCanvas(entropySparkEl);
  if (!info) {
    return;
  }
  const { ctx, width, height, dpr } = info;
  const maxCount = Math.max(...counts, 1);
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#0b1220";
  ctx.fillRect(0, 0, width, height);
  const pad = 4 * dpr;
  const spanX = Math.max(1, counts.length - 1);
  const scaleX = (width - pad * 2) / spanX;
  const scaleY = (height - pad * 2) / maxCount;
  ctx.beginPath();
  counts.forEach((count, idx) => {
    const x = pad + idx * scaleX;
    const y = height - pad - count * scaleY;
    if (idx === 0) {
      ctx.moveTo(x, y);
    } else {
      ctx.lineTo(x, y);
    }
  });
  ctx.strokeStyle = "#f59e0b";
  ctx.lineWidth = Math.max(1, 1.2 * dpr);
  ctx.globalAlpha = 0.85;
  ctx.stroke();
  ctx.globalAlpha = 1;
}

function refreshEntropyBars() {
  if (!Module || !Module.wordleTopGuesses) {
    return;
  }
  const hardMode = Boolean(wordleHardEl.checked);
  const result = Module.wordleTopGuesses(5, hardMode);
  if (!result) {
    return;
  }
  try {
    const payload = JSON.parse(result);
    if (Array.isArray(payload.items)) {
      renderEntropyBars(payload.items);
    }
  } catch (err) {
    logLine("Entropy data parse failed.");
  }
}

window.addEventListener("error", (event) => {
  logLine(`JS error: ${event.message || event.type}`);
});

window.addEventListener("unhandledrejection", (event) => {
  logLine(`Promise rejection: ${event.reason || "unknown"}`);
});

const wasmStart = performance.now();

const useThreads = window.crossOriginIsolated;
const wasmPath = useThreads
  ? "./aletheia_wasm_mt.js"
  : "./aletheia_wasm.js";

updateCoiStatus();
initThreadMeter();
setThreadActivity(false);

function ensureCoiServiceWorker() {
  if (!("serviceWorker" in navigator)) {
    return Promise.resolve(false);
  }
  return navigator.serviceWorker
    .register("./coi-serviceworker.js")
    .then(() => true)
    .catch(() => false);
}

function waitForServiceWorkerReady(timeoutMs = COI_WAIT_MS) {
  if (!("serviceWorker" in navigator)) {
    return Promise.resolve(false);
  }
  return Promise.race([
    navigator.serviceWorker.ready.then(() => true),
    new Promise((resolve) => setTimeout(() => resolve(false), timeoutMs)),
  ]);
}

function scheduleCoiReload() {
  if (!("serviceWorker" in navigator)) {
    return;
  }
  if (navigator.serviceWorker.controller) {
    location.reload();
    return;
  }
  navigator.serviceWorker.addEventListener(
    "controllerchange",
    () => {
      location.reload();
    },
    { once: true }
  );
}

function markCoiPending() {
  sessionStorage.setItem(COI_PENDING_KEY, "1");
  sessionStorage.setItem(COI_START_KEY, String(Date.now()));
}

function clearCoiPending() {
  sessionStorage.removeItem(COI_PENDING_KEY);
  sessionStorage.removeItem(COI_START_KEY);
}

function checkCoiPending() {
  if (window.crossOriginIsolated) {
    clearCoiPending();
    return;
  }
  if (sessionStorage.getItem(COI_PENDING_KEY) !== "1") {
    return;
  }
  const started = Number(sessionStorage.getItem(COI_START_KEY)) || 0;
  if (started && Date.now() - started > COI_WAIT_MS) {
    logLine(
      "COI still off. Close other localhost tabs or click Reset SW, then reload."
    );
    clearCoiPending();
  }
}

if (!window.crossOriginIsolated) {
  ensureCoiServiceWorker().then((ready) => {
    if (
      ready &&
      !navigator.serviceWorker.controller &&
      !sessionStorage.getItem(COI_RELOAD_KEY)
    ) {
      sessionStorage.setItem(COI_RELOAD_KEY, "1");
      markCoiPending();
      scheduleCoiReload();
    }
  });
}

if (resetSwBtn) {
  resetSwBtn.addEventListener("click", async () => {
    if (!("serviceWorker" in navigator)) {
      logLine("Service worker API unavailable.");
      return;
    }
    try {
      const regs = await navigator.serviceWorker.getRegistrations();
      if (regs.length === 0) {
        logLine("No service worker registrations found.");
        return;
      }
      for (const reg of regs) {
        await reg.unregister();
      }
      logLine("Service worker unregistered. Reloading...");
      location.reload();
    } catch (err) {
      logLine("Service worker reset failed.");
    }
  });
}

if (toggleThreadsBtn) {
  if (useThreads) {
    toggleThreadsBtn.textContent = "Multi-core WASM enabled";
    toggleThreadsBtn.disabled = true;
  } else {
    toggleThreadsBtn.addEventListener("click", () => {
      toggleThreadsBtn.textContent = "Enabling Multi-core...";
      toggleThreadsBtn.disabled = true;
      sessionStorage.removeItem(COI_RELOAD_KEY);
      markCoiPending();
      ensureCoiServiceWorker().then(async (ready) => {
        if (!ready) {
          logLine("Service worker unavailable; multi-core disabled.");
          toggleThreadsBtn.textContent = "Enable Multi-core WASM";
          toggleThreadsBtn.disabled = false;
          clearCoiPending();
          return;
        }
        logLine("Enabling multi-core WASM...");
        const swReady = await waitForServiceWorkerReady();
        if (!swReady) {
          logLine("Service worker ready timed out. Try Reset SW.");
          toggleThreadsBtn.textContent = "Enable Multi-core WASM";
          toggleThreadsBtn.disabled = false;
          clearCoiPending();
          return;
        }
        scheduleCoiReload();
      });
    });
  }
}

if (techOpenBtn && techModal) {
  techOpenBtn.addEventListener("click", () => {
    techModal.showModal();
    if (window.renderMathInElement && !techModal.dataset.mathRendered) {
      window.renderMathInElement(techModal, {
        delimiters: [
          { left: "\\[", right: "\\]", display: true },
          { left: "$$", right: "$$", display: true },
          { left: "\\(", right: "\\)", display: false },
        ],
        throwOnError: false,
      });
      techModal.dataset.mathRendered = "1";
    }
  });
}

if (techCloseBtn && techModal) {
  techCloseBtn.addEventListener("click", () => {
    techModal.close();
  });
}

if (techModal) {
  techModal.addEventListener("click", (event) => {
    if (event.target === techModal) {
      techModal.close();
    }
  });
}

setTimeout(checkCoiPending, COI_WAIT_MS + 250);

statusEl.textContent = `Loading WASM (${useThreads ? "multi-core" : "single-core"})...`;

import(wasmPath)
  .then((mod) => mod.default())
  .then((mod) => {
    Module = mod;
    const elapsed = ((performance.now() - wasmStart) / 1000).toFixed(2);
    const modeLabel = useThreads ? "multi-core" : "single-core";
    statusEl.textContent = `WASM ready (${elapsed}s, ${modeLabel}).`;
    if (!useThreads) {
      logLine("Use Enable Multi-core WASM to switch to pthreads.");
    }
    if (simdToggleEl) {
      if (Module.wordleSimdEnabled && Module.wordleSetSimdEnabled) {
        const current = Boolean(Module.wordleSimdEnabled());
        simdToggleEl.checked = current;
        applySimdSetting(current, true);
      } else {
        simdToggleEl.checked = false;
        simdToggleEl.disabled = true;
      }
    }
  })
  .catch((err) => {
    statusEl.textContent = "WASM failed to load. Check console/network.";
    logLine(`WASM load failed: ${err}`);
  });

function prepareCanvas(canvas) {
  if (!canvas) {
    return null;
  }
  const ctx = canvas.getContext("2d");
  if (!ctx) {
    return null;
  }
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  const width = Math.max(1, Math.floor(rect.width * dpr));
  const height = Math.max(1, Math.floor(rect.height * dpr));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  return { ctx, width, height, dpr };
}

function measureCanvasSize(canvas) {
  if (!canvas) {
    return null;
  }
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  const width = Math.max(1, Math.floor(rect.width * dpr));
  const height = Math.max(1, Math.floor(rect.height * dpr));
  return { width, height, dpr };
}

function initPcaWorker() {
  if (!chartEl || !chartEl.transferControlToOffscreen || !window.Worker) {
    return;
  }
  if (pcaWorkerReady) {
    return;
  }
  try {
    const worker = new Worker("./pca_worker.js", { type: "module" });
    const offscreen = chartEl.transferControlToOffscreen();
    const size = measureCanvasSize(chartEl) || { width: 1, height: 1, dpr: 1 };
    worker.postMessage(
      {
        type: "init",
        canvas: offscreen,
        width: size.width,
        height: size.height,
        dpr: size.dpr,
        colors: GROUP_COLORS,
      },
      [offscreen]
    );
    worker.addEventListener("error", () => {
      logLine("PCA worker failed; rendering disabled.");
    });
    pcaWorker = worker;
    pcaWorkerReady = true;
  } catch (err) {
    logLine("PCA worker init failed.");
  }
}

function updatePcaWorkerSize() {
  if (!pcaWorkerReady || !pcaWorker) {
    return;
  }
  const size = measureCanvasSize(chartEl);
  if (!size) {
    return;
  }
  pcaWorker.postMessage({
    type: "resize",
    width: size.width,
    height: size.height,
    dpr: size.dpr,
  });
}

function splitWords(text) {
  return text
    .split(/\s+/)
    .map((word) => word.trim().toLowerCase())
    .filter((word) => word.length > 0);
}

function updateLexicalValue() {
  if (!lexicalValueEl || !lexicalWeightEl) {
    return;
  }
  const value = Number(lexicalWeightEl.value || 0);
  lexicalValueEl.textContent = value.toFixed(2);
  localStorage.setItem(LEXICAL_WEIGHT_KEY, value.toFixed(2));
}

function getLexicalWeight() {
  if (!lexicalWeightEl) {
    return 0;
  }
  return Number(lexicalWeightEl.value) || 0;
}

function hashUnit(word, seed) {
  let hash = 2166136261 ^ seed;
  for (let i = 0; i < word.length; i++) {
    hash ^= word.charCodeAt(i);
    hash = Math.imul(hash, 16777619);
  }
  return (hash >>> 0) / 4294967296;
}

function buildFallbackPoints(words, groups, groupConfidence) {
  if (!Array.isArray(words) || words.length === 0) return [];
  const groupMap = new Map();
  if (Array.isArray(groups)) {
    groups.forEach((group, idx) => {
      if (!Array.isArray(group)) return;
      group.forEach((word) => groupMap.set(word, idx));
    });
  }
  return words.map((word, idx) => {
    const x = hashUnit(word, 0x9e3779b1) * 2 - 1;
    const y = hashUnit(word, 0x85ebca6b) * 2 - 1;
    const group = groupMap.has(word) ? groupMap.get(word) : idx % 4;
    const confidence =
      Array.isArray(groupConfidence) &&
      group >= 0 &&
      group < groupConfidence.length
        ? Number(groupConfidence[group]) || 0
        : 0;
    return {
      word,
      x,
      y,
      group,
      margin: 0,
      centroid_dist: 0,
      confidence,
    };
  });
}

function eigenDecomposition2x2(a, b, c) {
  const trace = a + c;
  const det = a * c - b * b;
  const term = Math.sqrt(Math.max(0, (trace * trace) / 4 - det));
  const l1 = trace / 2 + term;
  const l2 = trace / 2 - term;
  const angle = 0.5 * Math.atan2(2 * b, a - c);
  return { l1, l2, angle };
}

function computeEnvelopes(points) {
  const groups = [[], [], [], []];
  points.forEach((point) => {
    const group = point.group >= 0 && point.group < 4 ? point.group : 0;
    groups[group].push(point);
  });
  const envelopes = [];
  groups.forEach((groupPoints, idx) => {
    if (groupPoints.length < 2) {
      return;
    }
    let sumX = 0;
    let sumY = 0;
    groupPoints.forEach((point) => {
      sumX += point.x;
      sumY += point.y;
    });
    const n = groupPoints.length;
    const meanX = sumX / n;
    const meanY = sumY / n;
    let covXX = 0;
    let covYY = 0;
    let covXY = 0;
    groupPoints.forEach((point) => {
      const dx = point.x - meanX;
      const dy = point.y - meanY;
      covXX += dx * dx;
      covYY += dy * dy;
      covXY += dx * dy;
    });
    const denom = Math.max(1, n - 1);
    covXX /= denom;
    covYY /= denom;
    covXY /= denom;
    const { l1, l2, angle } = eigenDecomposition2x2(covXX, covXY, covYY);
    const rx = Math.sqrt(Math.max(0, l1)) * ENVELOPE_SIGMA;
    const ry = Math.sqrt(Math.max(0, l2)) * ENVELOPE_SIGMA;
    if (!Number.isFinite(rx) || !Number.isFinite(ry) || rx <= 0 || ry <= 0) {
      return;
    }
    envelopes.push({
      cx: meanX,
      cy: meanY,
      rx,
      ry,
      angle,
      color: GROUP_COLORS[idx] || GROUP_COLORS[0],
    });
  });
  return envelopes;
}

function pointsHaveSpread(points) {
  if (!Array.isArray(points) || points.length === 0) {
    return false;
  }
  let minX = points[0].x;
  let maxX = points[0].x;
  let minY = points[0].y;
  let maxY = points[0].y;
  for (const point of points) {
    if (!Number.isFinite(point.x) || !Number.isFinite(point.y)) {
      return false;
    }
    if (point.x < minX) minX = point.x;
    if (point.x > maxX) maxX = point.x;
    if (point.y < minY) minY = point.y;
    if (point.y > maxY) maxY = point.y;
  }
  return Math.abs(maxX - minX) > 1e-6 || Math.abs(maxY - minY) > 1e-6;
}

initPcaWorker();
clearEntropySpark();

if (lexicalWeightEl) {
  const savedWeight = Number(localStorage.getItem(LEXICAL_WEIGHT_KEY));
  if (Number.isFinite(savedWeight)) {
    lexicalWeightEl.value = savedWeight.toFixed(2);
  }
  updateLexicalValue();
  lexicalWeightEl.addEventListener("input", updateLexicalValue);
}

if (simdToggleEl) {
  pendingSimdEnabled = simdToggleEl.checked;
  simdToggleEl.addEventListener("change", () => {
    applySimdSetting(simdToggleEl.checked);
  });
}

window.addEventListener("resize", () => {
  updatePcaWorkerSize();
  if (lastConnPoints) {
    renderPoints(lastConnPoints);
  }
});

document.getElementById("loadDict").addEventListener("click", () => {
  if (!Module) return;
  Module.loadWordleDict(dictEl.value);
  Module.wordleReset();
  const remaining = Module.wordleRemainingCount();
  logLine(`Wordle dictionary loaded (${remaining} words).`);
  dictStatusEl.textContent = "Dictionary loaded.";
});

document.getElementById("resetWordle").addEventListener("click", () => {
  if (!Module) return;
  Module.wordleReset();
  logLine("Wordle session reset.");
  dictStatusEl.textContent = "Session reset.";
  entropyBarsEl.textContent = "";
  clearEntropySpark();
});

document.getElementById("patternBtn").addEventListener("click", () => {
  if (!Module) return;
  const guess = guessEl.value.trim();
  const target = targetEl.value.trim();
  const pattern = Module.wordlePattern(guess, target);
  patternOut.textContent = pattern ? `Pattern: ${pattern}` : "Invalid input.";
});

document.getElementById("applyPatternBtn").addEventListener("click", () => {
  if (!Module) return;
  const guess = guessEl.value.trim();
  const pattern = patternInputEl.value.trim();
  if (wordleHardEl.checked && !Module.wordleIsCandidate(guess)) {
    logLine("Hard mode: guess must match all revealed hints.");
    return;
  }
  const remaining = Module.wordleApplyFeedback(guess, pattern);
  if (remaining < 0) {
    logLine("Invalid Wordle feedback.");
    return;
  }
  logLine(`Applied feedback: ${guess.toUpperCase()} ${pattern}, remaining ${remaining}.`);
});

document.getElementById("bestGuessBtn").addEventListener("click", () => {
  if (!Module) return;
  if (bestGuessBtn) bestGuessBtn.disabled = true;
  const hardMode = Boolean(wordleHardEl.checked);
  const result = Module.wordleBestGuess(hardMode);
  if (!result) {
    bestGuessOut.textContent = "Load a dictionary first.";
    clearEntropySpark();
    if (bestGuessBtn) bestGuessBtn.disabled = false;
    return;
  }
  const [guess, entropy] = result.split("|");
  bestGuessOut.textContent = `Best guess: ${guess} (entropy ${Number(
    entropy
  ).toFixed(4)})`;
  const remaining = Module.wordleRemainingCount();
  logLine(
    `Best guess (${hardMode ? "hard" : "normal"}): ${guess.toUpperCase()}, entropy ${Number(
      entropy
    ).toFixed(4)}, remaining ${remaining}`
  );
  refreshEntropyBars();
  if (Module.wordlePatternHistogram) {
    const histogram = Module.wordlePatternHistogram(guess);
    try {
      const payload = JSON.parse(histogram);
      if (payload.counts && payload.total) {
        renderEntropySpark(payload.counts, payload.total);
      } else {
        clearEntropySpark();
      }
    } catch (err) {
      clearEntropySpark();
    }
  } else {
    clearEntropySpark();
  }
  if (bestGuessBtn) bestGuessBtn.disabled = false;
});

document.getElementById("solveConn").addEventListener("click", async () => {
  if (!Module) return;
  const hardMode = Boolean(connectionsHardEl.checked);
  connOut.textContent = "Solving...";
  connMeta.textContent = "Running clustering + PCA...";
  if (solveConnBtn) solveConnBtn.disabled = true;
  setThreadActivity(true);
  await new Promise((resolve) => requestAnimationFrame(resolve));
  try {
    const lexicalWeight = getLexicalWeight();
    const result = Module.connectionsSolveWeighted
      ? Module.connectionsSolveWeighted(connWordsEl.value, hardMode, lexicalWeight)
      : Module.connectionsSolveDetailed(connWordsEl.value, hardMode);
    const payload = JSON.parse(result);
    if (payload.error) {
      connOut.textContent = payload.error;
      logLine("Connections error: " + payload.error);
      return;
    }
    const lines = payload.groups.map((group, i) => {
      return `Group ${i + 1}: ${group.join(", ")}`;
    });
    connOut.textContent = lines.join("\n");
    const avgConfidence = payload.group_confidence
      ? (payload.group_confidence.reduce((a, b) => a + b, 0) /
          payload.group_confidence.length)
      : 0;
    const variance = Array.isArray(payload.variance) ? payload.variance : null;
    const varianceText = variance
      ? ` | EVR: ${Number(variance[0]).toFixed(2)}/${Number(variance[1]).toFixed(2)}`
      : "";
    const weightText =
      payload.lexical_weight != null
        ? ` | Lexical weight: ${Number(payload.lexical_weight).toFixed(2)}`
        : "";
    let vectorNote = "Vector space: PCA";
    let points = payload.points;
    let fallbackPoints = null;
    const pointsValid =
      Array.isArray(points) &&
      points.length === 16 &&
      pointsHaveSpread(points);
    if (!pointsValid) {
      const words = splitWords(connWordsEl.value);
      fallbackPoints = buildFallbackPoints(
        words,
        payload.groups,
        payload.group_confidence
      );
      vectorNote = "Vector space: fallback";
      logLine("Vector space invalid; using fallback projection.");
    }
    connMeta.textContent = `Avg confidence: ${avgConfidence.toFixed(
      3
    )} | Lexical boost: ${payload.lexical_boosted ? "yes" : "no"}${weightText}${varianceText} | ${vectorNote} | Envelopes: on`;
    logLine(
      `Connections solved (${hardMode ? "hard" : "normal"}). Avg confidence ${avgConfidence.toFixed(
        3
      )}.`
    );
    if (pointsValid) {
      const rendered = renderPoints(points);
      if (!rendered) {
        logLine("PCA points invalid; using fallback projection.");
        const words = splitWords(connWordsEl.value);
        fallbackPoints =
          fallbackPoints ||
          buildFallbackPoints(
            words,
            payload.groups,
            payload.group_confidence
          );
        if (fallbackPoints.length > 0) {
          renderPoints(fallbackPoints);
          vectorNote = "Vector space: fallback";
          connMeta.textContent = `Avg confidence: ${avgConfidence.toFixed(
            3
          )} | Lexical boost: ${payload.lexical_boosted ? "yes" : "no"}${weightText}${varianceText} | ${vectorNote} | Envelopes: on`;
        }
      }
    } else if (fallbackPoints && fallbackPoints.length > 0) {
      renderPoints(fallbackPoints);
    }
  } catch (err) {
    connOut.textContent = "Failed to parse output.";
  } finally {
    if (solveConnBtn) solveConnBtn.disabled = false;
    setThreadActivity(false);
  }
});

function drawPlot(ctx, width, height, points, envelopes, dpr) {
  let minX = points[0].x;
  let maxX = points[0].x;
  let minY = points[0].y;
  let maxY = points[0].y;
  for (const point of points) {
    if (point.x < minX) minX = point.x;
    if (point.x > maxX) maxX = point.x;
    if (point.y < minY) minY = point.y;
    if (point.y > maxY) maxY = point.y;
  }
  const spanX = Math.max(1e-6, maxX - minX);
  const spanY = Math.max(1e-6, maxY - minY);
  const extraX = spanX * 0.08;
  const extraY = spanY * 0.08;
  minX -= extraX;
  maxX += extraX;
  minY -= extraY;
  maxY += extraY;
  const paddedSpanX = Math.max(1e-6, maxX - minX);
  const paddedSpanY = Math.max(1e-6, maxY - minY);
  const pad = 10 * dpr;
  const scaleX = (width - pad * 2) / paddedSpanX;
  const scaleY = (height - pad * 2) / paddedSpanY;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#0b1220";
  ctx.fillRect(0, 0, width, height);
  envelopes.forEach((env) => {
    const x = pad + (env.cx - minX) * scaleX;
    const y = pad + (env.cy - minY) * scaleY;
    const py = height - y;
    const rx = Math.max(1, env.rx * scaleX);
    const ry = Math.max(1, env.ry * scaleY);
    ctx.save();
    ctx.translate(x, py);
    ctx.rotate(-env.angle);
    ctx.beginPath();
    ctx.ellipse(0, 0, rx, ry, 0, 0, Math.PI * 2);
    ctx.strokeStyle = env.color;
    ctx.lineWidth = Math.max(1, 1.4 * dpr);
    ctx.globalAlpha = 0.55;
    ctx.stroke();
    ctx.restore();
  });
  const radius = 3 * dpr;
  for (const point of points) {
    const group = point.group >= 0 && point.group < 4 ? point.group : 0;
    const color = GROUP_COLORS[group] || GROUP_COLORS[0];
    const x = pad + (point.x - minX) * scaleX;
    const y = pad + (point.y - minY) * scaleY;
    const py = height - y;
    ctx.beginPath();
    ctx.arc(x, py, radius, 0, Math.PI * 2);
    ctx.fillStyle = color;
    ctx.fill();
  }
}

function renderPoints(points) {
  const safePoints = points.filter(
    (point) => Number.isFinite(point.x) && Number.isFinite(point.y)
  );
  if (safePoints.length === 0) {
    logLine("PCA points missing or invalid.");
    return false;
  }
  const envelopes = computeEnvelopes(safePoints);
  lastConnPoints = safePoints;
  if (pcaWorkerReady && pcaWorker) {
    try {
      updatePcaWorkerSize();
      pcaWorker.postMessage({ type: "render", points: safePoints, envelopes });
      return true;
    } catch (err) {
      logLine("PCA worker render failed.");
    }
  }
  const canvasInfo = prepareCanvas(chartEl);
  if (!canvasInfo) {
    logLine("Vector space canvas unavailable.");
    return false;
  }
  drawPlot(canvasInfo.ctx, canvasInfo.width, canvasInfo.height, safePoints, envelopes, canvasInfo.dpr);
  return true;
}

async function runSpeedTest(requestedCount) {
  if (!Module) return;
  const count = Math.max(1, Math.min(500, Number(requestedCount) || 100));
  if (!ensureWordleLoaded()) {
    speedOutEl.textContent = "Load a dictionary first.";
    return;
  }
  const hardMode = Boolean(wordleHardEl.checked);
  const words = dictEl.value
    .split(/\s+/)
    .map((w) => w.trim().toLowerCase())
    .filter((w) => w.length === 5);
  if (words.length === 0) {
    speedOutEl.textContent = "Load a dictionary first.";
    return;
  }
  if (speedTestBtn) speedTestBtn.disabled = true;
  if (speedTest100Btn) speedTest100Btn.disabled = true;
  setThreadActivity(true);
  logLine(`Speed test started (${count} games, ${hardMode ? "hard" : "normal"}).`);
  if (Module.wordleSpeedTest) {
    const result = Module.wordleSpeedTest(count, hardMode);
    try {
      const payload = JSON.parse(result);
      if (payload.error) {
        speedOutEl.textContent = payload.error;
        logLine(`Speed test error: ${payload.error}`);
        return;
      }
      const winRate = Number(payload.win_rate) || 0;
      const avgGuesses = Number(payload.avg_guesses) || 0;
      const p99 = Number(payload.p99_ms) || 0;
      speedOutEl.textContent = `Win rate: ${winRate.toFixed(
        1
      )}% | Avg guesses: ${avgGuesses.toFixed(2)} | P99: ${p99.toFixed(2)}ms`;
      logLine(
        `Speed test done. Win rate ${winRate.toFixed(
          1
        )}%, avg guesses ${avgGuesses.toFixed(2)}, P99 ${p99.toFixed(2)}ms.`
      );
      if (!useThreads) {
        const baseline = {
          count,
          hardMode,
          p99,
          timestamp: Date.now(),
        };
        localStorage.setItem(SPEED_BASELINE_KEY, JSON.stringify(baseline));
        logLine("Saved single-core baseline for comparison.");
      } else {
        const baselineRaw = localStorage.getItem(SPEED_BASELINE_KEY);
        if (baselineRaw) {
          try {
            const baseline = JSON.parse(baselineRaw);
            if (baseline && Number.isFinite(baseline.p99)) {
              const delta = baseline.p99 - p99;
              const pct = (delta / baseline.p99) * 100;
              logLine(
                `Multi-core delta vs baseline: ${delta.toFixed(2)}ms (${pct.toFixed(1)}%).`
              );
            }
          } catch (err) {
            logLine("Baseline parse failed.");
          }
        }
      }
    } catch (err) {
      speedOutEl.textContent = "Speed test parse failed.";
    } finally {
      if (Module.wordleSetSimdEnabled) {
        await runSimdComparison();
      }
      if (speedTestBtn) speedTestBtn.disabled = false;
      if (speedTest100Btn) speedTest100Btn.disabled = false;
      setThreadActivity(false);
    }
    return;
  }
  const latencies = [];
  let wins = 0;
  let totalGuesses = 0;
  for (let i = 0; i < count; i++) {
    const target = words[Math.floor(Math.random() * words.length)];
    Module.wordleReset();
    const start = performance.now();
    let solved = false;
    let guesses = 0;
    for (let step = 0; step < 6; step++) {
      const result = Module.wordleBestGuess(hardMode);
      if (!result) break;
      const [guess] = result.split("|");
      const pattern = Module.wordlePattern(guess, target);
      if (!pattern) break;
      Module.wordleApplyFeedback(guess, pattern);
      guesses += 1;
      if (pattern === "22222") {
        solved = true;
        break;
      }
    }
    const elapsed = performance.now() - start;
    latencies.push(elapsed);
    totalGuesses += Math.max(guesses, 1);
    if (solved) wins += 1;
    if ((i + 1) % 20 === 0) {
      await new Promise((r) => setTimeout(r, 0));
    }
  }
  latencies.sort((a, b) => a - b);
  const percentile = (pct) =>
    latencies[Math.min(latencies.length - 1, Math.floor((pct / 100) * latencies.length))] || 0;
  const p50 = percentile(50);
  const p90 = percentile(90);
  const p99 = percentile(99);
  const winRate = (wins / count) * 100;
  const avgGuesses = totalGuesses / count;
  speedOutEl.textContent = `Win rate: ${winRate.toFixed(
    1
  )}% | Avg guesses: ${avgGuesses.toFixed(2)} | P99: ${p99.toFixed(2)}ms`;
  logLine(
    `Speed test done. Win rate ${winRate.toFixed(
      1
    )}%, avg guesses ${avgGuesses.toFixed(2)}, P99 ${p99.toFixed(2)}ms.`
  );
  if (!useThreads) {
    const baseline = {
      count,
      hardMode,
      p99,
      timestamp: Date.now(),
    };
    localStorage.setItem(SPEED_BASELINE_KEY, JSON.stringify(baseline));
    logLine("Saved single-core baseline for comparison.");
  } else {
    const baselineRaw = localStorage.getItem(SPEED_BASELINE_KEY);
    if (baselineRaw) {
      try {
        const baseline = JSON.parse(baselineRaw);
        if (baseline && Number.isFinite(baseline.p99)) {
          const delta = baseline.p99 - p99;
          const pct = (delta / baseline.p99) * 100;
          logLine(
            `Multi-core delta vs baseline: ${delta.toFixed(2)}ms (${pct.toFixed(1)}%).`
          );
        }
      } catch (err) {
        logLine("Baseline parse failed.");
      }
    }
  }
  if (Module.wordleSetSimdEnabled) {
    await runSimdComparison();
  }
  if (speedTestBtn) speedTestBtn.disabled = false;
  if (speedTest100Btn) speedTest100Btn.disabled = false;
  setThreadActivity(false);
}

if (speedTestBtn) {
  speedTestBtn.addEventListener("click", () => {
    runSpeedTest(speedCountEl.value);
  });
}

if (speedTest100Btn) {
  speedTest100Btn.addEventListener("click", () => {
    runSpeedTest(100);
  });
}

document.getElementById("stressTestBtn").addEventListener("click", () => {
  if (!Module || !Module.wordleAdversarialStress) {
    stressOutEl.textContent = "WASM update required.";
    return;
  }
  const count = Math.max(1, Math.min(200, Number(stressCountEl.value) || 25));
  const hardMode = Boolean(wordleHardEl.checked);
  const result = Module.wordleAdversarialStress(count, hardMode);
  if (!result) {
    stressOutEl.textContent = "Stress test failed.";
    return;
  }
  try {
    const payload = JSON.parse(result);
    if (payload.error) {
      stressOutEl.textContent = payload.error;
      return;
    }
    stressOutEl.textContent = `Worst step: ${payload.worst_ms.toFixed(
      2
    )}ms | Avg step: ${payload.avg_ms.toFixed(2)}ms | Steps: ${
      payload.steps
    }`;
    logLine(
      `Adversarial stress: worst ${payload.worst_ms.toFixed(
        2
      )}ms, avg ${payload.avg_ms.toFixed(2)}ms.`
    );
  } catch (err) {
    stressOutEl.textContent = "Stress test parse failed.";
  }
});
