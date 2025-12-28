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
}  // namespace

void LoadWordleDict(const std::string& dict_text) {
  std::vector<std::string> words = SplitWordsText(dict_text);
  g_wordle.SetWordList(words);
  g_loaded = !g_wordle.words().empty();
}

std::string WordleBestGuess() {
  if (!g_loaded) {
    return "";
  }
  std::vector<size_t> indices(g_wordle.words().size());
  std::iota(indices.begin(), indices.end(), 0);
  double entropy = 0.0;
  std::string guess = g_wordle.BestGuess(indices, indices, &entropy);
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

std::string ConnectionsSolve(const std::string& words_text) {
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
  similarity.BuildMatrix(vectors);
  aletheia::ConnectionsSolver solver(similarity.matrix());
  std::vector<uint16_t> groups = solver.SolveBestPartition();

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
  out << "]}";
  return out.str();
}

EMSCRIPTEN_BINDINGS(aletheia_wasm) {
  emscripten::function("loadWordleDict", &LoadWordleDict);
  emscripten::function("wordleBestGuess", &WordleBestGuess);
  emscripten::function("wordlePattern", &WordlePattern);
  emscripten::function("connectionsSolve", &ConnectionsSolve);
}
