import createModule from "./aletheia_wasm.js";

const statusEl = document.getElementById("status");
const dictEl = document.getElementById("dict");
const dictStatusEl = document.getElementById("dictStatus");
const guessEl = document.getElementById("guess");
const targetEl = document.getElementById("target");
const patternOut = document.getElementById("patternOut");
const bestGuessOut = document.getElementById("bestGuessOut");
const connWordsEl = document.getElementById("connWords");
const connOut = document.getElementById("connOut");

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

createModule().then((mod) => {
  Module = mod;
  statusEl.textContent = "WASM ready.";
});

document.getElementById("loadDict").addEventListener("click", () => {
  if (!Module) return;
  Module.loadWordleDict(dictEl.value);
  dictStatusEl.textContent = "Dictionary loaded.";
});

document.getElementById("patternBtn").addEventListener("click", () => {
  if (!Module) return;
  const guess = guessEl.value.trim();
  const target = targetEl.value.trim();
  const pattern = Module.wordlePattern(guess, target);
  patternOut.textContent = pattern ? `Pattern: ${pattern}` : "Invalid input.";
});

document.getElementById("bestGuessBtn").addEventListener("click", () => {
  if (!Module) return;
  const result = Module.wordleBestGuess();
  if (!result) {
    bestGuessOut.textContent = "Load a dictionary first.";
    return;
  }
  const [guess, entropy] = result.split("|");
  bestGuessOut.textContent = `Best guess: ${guess} (entropy ${Number(
    entropy
  ).toFixed(4)})`;
});

document.getElementById("solveConn").addEventListener("click", () => {
  if (!Module) return;
  const result = Module.connectionsSolve(connWordsEl.value);
  try {
    const payload = JSON.parse(result);
    if (payload.error) {
      connOut.textContent = payload.error;
      return;
    }
    const lines = payload.groups.map((group, i) => {
      return `Group ${i + 1}: ${group.join(", ")}`;
    });
    connOut.textContent = lines.join("\n");
  } catch (err) {
    connOut.textContent = "Failed to parse output.";
  }
});
