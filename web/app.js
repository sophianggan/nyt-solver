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
let connChart = null;
let chartReady = false;

const GROUP_COLORS = ["#fbbf24", "#22c55e", "#3b82f6", "#a855f7"];

function logLine(message) {
  const time = new Date().toLocaleTimeString();
  logEl.textContent = `[${time}] ${message}\n` + logEl.textContent;
}

const wasmStart = performance.now();
initChart();

createModule()
  .then((mod) => {
    Module = mod;
    const elapsed = ((performance.now() - wasmStart) / 1000).toFixed(2);
    statusEl.textContent = `WASM ready (${elapsed}s).`;
    initChart();
  })
  .catch((err) => {
    statusEl.textContent = "WASM failed to load. Check console/network.";
    logLine(`WASM load failed: ${err}`);
  });

function initChart() {
  if (chartReady || !chartEl || !window.Chart) return;
  chartReady = true;
  connChart = new Chart(chartEl, {
    type: "scatter",
    data: {
      datasets: [
        { label: "Group 1", data: [], backgroundColor: GROUP_COLORS[0] },
        { label: "Group 2", data: [], backgroundColor: GROUP_COLORS[1] },
        { label: "Group 3", data: [], backgroundColor: GROUP_COLORS[2] },
        { label: "Group 4", data: [], backgroundColor: GROUP_COLORS[3] },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 800, easing: "easeOutQuart" },
      plugins: {
        legend: { labels: { color: "#94a3b8" } },
        tooltip: {
          callbacks: {
            label: (ctx) =>
              `${ctx.raw.label} (conf ${ctx.raw.confidence?.toFixed(2) ?? "0.00"}, margin ${ctx.raw.margin?.toFixed(2) ?? "0.00"})`,
          },
        },
      },
      scales: {
        x: { ticks: { color: "#94a3b8" }, grid: { color: "#1f2937" } },
        y: { ticks: { color: "#94a3b8" }, grid: { color: "#1f2937" } },
      },
    },
  });
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
  logLine(`Applied feedback: ${guess.toUpperCase()} ${pattern}, remaining ${remaining}.`);
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
      ? (payload.group_confidence.reduce((a, b) => a + b, 0) /
          payload.group_confidence.length)
      : 0;
    connMeta.textContent = `Avg confidence: ${avgConfidence.toFixed(
      3
    )} | Lexical boost: ${payload.lexical_boosted ? "yes" : "no"}`;
    logLine(
      `Connections solved (${hardMode ? "hard" : "normal"}). Avg confidence ${avgConfidence.toFixed(
        3
      )}.`
    );
    if (payload.points && connChart) {
      renderPoints(payload.points);
    }
  } catch (err) {
    connOut.textContent = "Failed to parse output.";
  }
});

function renderPoints(points) {
  const grouped = [[], [], [], []];
  const randomOffset = () => (Math.random() - 0.5) * 4;
  const seeded = points.map((point) => ({
    ...point,
    x: point.x + randomOffset(),
    y: point.y + randomOffset(),
  }));
  connChart.data.datasets.forEach((dataset, idx) => {
    dataset.data = seeded.filter((p) => (p.group ?? 0) === idx).map((p) => ({
      x: p.x,
      y: p.y,
      label: p.word,
      margin: p.margin ?? 0,
      confidence: p.confidence ?? 0,
    }));
    dataset.pointRadius = dataset.data.map(() => 4);
    dataset.pointBorderWidth = dataset.data.map(() => 0);
  });
  connChart.update();
  const margins = points.map((point) => point.margin ?? 0);
  const sortedMargins = [...margins].sort((a, b) => a - b);
  const cutoff = sortedMargins[Math.min(2, sortedMargins.length - 1)];
  for (const point of points) {
    const group = point.group >= 0 && point.group < 4 ? point.group : 0;
    grouped[group].push({
      x: point.x,
      y: point.y,
      label: point.word,
      margin: point.margin ?? 0,
      confidence: point.confidence ?? 0,
    });
  }
  grouped.forEach((data, idx) => {
    connChart.data.datasets[idx].data = data;
    connChart.data.datasets[idx].pointRadius = data.map((pt) =>
      pt.margin <= cutoff ? 7 : 4
    );
    connChart.data.datasets[idx].pointBorderColor = data.map((pt) =>
      pt.margin <= cutoff ? "#f87171" : "transparent"
    );
    connChart.data.datasets[idx].pointBorderWidth = data.map((pt) =>
      pt.margin <= cutoff ? 2 : 0
    );
  });
  setTimeout(() => {
    connChart.update();
  }, 120);
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
    latencies[Math.min(latencies.length - 1, Math.floor((pct / 100) * latencies.length))] || 0;
  const p50 = p(50).toFixed(2);
  const p90 = p(90).toFixed(2);
  const p99 = p(99).toFixed(2);
  const winRate = ((wins / count) * 100).toFixed(1);
  speedOutEl.textContent = `Win rate: ${winRate}% | P50: ${p50}ms | P90: ${p90}ms | P99: ${p99}ms`;
  logLine(`Speed test done. Win rate ${winRate}%, P99 ${p99}ms.`);
});
