#!/usr/bin/env python3
import argparse
import datetime as dt
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import urllib.request
import ssl
from typing import List, Optional

DEFAULT_SOLUTIONS_URL = "https://raw.githubusercontent.com/tabatkins/wordle-list/main/words"
DEFAULT_SOLUTIONS_CACHE = "data/wordle_solutions.txt"
WORDLE_START_DATE = dt.date(2021, 6, 19)
MIN_WORDLIST_SIZE = 1000

STEP_RE = re.compile(
    r"Step\s+(?P<step>\d+):\s+guess=(?P<guess>[a-z]+)\s+pattern=(?P<pattern>\d{5})"
    r"\s+entropy=(?P<entropy>[-0-9.]+)\s+bits=(?P<bits>[-0-9.]+)"
)
LATENCY_RE = re.compile(r"Total latency:\s+(?P<micros>\d+)us")


def filter_word_list(lines: List[str]) -> List[str]:
    words = []
    seen = set()
    for line in lines:
        word = line.strip().lower()
        if len(word) != 5 or not word.isalpha():
            continue
        if word in seen:
            continue
        seen.add(word)
        words.append(word)
    return words


def fetch_solutions(url: str, insecure: bool) -> List[str]:
    context = None
    if insecure:
        context = ssl._create_unverified_context()
    with urllib.request.urlopen(url, timeout=15, context=context) as response:
        data = response.read().decode("utf-8")
    return filter_word_list(data.splitlines())


def fetch_solutions_via_curl(url: str, insecure: bool) -> List[str]:
    curl_path = shutil.which("curl")
    if not curl_path:
        return []
    cmd = [curl_path, "-L", "--silent", "--show-error"]
    if insecure:
        cmd.append("-k")
    cmd.append(url)
    result = subprocess.run(cmd, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "curl failed")
    return filter_word_list(result.stdout.splitlines())


def fetch_solutions_via_wget(url: str, insecure: bool) -> List[str]:
    wget_path = shutil.which("wget")
    if not wget_path:
        return []
    cmd = [wget_path, "-q", "-O", "-"]
    if insecure:
        cmd.append("--no-check-certificate")
    cmd.append(url)
    result = subprocess.run(cmd, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "wget failed")
    return filter_word_list(result.stdout.splitlines())


def read_solution_file(path: str) -> List[str]:
    with open(path, "r", encoding="utf-8") as handle:
        return filter_word_list(handle)


def write_solution_file(path: str, solutions: List[str]) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(solutions) + "\n")


def load_solutions(
    path: Optional[str], url: str, cache_path: str, insecure: bool
) -> List[str]:
    errors: List[str] = []

    if path:
        try:
            words = read_solution_file(path)
            if words:
                return words
            errors.append(f"solutions file empty: {path}")
        except Exception as exc:
            sys.stderr.write(f"Failed to read solutions file {path}: {exc}\n")

    def record_error(label: str, exc: Exception) -> None:
        errors.append(f"{label}: {exc}")

    def try_fetch(insecure_flag: bool) -> List[str]:
        try:
            return fetch_solutions(url, insecure_flag)
        except Exception as exc:
            record_error("urllib", exc)
            return []

    solutions = try_fetch(insecure)
    if not solutions and not insecure:
        solutions = try_fetch(True)

    if not solutions:
        try:
            solutions = fetch_solutions_via_curl(url, insecure)
        except Exception as exc:
            record_error("curl", exc)
            solutions = []
    if not solutions and not insecure:
        try:
            solutions = fetch_solutions_via_curl(url, True)
        except Exception as exc:
            record_error("curl-insecure", exc)
            solutions = []

    if not solutions:
        try:
            solutions = fetch_solutions_via_wget(url, insecure)
        except Exception as exc:
            record_error("wget", exc)
            solutions = []
    if not solutions and not insecure:
        try:
            solutions = fetch_solutions_via_wget(url, True)
        except Exception as exc:
            record_error("wget-insecure", exc)
            solutions = []

    if solutions and len(solutions) >= MIN_WORDLIST_SIZE:
        write_solution_file(cache_path, solutions)
        return solutions
    if solutions:
        errors.append(
            f"fetched list too small ({len(solutions)} entries): {url}"
        )

    if cache_path and os.path.exists(cache_path):
        try:
            words = read_solution_file(cache_path)
            if len(words) >= MIN_WORDLIST_SIZE:
                return words
            errors.append(
                f"cached list too small ({len(words)} entries): {cache_path}"
            )
        except Exception as exc:
            sys.stderr.write(f"Failed to read cached solutions: {exc}\n")

    if errors:
        sys.stderr.write("Failed to fetch solutions. Attempts:\n")
        for error in errors:
            sys.stderr.write(f"  - {error}\n")

    return []


def select_recent_solutions(solutions: List[str], count: int) -> List[str]:
    if not solutions:
        return []
    today = dt.date.today()
    days_since_start = (today - WORDLE_START_DATE).days
    if days_since_start < 0:
        days_since_start = 0
    end_index = min(days_since_start, len(solutions) - 1)
    start_index = max(0, end_index - count + 1)
    return solutions[start_index : end_index + 1]


def percentile(values: List[float], pct: float) -> float:
    if not values:
        return 0.0
    if pct <= 0:
        return min(values)
    if pct >= 100:
        return max(values)
    ordered = sorted(values)
    rank = math.ceil((pct / 100.0) * len(ordered)) - 1
    rank = max(0, min(rank, len(ordered) - 1))
    return ordered[rank]


def run_solver(binary: str, dict_path: str, target: str, max_steps: int) -> dict:
    cmd = [
        binary,
        "--wordle-dict",
        dict_path,
        "--wordle-target",
        target,
        "--wordle-max-steps",
        str(max_steps),
    ]
    proc = subprocess.run(
        cmd,
        check=False,
        capture_output=True,
        text=True,
    )
    output = proc.stdout + proc.stderr
    steps = []
    for match in STEP_RE.finditer(output):
        steps.append(
            {
                "step": int(match.group("step")),
                "guess": match.group("guess"),
                "pattern": match.group("pattern"),
                "entropy": float(match.group("entropy")),
                "bits": float(match.group("bits")),
            }
        )
    latency_match = LATENCY_RE.search(output)
    latency_us = int(latency_match.group("micros")) if latency_match else 0
    solved = bool(steps) and steps[-1]["pattern"] == "22222"
    total_bits = sum(step["bits"] for step in steps)
    guesses = len(steps)
    per_guess_us = latency_us / guesses if guesses else 0.0

    return {
        "target": target,
        "returncode": proc.returncode,
        "solved": solved,
        "guesses": guesses,
        "total_bits": total_bits,
        "latency_us": latency_us,
        "per_guess_us": per_guess_us,
        "steps": steps,
        "raw_output": output,
    }


def render_histogram(values: list[int], out_path: str) -> bool:
    try:
        import matplotlib.pyplot as plt
    except Exception as exc:
        sys.stderr.write(f"matplotlib not available, skipping plot: {exc}\n")
        return False

    if not values:
        sys.stderr.write("No values for histogram, skipping plot.\n")
        return False

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    plt.figure(figsize=(7, 4))
    bins = list(range(1, max(values) + 2))
    plt.hist(values, bins=bins, align="left", rwidth=0.85, color="#4c78a8")
    plt.xticks(range(1, max(values) + 1))
    plt.xlabel("Guesses to Solve")
    plt.ylabel("Count")
    plt.title("Wordle Solver Guess Distribution")
    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    plt.close()
    return True


def write_json_report(path: str, payload: dict) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Benchmark the aletheia Wordle solver over recent solutions."
    )
    parser.add_argument(
        "--binary",
        default="./build/aletheia",
        help="Path to the aletheia binary.",
    )
    parser.add_argument(
        "--dict",
        default=None,
        help="Path to the 5-letter dictionary file (defaults to cached list or wordle.txt).",
    )
    parser.add_argument(
        "--count",
        type=int,
        default=30,
        help="Number of recent solutions to benchmark (30-100 recommended).",
    )
    parser.add_argument(
        "--max-steps",
        type=int,
        default=6,
        help="Max guesses per word (Wordle default is 6).",
    )
    parser.add_argument(
        "--solutions-file",
        default=None,
        help="Optional local file containing solution words (one per line).",
    )
    parser.add_argument(
        "--solutions-url",
        default=DEFAULT_SOLUTIONS_URL,
        help="URL with the official Wordle solutions list.",
    )
    parser.add_argument(
        "--insecure",
        action="store_true",
        help="Disable SSL certificate verification when fetching solutions.",
    )
    parser.add_argument(
        "--plot",
        default="reports/wordle_guess_hist.png",
        help="Output path for the histogram image.",
    )
    parser.add_argument(
        "--json",
        default="reports/wordle_benchmark.json",
        help="Output path for the JSON report.",
    )
    parser.add_argument(
        "--progress-every",
        type=int,
        default=5,
        help="Print progress every N words (0 disables).",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.count <= 0:
        sys.stderr.write("Count must be positive.\n")
        return 2

    solutions = load_solutions(
        args.solutions_file,
        args.solutions_url,
        DEFAULT_SOLUTIONS_CACHE,
        args.insecure,
    )
    if not solutions:
        sys.stderr.write(
            "No solutions available. Provide --solutions-file, place "
            f"a list at {DEFAULT_SOLUTIONS_CACHE}, or retry with --insecure.\n"
        )
        return 2

    recent = select_recent_solutions(solutions, args.count)
    if not recent:
        sys.stderr.write("No recent solutions selected.\n")
        return 2

    dict_path = args.dict
    if not dict_path:
        if os.path.exists(DEFAULT_SOLUTIONS_CACHE):
            dict_path = DEFAULT_SOLUTIONS_CACHE
        else:
            dict_path = "wordle.txt"

    if not os.path.exists(dict_path):
        sys.stderr.write(f"Dictionary not found: {dict_path}\n")
        return 2

    dictionary_words = read_solution_file(dict_path)
    if not dictionary_words:
        sys.stderr.write(f"Dictionary is empty: {dict_path}\n")
        return 2

    dictionary_set = set(dictionary_words)
    missing_targets = [word for word in recent if word not in dictionary_set]
    if missing_targets:
        if (
            args.dict is None
            and dict_path != DEFAULT_SOLUTIONS_CACHE
            and os.path.exists(DEFAULT_SOLUTIONS_CACHE)
        ):
            dict_path = DEFAULT_SOLUTIONS_CACHE
            dictionary_words = read_solution_file(dict_path)
            dictionary_set = set(dictionary_words)
            missing_targets = [
                word for word in recent if word not in dictionary_set
            ]

    if missing_targets:
        sample = ", ".join(missing_targets[:5])
        sys.stderr.write(
            "Dictionary does not contain all targets "
            f"({len(missing_targets)} missing). Example(s): {sample}\n"
        )
        sys.stderr.write(
            "Provide a dictionary that includes the target list, or pass "
            f"--dict {DEFAULT_SOLUTIONS_CACHE}.\n"
        )
        return 2

    print(f"Using dictionary: {dict_path} ({len(dictionary_words)} words)")

    results = []
    total = len(recent)
    for idx, word in enumerate(recent, start=1):
        if args.progress_every and (
            idx == 1
            or idx == total
            or idx % args.progress_every == 0
        ):
            print(f"Running {idx}/{total}: {word}", flush=True)
        result = run_solver(args.binary, dict_path, word, args.max_steps)
        if result["returncode"] != 0:
            sys.stderr.write(f"Solver failed for {word}.\n")
        results.append(result)

    solved_results = [r for r in results if r["solved"]]
    guess_counts = [r["guesses"] for r in solved_results]
    latency_ms = [r["latency_us"] / 1000.0 for r in solved_results]
    per_guess_ms = [r["per_guess_us"] / 1000.0 for r in solved_results]
    total_bits = [r["total_bits"] for r in solved_results]

    win_rate = len(solved_results) / len(results) if results else 0.0
    avg_guesses = statistics.mean(guess_counts) if guess_counts else 0.0
    max_guesses = max(guess_counts) if guess_counts else 0
    hardest_words = [
        r["target"] for r in solved_results if r["guesses"] == max_guesses
    ]
    p99_latency = percentile(latency_ms, 99)
    p99_guess_latency = percentile(per_guess_ms, 99)
    avg_bits = statistics.mean(total_bits) if total_bits else 0.0
    sum_bits = sum(total_bits)
    avg_guess_latency = statistics.mean(per_guess_ms) if per_guess_ms else 0.0

    report = {
        "count": len(results),
        "max_steps": args.max_steps,
        "win_rate": win_rate,
        "average_guesses": avg_guesses,
        "max_guesses": max_guesses,
        "hardest_words": hardest_words,
        "p99_latency_ms": p99_latency,
        "p99_guess_latency_ms": p99_guess_latency,
        "average_total_bits": avg_bits,
        "total_bits": sum_bits,
        "average_guess_latency_ms": avg_guess_latency,
        "results": [
            {
                "target": r["target"],
                "solved": r["solved"],
                "guesses": r["guesses"],
                "total_bits": r["total_bits"],
                "latency_ms": r["latency_us"] / 1000.0,
                "per_guess_ms": r["per_guess_us"] / 1000.0,
            }
            for r in results
        ],
    }

    print("Wordle Benchmark Report")
    print(f"Words evaluated: {len(results)}")
    print(f"Win rate: {win_rate:.1%}")
    print(f"Average guesses: {avg_guesses:.2f}")
    print(f"Max guesses: {max_guesses}")
    print(f"Hardest words: {', '.join(hardest_words) if hardest_words else 'n/a'}")
    print(f"P99 latency (ms): {p99_latency:.3f}")
    print(f"P99 latency per guess (ms): {p99_guess_latency:.3f}")
    print(f"Average guess latency (ms): {avg_guess_latency:.3f}")
    print(f"Average total bits: {avg_bits:.3f}")
    print(f"Total bits: {sum_bits:.3f}")

    if args.json:
        write_json_report(args.json, report)
        print(f"JSON report: {args.json}")

    if args.plot:
        plot_saved = render_histogram(guess_counts, args.plot)
        if plot_saved:
            print(f"Histogram saved: {args.plot}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
