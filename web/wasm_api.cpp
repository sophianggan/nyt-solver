#include "Solver.hpp"

#include <algorithm>
#include <cctype>
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
  if (hard_mode) {
    similarity.BuildMatrixHybrid(vectors, words, 0.35);
  } else {
    similarity.BuildMatrix(vectors);
  }
  aletheia::ConnectionsSolver solver(similarity.matrix());
  std::vector<uint16_t> groups = solver.SolveBestPartition();
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
  out << "],\"points\":[";
  for (size_t i = 0; i < words.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    double x = 0.0;
    double y = 0.0;
    if (pca.projected.rows() == static_cast<int>(words.size()) &&
        pca.projected.cols() >= 2) {
      x = pca.projected(static_cast<int>(i), 0);
      y = pca.projected(static_cast<int>(i), 1);
    }
    out << "{\"word\":\"" << JsonEscape(words[i]) << "\""
        << ",\"x\":" << x << ",\"y\":" << y
        << ",\"group\":" << group_of[i] << "}";
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
