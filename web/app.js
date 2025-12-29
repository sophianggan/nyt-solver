import createModule from "./aletheia_wasm.js";

const statusEl = document.getElementById("status");
const dictEl = document.getElementById("dict");
const dictStatusEl = document.getElementById("dictStatus");
const guessEl = document.getElementById("guess");
const targetEl = document.getElementById("target");
const patternOut = document.getElementById("patternOut");
const patternInputEl = document.getElementById("patternInput");
const bestGuessOut = document.getElementById("bestGuessOut");
const speedCountEl = document.getElementById("speedCount");
const speedOutEl = document.getElementById("speedOut");
const connWordsEl = document.getElementById("connWords");
const connOut = document.getElementById("connOut");
const connMeta = document.getElementById("connMeta");
const logEl = document.getElementById("log");
const wordleHardEl = document.getElementById("wordleHard");
const connectionsHardEl = document.getElementById("connectionsHard");
const chartEl = document.getElementById("connChart");

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
let chartReady = false;

const GROUP_COLORS = ["#fbbf24", "#22c55e", "#3b82f6", "#a855f7"];
const CHART_WIDTH = 400;
const CHART_HEIGHT = 240;
const CHART_PADDING = 28;

function logLine(message) {
  const time = new Date().toLocaleTimeString();
  logEl.textContent = `[${time}] ${message}\n` + logEl.textContent;
}

window.addEventListener("error", (event) => {
  logLine(`JS error: ${event.message || event.type}`);
});

window.addEventListener("unhandledrejection", (event) => {
  logLine(`Promise rejection: ${event.reason || "unknown"}`);
});

const wasmStart = performance.now();
initChart();

createModule()
  .then((mod) => {
    Module = mod;
    const elapsed = ((performance.now() - wasmStart) / 1000).toFixed(2);
    statusEl.textContent = `WASM ready (${elapsed}s).`;
  })
  .catch((err) => {
    statusEl.textContent = "WASM failed to load. Check console/network.";
    logLine(`WASM load failed: ${err}`);
  });

function initChart() {
  if (chartReady || !chartEl) return;
  chartReady = true;
  chartEl.setAttribute("viewBox", `0 0 ${CHART_WIDTH} ${CHART_HEIGHT}`);
  chartEl.setAttribute("preserveAspectRatio", "xMidYMid meet");
  drawChartFrame();
}

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
  logLine(
    `Applied feedback: ${guess.toUpperCase()} ${pattern}, remaining ${remaining}.`
  );
});

document.getElementById("bestGuessBtn").addEventListener("click", () => {
  if (!Module) return;
  const hardMode = Boolean(wordleHardEl.checked);
  const result = Module.wordleBestGuess(hardMode);
  if (!result) {
    bestGuessOut.textContent = "Load a dictionary first.";
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
});

document.getElementById("solveConn").addEventListener("click", () => {
  if (!Module) return;
  const hardMode = Boolean(connectionsHardEl.checked);
  const result = Module.connectionsSolveDetailed(connWordsEl.value, hardMode);
  try {
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
      ? payload.group_confidence.reduce((a, b) => a + b, 0) /
        payload.group_confidence.length
      : 0;
    connMeta.textContent = `Avg confidence: ${avgConfidence.toFixed(
      3
    )} | Lexical boost: ${payload.lexical_boosted ? "yes" : "no"}`;
    logLine(
      `Connections solved (${hardMode ? "hard" : "normal"}). Avg confidence ${avgConfidence.toFixed(
        3
      )}.`
    );
    if (payload.points && chartReady) {
      renderPoints(payload.points);
    }
  } catch (err) {
    connOut.textContent = "Failed to parse output.";
  }
});

function renderPoints(points) {
  if (!chartEl || !chartReady) {
    logLine("Chart not initialized.");
    return;
  }
  const safePoints = points.filter(
    (point) => Number.isFinite(point.x) && Number.isFinite(point.y)
  );
  if (safePoints.length === 0) {
    logLine("PCA points missing or invalid.");
    return;
  }
  const margins = safePoints.map((point) =>
    Number.isFinite(point.margin) ? point.margin : 0
  );
  const sortedMargins = [...margins].sort((a, b) => a - b);
  const cutoff = sortedMargins[Math.min(2, sortedMargins.length - 1)];

  const xs = safePoints.map((point) => point.x);
  const ys = safePoints.map((point) => point.y);
  const minX = Math.min(...xs);
  const maxX = Math.max(...xs);
  const minY = Math.min(...ys);
  const maxY = Math.max(...ys);
  const scaleX = (x) => {
    const denom = maxX - minX || 1;
    return (
      CHART_PADDING +
      ((x - minX) / denom) * (CHART_WIDTH - CHART_PADDING * 2)
    );
  };
  const scaleY = (y) => {
    const denom = maxY - minY || 1;
    return (
      CHART_HEIGHT -
      CHART_PADDING -
      ((y - minY) / denom) * (CHART_HEIGHT - CHART_PADDING * 2)
    );
  };

  clearSvg();
  drawChartFrame();

  safePoints.forEach((point) => {
    const group = point.group >= 0 && point.group < 4 ? point.group : 0;
    const isHerring = (point.margin ?? 0) <= cutoff;
    const cx = scaleX(point.x);
    const cy = scaleY(point.y);
    const circle = document.createElementNS(
      "http://www.w3.org/2000/svg",
      "circle"
    );
    circle.setAttribute("cx", cx.toFixed(2));
    circle.setAttribute("cy", cy.toFixed(2));
    circle.setAttribute("r", isHerring ? "6" : "4");
    circle.setAttribute("fill", GROUP_COLORS[group]);
    circle.setAttribute("stroke", isHerring ? "#f87171" : "transparent");
    circle.setAttribute("stroke-width", isHerring ? "2" : "0");
    const title = document.createElementNS(
      "http://www.w3.org/2000/svg",
      "title"
    );
    const confidence =
      point.confidence != null ? Number(point.confidence) : 0;
    const margin = point.margin != null ? Number(point.margin) : 0;
    title.textContent = `${point.word} (conf ${confidence.toFixed(
      2
    )}, margin ${margin.toFixed(2)})`;
    circle.appendChild(title);
    chartEl.appendChild(circle);
  });
}

function clearSvg() {
  while (chartEl.firstChild) {
    chartEl.removeChild(chartEl.firstChild);
  }
}

function drawChartFrame() {
  const axis = document.createElementNS("http://www.w3.org/2000/svg", "rect");
  axis.setAttribute("x", CHART_PADDING.toString());
  axis.setAttribute("y", CHART_PADDING.toString());
  axis.setAttribute(
    "width",
    (CHART_WIDTH - CHART_PADDING * 2).toString()
  );
  axis.setAttribute(
    "height",
    (CHART_HEIGHT - CHART_PADDING * 2).toString()
  );
  axis.setAttribute("fill", "none");
  axis.setAttribute("stroke", "#1f2937");
  axis.setAttribute("stroke-width", "1");
  chartEl.appendChild(axis);
}

document.getElementById("speedTestBtn").addEventListener("click", async () => {
  if (!Module) return;
  const count = Math.max(1, Math.min(500, Number(speedCountEl.value) || 100));
  const words = dictEl.value
    .split(/\s+/)
    .map((w) => w.trim().toLowerCase())
    .filter((w) => w.length === 5);
  if (words.length === 0) {
    speedOutEl.textContent = "Load a dictionary first.";
    return;
  }
  const hardMode = Boolean(wordleHardEl.checked);
  logLine(`Speed test started (${count} games, ${hardMode ? "hard" : "normal"}).`);
  const latencies = [];
  let wins = 0;
  for (let i = 0; i < count; i++) {
    const target = words[Math.floor(Math.random() * words.length)];
    Module.wordleReset();
    const start = performance.now();
    let solved = false;
    for (let step = 0; step < 6; step++) {
      const result = Module.wordleBestGuess(hardMode);
      if (!result) break;
      const [guess] = result.split("|");
      const pattern = Module.wordlePattern(guess, target);
      if (!pattern) break;
      Module.wordleApplyFeedback(guess, pattern);
      if (pattern === "22222") {
        solved = true;
        break;
      }
    }
    const elapsed = performance.now() - start;
    latencies.push(elapsed);
    if (solved) wins += 1;
    if ((i + 1) % 20 === 0) {
      await new Promise((r) => setTimeout(r, 0));
    }
  }
  latencies.sort((a, b) => a - b);
  const p = (pct) =>
    latencies[
      Math.min(latencies.length - 1, Math.floor((pct / 100) * latencies.length))
    ] || 0;
  const p50 = p(50).toFixed(2);
  const p90 = p(90).toFixed(2);
  const p99 = p(99).toFixed(2);
  const winRate = ((wins / count) * 100).toFixed(1);
  speedOutEl.textContent = `Win rate: ${winRate}% | P50: ${p50}ms | P90: ${p90}ms | P99: ${p99}ms`;
  logLine(`Speed test done. Win rate ${winRate}%, P99 ${p99}ms.`);
});
