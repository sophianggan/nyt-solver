import createModule from "./aletheia_wasm.js";

const statusEl = document.getElementById("status");
const dictEl = document.getElementById("dict");
const dictStatusEl = document.getElementById("dictStatus");
const guessEl = document.getElementById("guess");
const targetEl = document.getElementById("target");
const patternOut = document.getElementById("patternOut");
const patternInputEl = document.getElementById("patternInput");
const bestGuessOut = document.getElementById("bestGuessOut");
const connWordsEl = document.getElementById("connWords");
const connOut = document.getElementById("connOut");
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

const GROUP_COLORS = ["#fbbf24", "#22c55e", "#3b82f6", "#a855f7"];

function logLine(message) {
  const time = new Date().toLocaleTimeString();
  logEl.textContent = `[${time}] ${message}\n` + logEl.textContent;
}

createModule().then((mod) => {
  Module = mod;
  statusEl.textContent = "WASM ready.";
  initChart();
});

function initChart() {
  if (!chartEl) return;
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
            label: (ctx) => `${ctx.raw.label} (${ctx.raw.x.toFixed(2)}, ${ctx.raw.y.toFixed(2)})`,
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
    logLine(
      `Connections solved (${hardMode ? "hard" : "normal"}). PCA points rendered.`
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
  for (const point of points) {
    const group = point.group >= 0 && point.group < 4 ? point.group : 0;
    grouped[group].push({
      x: point.x,
      y: point.y,
      label: point.word,
    });
  }
  grouped.forEach((data, idx) => {
    connChart.data.datasets[idx].data = data;
  });
  connChart.update();
}
