# Aletheia: Information-Theoretic Puzzle Optimization

## Abstract

Aletheia is a high-performance solver for NYT Wordle and Connections. It pairs
Shannon entropy with SIMD acceleration and fixed-block memory pooling to
optimize guess selection, and uses PCA-based clustering to solve Connections.
This paper describes the mathematical framing and the engineering choices
behind the system.

## Wordle as Information Theory

Each guess partitions the candidate set by feedback pattern. We treat the
feedback distribution as a probability mass function and maximize Shannon
entropy:

```
H = -\sum_i p_i \log_2(p_i)
```

For a specific pattern with probability `p`, the information gain is:

```
I = -\log_2(p)
```

By selecting the guess with the highest expected entropy, Aletheia maximizes
expected information gain per turn.

## SIMD + Memory Pooling

The solver performs millions of candidate checks. To reduce latency:

- SIMD prefilters (Google Highway) accelerate pattern checks.
- A fixed-block pool stores words in contiguous, aligned memory.
- Candidate buffers are reused between turns to avoid heap churn.

These changes minimize cache misses and allocation overhead in hot loops.

## Connections via Semantic Clustering

Connections words are embedded as vectors, and cosine similarity builds the
adjacency matrix:

```
cos(theta) = (a · b) / (||a|| * ||b||)
```

PCA projects embeddings into 2D for visualization and red-herring analysis.
The solver searches 4-clique partitions that maximize total intra-group
similarity.

## Validation

The Python benchmarking suite evaluates win rate, average guesses, and P99
latency across official Wordle solutions. A/B runs compare baseline versus
optimized builds, documenting performance deltas in `BENCHMARKS.md`.

## Conclusion

Aletheia demonstrates how mathematical objectives (entropy maximization and
PCA clustering) can be translated into production-grade C++ with careful
performance engineering.
