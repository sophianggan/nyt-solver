# Theory Notes

This engine relies on two core math ideas: information theory for Wordle
and vector similarity for Connections.

## Shannon Entropy (Wordle)

For a set of outcomes with probabilities `p_i`, Shannon entropy is:

```
H = -\sum_i p_i \log_2(p_i)
```

For a specific feedback pattern with probability `p`, the information gain is:

```
I = -\log_2(p)
```

In practice, `p` is estimated as:

```
p = (remaining_after) / (remaining_before)
```

## Cosine Similarity (Connections)

Given embedding vectors `a` and `b`, cosine similarity is:

```
cos(theta) = (a · b) / (||a|| * ||b||)
```

This yields the weighted similarity matrix used by the Connections solver.

## PCA (Connections)

We mean-center the word vectors, compute the covariance matrix, and use
eigenvectors of the covariance matrix to project into lower dimensions.
The top eigenvectors capture the largest variance and help visualize
clusters and red herrings.
