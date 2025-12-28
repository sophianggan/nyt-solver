#include "Solver.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include <emscripten/bind.h>

namespace {
aletheia::WordleSolver g_wordle;
bool g_loaded = false;
std::vector<size_t> g_remaining;

std::string ToLowerAscii(std::string input) {
  for (char& c : input) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return input;
}

std::vector<std::string> SplitWordsText(const std::string& text) {
  std::istringstream iss(text);
  std::vector<std::string> words;
  std::string token;
  while (iss >> token) {
    words.push_back(ToLowerAscii(token));
  }
  return words;
}

Eigen::VectorXd FallbackEmbedding(const std::string& word, int dims) {
  Eigen::VectorXd vec(dims);
  uint64_t hash = 1469598103934665603ULL;
  for (char c : word) {
    hash ^= static_cast<uint64_t>(c);
    hash *= 1099511628211ULL;
  }
  for (int i = 0; i < dims; ++i) {
    double value = static_cast<double>((hash >> (i * 3)) & 0xFFFF);
    vec[i] = std::sin(value * 0.001 + static_cast<double>(i));
  }
  return vec;
}

std::string JsonEscape(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

bool IsPatternValid(const std::string& pattern) {
  if (pattern.size() != 5) {
    return false;
  }
  for (char c : pattern) {
    if (c < '0' || c > '2') {
      return false;
    }
  }
  return true;
}

struct PcaResult {
  Eigen::MatrixXd projected;
};

PcaResult ComputePcaProjection(const std::vector<Eigen::VectorXd>& embeddings,
                               int dims) {
  PcaResult result;
  const int n = static_cast<int>(embeddings.size());
  if (n == 0) {
    return result;
  }
  const int feature_dims = static_cast<int>(embeddings.front().size());
  if (feature_dims == 0) {
    return result;
  }
  int k = dims <= 0 ? 2 : dims;
  if (k > feature_dims) {
    k = feature_dims;
  }

  Eigen::MatrixXd X(n, feature_dims);
  for (int i = 0; i < n; ++i) {
    X.row(i) = embeddings[i].transpose();
  }

  Eigen::VectorXd mean = X.colwise().mean();
  Eigen::MatrixXd centered = X.rowwise() - mean.transpose();
  Eigen::MatrixXd cov =
      (centered.transpose() * centered) / std::max(1, n - 1);

  Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(cov);
  if (solver.info() != Eigen::Success) {
    return result;
  }

  Eigen::VectorXd evals = solver.eigenvalues();
  Eigen::MatrixXd evecs = solver.eigenvectors();
  std::vector<int> indices(feature_dims);
  std::iota(indices.begin(), indices.end(), 0);
  std::sort(indices.begin(), indices.end(), [&](int a, int b) {
    return evals[a] > evals[b];
  });

  Eigen::MatrixXd components(feature_dims, k);
  for (int i = 0; i < k; ++i) {
    components.col(i) = evecs.col(indices[i]);
  }
  result.projected = centered * components;
  return result;
}
}  // namespace

void WordleReset();

void LoadWordleDict(const std::string& dict_text) {
  std::vector<std::string> words = SplitWordsText(dict_text);
  g_wordle.SetWordList(words);
  g_loaded = !g_wordle.words().empty();
  WordleReset();
}

void WordleReset() {
  g_remaining.clear();
  if (g_loaded) {
    g_remaining.resize(g_wordle.words().size());
    std::iota(g_remaining.begin(), g_remaining.end(), 0);
  }
}

int WordleRemainingCount() {
  return static_cast<int>(g_remaining.size());
}

bool WordleIsCandidate(const std::string& guess) {
  if (!g_loaded) {
    return false;
  }
  std::string g = aletheia::WordleSolver::NormalizeWord(guess);
  if (!aletheia::WordleSolver::IsValidWord(g)) {
    return false;
  }
  for (size_t idx : g_remaining) {
    if (g_wordle.words()[idx].text == g) {
      return true;
    }
  }
  return false;
}

int WordleApplyFeedback(const std::string& guess, const std::string& pattern) {
  if (!g_loaded || g_remaining.empty()) {
    return -1;
  }
  std::string g = aletheia::WordleSolver::NormalizeWord(guess);
  if (!aletheia::WordleSolver::IsValidWord(g) ||
      !IsPatternValid(pattern)) {
    return -1;
  }
  std::vector<size_t> next;
  next.reserve(g_remaining.size());
  aletheia::WordleSolver::FilterCandidates(
      g_wordle.words(), g_remaining, g, pattern, &next);
  g_remaining.swap(next);
  return static_cast<int>(g_remaining.size());
}

std::string WordleBestGuess(bool hard_mode) {
  if (!g_loaded) {
    return "";
  }
  std::vector<size_t> all_indices(g_wordle.words().size());
  std::iota(all_indices.begin(), all_indices.end(), 0);
  const std::vector<size_t>& targets =
      g_remaining.empty() ? all_indices : g_remaining;
  const std::vector<size_t>& candidates =
      hard_mode ? targets : all_indices;
  double entropy = 0.0;
  std::string guess = g_wordle.BestGuess(candidates, targets, &entropy);
  std::ostringstream out;
  out << guess << "|" << entropy;
  return out.str();
}

std::string WordlePattern(const std::string& guess, const std::string& target) {
  std::string g = aletheia::WordleSolver::NormalizeWord(guess);
  std::string t = aletheia::WordleSolver::NormalizeWord(target);
  if (!aletheia::WordleSolver::IsValidWord(g) ||
      !aletheia::WordleSolver::IsValidWord(t)) {
    return "";
  }
  auto g_pack = aletheia::WordleSolver::EncodeWord(g);
  auto t_pack = aletheia::WordleSolver::EncodeWord(t);
  int pattern = aletheia::WordleSolver::Pattern(g_pack, t_pack);
  return aletheia::WordleSolver::PatternString(pattern);
}

std::string ConnectionsSolveDetailed(const std::string& words_text,
                                     bool hard_mode);

std::string ConnectionsSolve(const std::string& words_text) {
  return ConnectionsSolveDetailed(words_text, false);
}

std::string ConnectionsSolveDetailed(const std::string& words_text,
                                     bool hard_mode) {
  std::vector<std::string> words = SplitWordsText(words_text);
  if (words.size() != 16) {
    return "{\"error\":\"Expected 16 words\"}";
  }

  std::vector<Eigen::VectorXd> vectors;
  vectors.reserve(words.size());
  const int dims = 64;
  for (const auto& word : words) {
    vectors.push_back(FallbackEmbedding(word, dims));
  }

  aletheia::SimilarityEngine similarity;
  double lexical_weight = hard_mode ? 0.25 : 0.0;
  bool lexical_boosted = false;
  auto build_matrix = [&](double weight) {
    if (weight > 0.0) {
      similarity.BuildMatrixHybrid(vectors, words, weight);
    } else {
      similarity.BuildMatrix(vectors);
    }
  };
  build_matrix(lexical_weight);

  auto solve_groups = [&]() {
    aletheia::ConnectionsSolver solver(similarity.matrix());
    return solver.SolveBestPartition();
  };

  std::vector<uint16_t> groups = solve_groups();

  auto build_group_indices = [&](const std::vector<uint16_t>& masks) {
    std::vector<std::vector<int>> indices;
    indices.reserve(masks.size());
    for (size_t g = 0; g < masks.size(); ++g) {
      std::vector<int> group;
      for (size_t i = 0; i < words.size(); ++i) {
        if (masks[g] & (1U << i)) {
          group.push_back(static_cast<int>(i));
        }
      }
      indices.push_back(std::move(group));
    }
    return indices;
  };

  auto cluster_confidence = [&](const std::vector<int>& indices) {
    if (indices.size() < 2) {
      return 0.0;
    }
    const int n = static_cast<int>(indices.size());
    const int dims = static_cast<int>(vectors[indices[0]].size());
    Eigen::MatrixXd X(n, dims);
    for (int i = 0; i < n; ++i) {
      X.row(i) = vectors[indices[i]].transpose();
    }
    Eigen::VectorXd mean = X.colwise().mean();
    Eigen::MatrixXd centered = X.rowwise() - mean.transpose();
    Eigen::MatrixXd cov =
        (centered.transpose() * centered) / std::max(1, n - 1);
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(cov);
    if (solver.info() != Eigen::Success) {
      return 0.0;
    }
    Eigen::VectorXd evals = solver.eigenvalues();
    double sum = evals.sum();
    if (sum <= 0.0) {
      return 0.0;
    }
    double max_eval = evals.maxCoeff();
    return max_eval / sum;
  };

  std::vector<std::vector<int>> group_indices = build_group_indices(groups);
  std::vector<double> group_confidence;
  group_confidence.reserve(group_indices.size());
  double avg_confidence = 0.0;
  for (const auto& indices : group_indices) {
    double conf = cluster_confidence(indices);
    group_confidence.push_back(conf);
    avg_confidence += conf;
  }
  if (!group_confidence.empty()) {
    avg_confidence /= static_cast<double>(group_confidence.size());
  }

  if (hard_mode && avg_confidence < 0.25 && lexical_weight < 0.5) {
    lexical_weight = 0.5;
    lexical_boosted = true;
    build_matrix(lexical_weight);
    groups = solve_groups();
    group_indices = build_group_indices(groups);
    group_confidence.clear();
    avg_confidence = 0.0;
    for (const auto& indices : group_indices) {
      double conf = cluster_confidence(indices);
      group_confidence.push_back(conf);
      avg_confidence += conf;
    }
    if (!group_confidence.empty()) {
      avg_confidence /= static_cast<double>(group_confidence.size());
    }
  }

  PcaResult pca = ComputePcaProjection(vectors, 2);

  std::vector<int> group_of(words.size(), -1);
  for (size_t g = 0; g < groups.size(); ++g) {
    for (size_t i = 0; i < words.size(); ++i) {
      if (groups[g] & (1U << i)) {
        group_of[i] = static_cast<int>(g);
      }
    }
  }

  std::ostringstream out;
  out << "{\"groups\":[";
  for (size_t g = 0; g < groups.size(); ++g) {
    if (g > 0) {
      out << ",";
    }
    out << "[";
    bool first = true;
    for (size_t i = 0; i < words.size(); ++i) {
      if (groups[g] & (1U << i)) {
        if (!first) {
          out << ",";
        }
        out << "\"" << JsonEscape(words[i]) << "\"";
        first = false;
      }
    }
    out << "]";
  }
  out << "],\"group_confidence\":[";
  for (size_t i = 0; i < group_confidence.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << group_confidence[i];
  }
  out << "],\"lexical_boosted\":" << (lexical_boosted ? "true" : "false");
  out << ",\"points\":[";

  std::vector<Eigen::Vector2d> centroids(group_indices.size(),
                                         Eigen::Vector2d::Zero());
  if (pca.projected.rows() == static_cast<int>(words.size()) &&
      pca.projected.cols() >= 2) {
    for (size_t g = 0; g < group_indices.size(); ++g) {
      if (group_indices[g].empty()) {
        continue;
      }
      Eigen::Vector2d sum(0.0, 0.0);
      for (int idx : group_indices[g]) {
        sum[0] += pca.projected(idx, 0);
        sum[1] += pca.projected(idx, 1);
      }
      centroids[g] = sum / static_cast<double>(group_indices[g].size());
    }
  }

  for (size_t i = 0; i < words.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    double x = 0.0;
    double y = 0.0;
    double margin = 0.0;
    if (pca.projected.rows() == static_cast<int>(words.size()) &&
        pca.projected.cols() >= 2) {
      x = pca.projected(static_cast<int>(i), 0);
      y = pca.projected(static_cast<int>(i), 1);
      if (!centroids.empty()) {
        int own = group_of[i];
        double best = std::numeric_limits<double>::infinity();
        double second = std::numeric_limits<double>::infinity();
        for (size_t g = 0; g < centroids.size(); ++g) {
          double dx = x - centroids[g][0];
          double dy = y - centroids[g][1];
          double dist = std::sqrt(dx * dx + dy * dy);
          if (static_cast<int>(g) == own) {
            best = dist;
          } else if (dist < second) {
            second = dist;
          }
        }
        if (std::isfinite(best) && std::isfinite(second)) {
          margin = second - best;
        }
      }
    }
    double confidence = 0.0;
    if (group_of[i] >= 0 &&
        static_cast<size_t>(group_of[i]) < group_confidence.size()) {
      confidence = group_confidence[group_of[i]];
    }
    out << "{\"word\":\"" << JsonEscape(words[i]) << "\""
        << ",\"x\":" << x << ",\"y\":" << y
        << ",\"group\":" << group_of[i]
        << ",\"margin\":" << margin
        << ",\"confidence\":" << confidence << "}";
  }
  out << "]}";
  return out.str();
}

EMSCRIPTEN_BINDINGS(aletheia_wasm) {
  emscripten::function("loadWordleDict", &LoadWordleDict);
  emscripten::function("wordleReset", &WordleReset);
  emscripten::function("wordleRemainingCount", &WordleRemainingCount);
  emscripten::function("wordleIsCandidate", &WordleIsCandidate);
  emscripten::function("wordleApplyFeedback", &WordleApplyFeedback);
  emscripten::function("wordleBestGuess", &WordleBestGuess);
  emscripten::function("wordlePattern", &WordlePattern);
  emscripten::function("connectionsSolve", &ConnectionsSolve);
  emscripten::function("connectionsSolveDetailed", &ConnectionsSolveDetailed);
}
