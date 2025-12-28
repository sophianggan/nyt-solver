#include "Solver.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
struct Config {
  std::string wordle_dict;
  std::string wordle_target;
  size_t wordle_max_steps = 6;
  bool wordle_interactive = false;
  bool wordle_adversarial = false;
  bool wordle_profile = false;
  bool wordle_hard = false;
  std::string connections_words;
  std::string embeddings_path;
  std::string embeddings_format = "word2vec";
  bool allow_fallback = false;
  std::string connections_dot;
  int connections_pca_dims = 2;
  size_t connections_red_herrings = 3;
  bool connections_interactive = false;
  bool connections_demo = false;
  bool connections_shuffle = false;
  bool connections_hard = false;
  double connections_lexical_weight = 0.25;
};

void PrintUsage(const char* argv0) {
  std::cout
      << "Aletheia: High-Performance Puzzle Optimization Engine\n"
      << "Usage:\n"
      << "  " << argv0
      << " --wordle-dict WORDS.txt --wordle-target CRANE [--wordle-max-steps 6]\n"
      << "  " << argv0
      << " --wordle-dict WORDS.txt --interactive [--wordle-target CRANE]\n"
      << "  " << argv0
      << " --wordle-dict WORDS.txt --interactive --adversarial\n"
      << "  " << argv0
      << " --connections-words WORDS16.txt --embeddings VECTORS.bin "
         "[--embeddings-format word2vec|text]\n"
      << "Options:\n"
      << "  --wordle-dict PATH         5-letter dictionary (one word per line)\n"
      << "  --wordle-target WORD       Target word for solution path or auto feedback\n"
      << "  --wordle-max-steps N       Max guesses to simulate (default 6)\n"
      << "  --interactive              Interactive Wordle loop using feedback\n"
      << "  --adversarial              Absurdle-style mode (auto pattern, worst case)\n"
      << "  --profile                  Log allocation vs compute timing per turn\n"
      << "  --wordle-hard              Enforce Wordle hard mode in interactive play\n"
      << "  --connections-words PATH   16 words for Connections (whitespace or line-separated)\n"
      << "  --connections-demo         Use the built-in demo puzzle + categories\n"
      << "  --connections-shuffle      Shuffle word order for display each run\n"
      << "  --connections-hard         Boost lexical similarity for wordplay puzzles\n"
      << "  --connections-lexical-weight N  Lexical weight (0-1, default 0.25)\n"
      << "  --embeddings PATH          Word2Vec binary or text embeddings file\n"
      << "  --embeddings-format FMT    word2vec (binary) or text (GloVe/fastText .vec)\n"
      << "  --connections-dot PATH     Write a Graphviz .dot visualization\n"
      << "  --connections-pca-dims N   PCA projection dimensions (default 2)\n"
      << "  --connections-red-herrings N  Show N ambiguous words (default 3)\n"
      << "  --connections-interactive Interactive guessing mode\n"
      << "  --allow-fallback           Use deterministic hash embeddings if missing\n"
      << "                           (also enables a built-in demo word list)\n"
      << "  --help                     Show this help\n";
}

std::string ToLowerAscii(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    if (c >= 'A' && c <= 'Z') {
      out.push_back(static_cast<char>(c - 'A' + 'a'));
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string ToUpperAscii(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    if (c >= 'a' && c <= 'z') {
      out.push_back(static_cast<char>(c - 'a' + 'A'));
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string TrimWhitespace(const std::string& input) {
  size_t start = 0;
  while (start < input.size() &&
         std::isspace(static_cast<unsigned char>(input[start]))) {
    ++start;
  }
  size_t end = input.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(input[end - 1]))) {
    --end;
  }
  return input.substr(start, end - start);
}

std::vector<std::string> LoadWordList(const std::string& path) {
  std::ifstream infile(path);
  std::vector<std::string> words;
  if (!infile) {
    return words;
  }
  std::string word;
  while (infile >> word) {
    words.push_back(ToLowerAscii(word));
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

bool IsPatternOnly(const std::string& input) {
  if (input.size() != 5) {
    return false;
  }
  for (char c : input) {
    if (c < '0' || c > '2') {
      return false;
    }
  }
  return true;
}

bool ParsePatternString(const std::string& pattern_str, int* pattern_out) {
  if (!IsPatternOnly(pattern_str)) {
    return false;
  }
  int pattern = 0;
  int base = 1;
  for (char c : pattern_str) {
    pattern += (c - '0') * base;
    base *= 3;
  }
  if (pattern_out) {
    *pattern_out = pattern;
  }
  return true;
}

void PrintInteractiveHelp() {
  std::cout
      << "Interactive commands:\n"
      << "  GUESS PATTERN   Provide a guess and 5-digit pattern (0/1/2)\n"
      << "  PATTERN         Provide only a 5-digit pattern to accept suggestion\n"
      << "  GUESS           (auto feedback) Provide a guess only\n"
      << "  [Enter]         (auto feedback) Accept suggestion\n"
      << "  (adversarial)   The engine responds with the least-informative pattern\n"
      << "  22222           Mark solved\n"
      << "  help or ?       Show this help\n"
      << "  quit or exit    Leave interactive mode\n";
}

void PrintColoredPattern(const std::string& guess,
                         const std::string& pattern) {
  const char* colors[] = {"\x1b[90m", "\x1b[33m", "\x1b[32m"};
  const char* reset = "\x1b[0m";
  std::string display = ToUpperAscii(guess);
  std::cout << "Feedback: ";
  for (size_t i = 0; i < pattern.size() && i < display.size(); ++i) {
    int value = pattern[i] - '0';
    if (value < 0 || value > 2) {
      value = 0;
    }
    std::cout << colors[value] << display[i] << reset;
  }
  std::cout << "\n";
}

void PrintEntropyBar(size_t remaining, size_t total) {
  if (total == 0) {
    return;
  }
  constexpr int kBarWidth = 20;
  double ratio = static_cast<double>(remaining) / static_cast<double>(total);
  int filled = static_cast<int>(std::round(ratio * kBarWidth));
  if (filled > kBarWidth) {
    filled = kBarWidth;
  }
  if (filled < 0) {
    filled = 0;
  }
  std::cout << "Uncertainty: [";
  for (int i = 0; i < kBarWidth; ++i) {
    std::cout << (i < filled ? '#' : '.');
  }
  std::cout << "] " << std::fixed << std::setprecision(1) << ratio * 100.0
            << "% remaining\n";
}

struct PcaResult {
  Eigen::MatrixXd projected;
  Eigen::MatrixXd components;
  Eigen::VectorXd eigenvalues;
  Eigen::VectorXd mean;
};

struct DemoConnectionsPuzzle {
  std::vector<std::string> words;
  std::vector<std::string> labels;
  std::vector<std::vector<int>> groups;
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
  Eigen::VectorXd top_evals(k);
  for (int i = 0; i < k; ++i) {
    components.col(i) = evecs.col(indices[i]);
    top_evals[i] = evals[indices[i]];
  }

  result.projected = centered * components;
  result.components = std::move(components);
  result.eigenvalues = std::move(top_evals);
  result.mean = std::move(mean);
  return result;
}

double ClusterConfidence(const std::vector<Eigen::VectorXd>& embeddings,
                         const std::vector<int>& indices) {
  if (indices.empty()) {
    return 0.0;
  }
  const int n = static_cast<int>(indices.size());
  const int dims = static_cast<int>(embeddings[indices[0]].size());
  if (dims == 0 || n == 1) {
    return 0.0;
  }

  Eigen::MatrixXd X(n, dims);
  for (int i = 0; i < n; ++i) {
    X.row(i) = embeddings[indices[i]].transpose();
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
}

double AverageWithinGroupSimilarity(const Eigen::MatrixXd& similarity,
                                    const std::vector<int>& indices) {
  if (indices.size() < 2) {
    return 0.0;
  }
  double total = 0.0;
  int pairs = 0;
  for (size_t i = 0; i < indices.size(); ++i) {
    for (size_t j = i + 1; j < indices.size(); ++j) {
      total += similarity(indices[i], indices[j]);
      ++pairs;
    }
  }
  return pairs == 0 ? 0.0 : total / static_cast<double>(pairs);
}

bool WriteConnectionsDot(const std::string& path,
                         const std::vector<std::string>& words,
                         const Eigen::MatrixXd& similarity,
                         const std::vector<int>& group_of,
                         const PcaResult& pca) {
  if (path.empty()) {
    return false;
  }
  std::ofstream out(path);
  if (!out) {
    return false;
  }

  const char* colors[] = {"#e45756", "#4c78a8", "#54a24b", "#f2cf5b"};
  out << "graph Connections {\n";
  out << "  graph [overlap=false, splines=true];\n";
  out << "  node [shape=circle, style=filled, fontname=\"Helvetica\"];\n";

  bool has_positions = pca.projected.cols() >= 2 &&
                       pca.projected.rows() == static_cast<int>(words.size());
  for (size_t i = 0; i < words.size(); ++i) {
    int group = group_of[i];
    const char* color = group >= 0 && group < 4 ? colors[group] : "#999999";
    out << "  \"" << words[i] << "\" [fillcolor=\"" << color << "\"";
    if (has_positions) {
      double x = pca.projected(static_cast<int>(i), 0);
      double y = pca.projected(static_cast<int>(i), 1);
      out << ", pos=\"" << std::fixed << std::setprecision(3) << x * 2.0
          << "," << y * 2.0 << "!\"";
    }
    out << "];\n";
  }

  for (size_t i = 0; i < words.size(); ++i) {
    for (size_t j = i + 1; j < words.size(); ++j) {
      double sim = similarity(static_cast<int>(i), static_cast<int>(j));
      double weight = std::max(0.0, sim);
      double penwidth = 1.0 + weight * 3.0;
      bool same_group = group_of[i] >= 0 && group_of[i] == group_of[j];
      out << "  \"" << words[i] << "\" -- \"" << words[j] << "\"";
      out << " [label=\"" << std::fixed << std::setprecision(2) << sim << "\"";
      out << ", penwidth=" << std::fixed << std::setprecision(2) << penwidth;
      if (same_group) {
        const char* color =
            group_of[i] >= 0 && group_of[i] < 4 ? colors[group_of[i]]
                                                : "#444444";
        out << ", color=\"" << color << "\"";
      } else {
        out << ", color=\"#bbbbbb\"";
      }
      out << "];\n";
    }
  }
  out << "}\n";
  return true;
}

std::vector<std::string> SplitWords(std::string line) {
  for (char& c : line) {
    if (c == ',' || c == ';') {
      c = ' ';
    }
  }
  std::istringstream iss(line);
  std::vector<std::string> words;
  std::string token;
  while (iss >> token) {
    std::string norm = ToLowerAscii(token);
    norm.erase(std::remove_if(norm.begin(), norm.end(),
                              [](char c) { return c < 'a' || c > 'z'; }),
               norm.end());
    if (!norm.empty()) {
      words.push_back(norm);
    }
  }
  return words;
}

void PrintConnectionsWords(const std::vector<std::string>& words) {
  std::cout << "Words:\n";
  for (size_t i = 0; i < words.size(); ++i) {
    if (i % 4 == 0) {
      std::cout << "  ";
    }
    std::cout << std::setw(10) << words[i];
    if (i % 4 == 3 || i + 1 == words.size()) {
      std::cout << "\n";
    }
  }
}

std::vector<DemoConnectionsPuzzle> BuildDemoConnectionsPuzzles() {
  std::vector<DemoConnectionsPuzzle> puzzles;
  puzzles.reserve(10);

  {
    DemoConnectionsPuzzle demo;
    demo.words = {"bee",    "tee",   "cue",    "sea",
                  "pinch",  "nick",  "swipe",  "lift",
                  "brie",   "feta",  "gouda",  "cheddar",
                  "hand",   "back",  "arm",    "face"};
    demo.labels = {"Homophones of letters", "Synonyms for steal",
                   "Cheeses", "Body parts that are verbs"};
    demo.groups = {
        {0, 1, 2, 3},     // Homophones of letters
        {4, 5, 6, 7},     // Synonyms for steal
        {8, 9, 10, 11},   // Cheeses
        {12, 13, 14, 15}  // Body parts that are verbs
    };
    puzzles.push_back(std::move(demo));
  }

  {
    DemoConnectionsPuzzle demo;
    demo.words = {"clubs",  "hearts", "spades", "diamonds",
                  "mars",   "venus",  "saturn", "uranus",
                  "mail",   "chat",   "show",   "court",
                  "knee",   "knot",   "knit",   "knob"};
    demo.labels = {"Card suits", "Planets",
                   "___ room", "Silent 'k' words"};
    demo.groups = {
        {0, 1, 2, 3},     // Card suits
        {4, 5, 6, 7},     // Planets
        {8, 9, 10, 11},   // ___ room
        {12, 13, 14, 15}  // Silent 'k' words
    };
    puzzles.push_back(std::move(demo));
  }

  {
    DemoConnectionsPuzzle demo;
    demo.words = {"inch",  "foot", "yard",  "mile",
                  "stare", "gaze", "peek",  "view",
                  "jack",  "bill", "will",  "mark",
                  "level", "radar","civic", "refer"};
    demo.labels = {"Units of length", "Ways to look",
                   "Male names", "Palindromes"};
    demo.groups = {
        {0, 1, 2, 3},     // Units of length
        {4, 5, 6, 7},     // Ways to look
        {8, 9, 10, 11},   // Male names
        {12, 13, 14, 15}  // Palindromes
    };
    puzzles.push_back(std::move(demo));
  }

  {
    DemoConnectionsPuzzle demo;
    demo.words = {"beta",  "gamma", "delta", "theta",
                  "tango", "salsa", "waltz", "polka",
                  "cook",  "text",  "note",  "rule",
                  "tiny",  "mini",  "petite","wee"};
    demo.labels = {"Greek letters", "Dances",
                   "___ book", "Synonyms for small"};
    demo.groups = {
        {0, 1, 2, 3},     // Greek letters
        {4, 5, 6, 7},     // Dances
        {8, 9, 10, 11},   // ___ book
        {12, 13, 14, 15}  // Synonyms for small
    };
    puzzles.push_back(std::move(demo));
  }

  {
    DemoConnectionsPuzzle demo;
    demo.words = {"punch", "time",  "border","finish",
                  "maple", "cedar", "pine",  "birch",
                  "ruby",  "python","java",  "rust",
                  "fee",   "fare",  "toll",  "rate"};
    demo.labels = {"___ line", "Trees",
                   "Programming languages", "Charges"};
    demo.groups = {
        {0, 1, 2, 3},     // ___ line
        {4, 5, 6, 7},     // Trees
        {8, 9, 10, 11},   // Programming languages
        {12, 13, 14, 15}  // Charges
    };
    puzzles.push_back(std::move(demo));
  }

  {
    DemoConnectionsPuzzle demo;
    demo.words = {"dragon","unicorn","phoenix","kraken",
                  "bunt",  "steal",  "pitch",  "swing",
                  "chop",  "stir",   "bake",   "boil",
                  "robin", "crane",  "heron",  "gull"};
    demo.labels = {"Mythical creatures", "Baseball actions",
                   "Cooking verbs", "Birds"};
    demo.groups = {
        {0, 1, 2, 3},     // Mythical creatures
        {4, 5, 6, 7},     // Baseball actions
        {8, 9, 10, 11},   // Cooking verbs
        {12, 13, 14, 15}  // Birds
    };
    puzzles.push_back(std::move(demo));
  }

  {
    DemoConnectionsPuzzle demo;
    demo.words = {"scarlet","crimson","ruby",  "maroon",
                  "pre",    "fore",   "ante",  "prior",
                  "board",  "bird",   "jack",  "list",
                  "pots",   "tops",   "post",  "spot"};
    demo.labels = {"Shades of red", "Prefixes meaning before",
                   "___ black", "Anagrams of stop"};
    demo.groups = {
        {0, 1, 2, 3},     // Shades of red
        {4, 5, 6, 7},     // Prefixes meaning before
        {8, 9, 10, 11},   // ___ black
        {12, 13, 14, 15}  // Anagrams of stop
    };
    puzzles.push_back(std::move(demo));
  }

  {
    DemoConnectionsPuzzle demo;
    demo.words = {"ounce", "pound", "quart", "pint",
                  "pen",   "ruler", "glue",  "eraser",
                  "tomb",  "mile",  "touch", "corner",
                  "mad",   "irate", "upset", "sore"};
    demo.labels = {"Units of measure", "School supplies",
                   "___ stone", "Synonyms for angry"};
    demo.groups = {
        {0, 1, 2, 3},     // Units of measure
        {4, 5, 6, 7},     // School supplies
        {8, 9, 10, 11},   // ___ stone
        {12, 13, 14, 15}  // Synonyms for angry
    };
    puzzles.push_back(std::move(demo));
  }

  {
    DemoConnectionsPuzzle demo;
    demo.words = {"cirrus","cumulus","stratus","nimbus",
                  "penne", "fusilli","orzo",  "rigatoni",
                  "air",   "witch", "hover", "space",
                  "bold",  "game",  "plucky","valiant"};
    demo.labels = {"Cloud types", "Pasta shapes",
                   "___ craft", "Synonyms for brave"};
    demo.groups = {
        {0, 1, 2, 3},     // Cloud types
        {4, 5, 6, 7},     // Pasta shapes
        {8, 9, 10, 11},   // ___ craft
        {12, 13, 14, 15}  // Synonyms for brave
    };
    puzzles.push_back(std::move(demo));
  }

  {
    DemoConnectionsPuzzle demo;
    demo.words = {"king",  "queen", "rook",  "bishop",
                  "loafer","pump",  "mule",  "clog",
                  "break", "burn",  "ache",  "beat",
                  "won",   "too",   "fore",  "ate"};
    demo.labels = {"Chess pieces", "Types of shoes",
                   "___ heart", "Homophones of numbers"};
    demo.groups = {
        {0, 1, 2, 3},     // Chess pieces
        {4, 5, 6, 7},     // Types of shoes
        {8, 9, 10, 11},   // ___ heart
        {12, 13, 14, 15}  // Homophones of numbers
    };
    puzzles.push_back(std::move(demo));
  }

  return puzzles;
}

std::vector<std::string> BuildDefaultGroupLabels(size_t group_count) {
  std::vector<std::string> labels;
  labels.reserve(group_count);
  for (size_t i = 0; i < group_count; ++i) {
    labels.push_back("Group " + std::to_string(i + 1));
  }
  return labels;
}

std::vector<int> RankGroupsByDifficulty(const std::vector<double>& scores) {
  std::vector<int> order(scores.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(),
            [&](int a, int b) { return scores[a] > scores[b]; });
  std::vector<int> rank(scores.size(), 0);
  for (size_t i = 0; i < order.size(); ++i) {
    rank[order[i]] = static_cast<int>(i);
  }
  return rank;
}

void PrintSolvedGroups(const std::vector<std::string>& words,
                       const std::vector<std::vector<int>>& group_indices,
                       const std::vector<std::string>& labels,
                       const std::vector<bool>& solved,
                       const std::vector<int>& rank_by_group,
                       const char* title = "Solved groups") {
  const char* colors[] = {"\x1b[38;2;255;204;0m",  // yellow
                          "\x1b[38;2;0;200;0m",    // green
                          "\x1b[38;2;0;120;255m",  // blue
                          "\x1b[38;2;168;85;247m"}; // purple
  const char* reset = "\x1b[0m";
  std::cout << "\n" << title << ":\n";
  for (size_t rank = 0; rank < group_indices.size(); ++rank) {
    auto it = std::find(rank_by_group.begin(), rank_by_group.end(),
                        static_cast<int>(rank));
    if (it == rank_by_group.end()) {
      continue;
    }
    size_t group = static_cast<size_t>(std::distance(rank_by_group.begin(), it));
    if (!solved[group]) {
      continue;
    }
    const char* color = colors[rank < 4 ? rank : 3];
    std::cout << "  " << color << labels[group] << reset << ": ";
    bool first = true;
    for (int idx : group_indices[group]) {
      if (!first) {
        std::cout << ", ";
      }
      std::cout << words[idx];
      first = false;
    }
    std::cout << "\n";
  }
  std::cout << "\n";
}

void RunConnectionsInteractive(
    const std::vector<std::string>& words,
    const std::vector<int>& group_of,
    const std::vector<std::vector<int>>& group_indices,
    const std::vector<std::string>& labels,
    const std::vector<double>& group_avg_sim) {
  std::unordered_map<std::string, size_t> index_by_word;
  for (size_t i = 0; i < words.size(); ++i) {
    index_by_word[ToLowerAscii(words[i])] = i;
  }

  std::vector<bool> solved(group_indices.size(), false);
  std::vector<int> rank_by_group = RankGroupsByDifficulty(group_avg_sim);
  size_t solved_count = 0;

  std::cout << "\n[Connections Interactive]\n";
  PrintConnectionsWords(words);
  std::cout << "Enter 4 words (comma or space separated), or type "
               "'words', 'board', 'solve', or 'quit'.\n";

  std::string line;
  while (solved_count < group_indices.size()) {
    std::cout << "Guess group (" << solved_count << "/"
              << group_indices.size() << " solved): ";
    if (!std::getline(std::cin, line)) {
      break;
    }
    line = TrimWhitespace(line);
    if (line.empty()) {
      continue;
    }
    std::string command = ToLowerAscii(line);
    if (command == "quit" || command == "exit") {
      break;
    }
    if (command == "words" || command == "show") {
      PrintConnectionsWords(words);
      continue;
    }
    if (command == "board" || command == "groups") {
      PrintSolvedGroups(words, group_indices, labels, solved, rank_by_group);
      continue;
    }
    if (command == "solve" || command == "reveal") {
      std::vector<bool> solved_all(group_indices.size(), true);
      PrintSolvedGroups(words, group_indices, labels, solved_all,
                        rank_by_group, "All groups");
      break;
    }

    std::vector<std::string> guess_words = SplitWords(line);
    if (guess_words.size() != 4) {
      std::cout << "Enter exactly 4 words.\n";
      continue;
    }
    std::unordered_set<size_t> guess_indices;
    bool ok = true;
    for (const auto& word : guess_words) {
      auto it = index_by_word.find(word);
      if (it == index_by_word.end()) {
        std::cout << "Unknown word: " << word << "\n";
        ok = false;
        break;
      }
      guess_indices.insert(it->second);
    }
    if (!ok) {
      continue;
    }
    if (guess_indices.size() != 4) {
      std::cout << "Duplicate words are not allowed.\n";
      continue;
    }

    std::vector<size_t> guessed;
    guessed.reserve(4);
    for (size_t idx : guess_indices) {
      guessed.push_back(idx);
    }

    int group = group_of[guessed[0]];
    bool same_group = true;
    for (size_t i = 1; i < guessed.size(); ++i) {
      if (group_of[guessed[i]] != group) {
        same_group = false;
        break;
      }
    }

    if (same_group) {
      if (group < 0 || static_cast<size_t>(group) >= solved.size()) {
        std::cout << "That group is not valid.\n";
        continue;
      }
      if (solved[group]) {
        std::cout << "Group already solved.\n";
        continue;
      }
      solved[group] = true;
      ++solved_count;
      std::cout << "Correct! " << labels[group] << " solved.\n";
      PrintSolvedGroups(words, group_indices, labels, solved, rank_by_group);
      if (solved_count == group_indices.size()) {
        std::cout << "All groups solved.\n";
        break;
      }
      continue;
    }

    std::vector<int> counts(group_indices.size(), 0);
    for (size_t idx : guessed) {
      int gid = group_of[idx];
      if (gid >= 0 && static_cast<size_t>(gid) < counts.size()) {
        counts[gid]++;
      }
    }
    auto best_it = std::max_element(counts.begin(), counts.end());
    int best_group = best_it == counts.end()
                         ? -1
                         : static_cast<int>(best_it - counts.begin());
    int best_count = best_it == counts.end() ? 0 : *best_it;
    if (best_group >= 0) {
      std::cout << "Not a group. Best match: " << labels[best_group] << " ("
                << best_count << "/4).\n";
    } else {
      std::cout << "Not a group.\n";
    }
  }
}

int SelectAdversarialPattern(
    const aletheia::PackedWord& guess,
    const std::vector<size_t>& remaining,
    const std::vector<aletheia::WordEntry>& words,
    int* count_out) {
  std::array<int, 243> counts{};
  counts.fill(0);
  for (size_t index : remaining) {
    int pattern = aletheia::WordleSolver::Pattern(guess, words[index].packed);
    counts[pattern]++;
  }

  int best_pattern = 0;
  int best_count = -1;
  int best_greens = 6;
  int best_yellows = 6;
  for (int pattern = 0; pattern < static_cast<int>(counts.size()); ++pattern) {
    int count = counts[pattern];
    if (count == 0) {
      continue;
    }
    int temp = pattern;
    int greens = 0;
    int yellows = 0;
    for (int i = 0; i < 5; ++i) {
      int digit = temp % 3;
      if (digit == 2) {
        greens++;
      } else if (digit == 1) {
        yellows++;
      }
      temp /= 3;
    }
    if (count > best_count ||
        (count == best_count &&
         (greens < best_greens ||
          (greens == best_greens &&
           (yellows < best_yellows ||
            (yellows == best_yellows && pattern < best_pattern)))))) {
      best_count = count;
      best_pattern = pattern;
      best_greens = greens;
      best_yellows = yellows;
    }
  }

  if (count_out) {
    *count_out = best_count < 0 ? 0 : best_count;
  }
  return best_pattern;
}
}  // namespace

int main(int argc, char** argv) {
  Config config;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--wordle-dict" && i + 1 < argc) {
      config.wordle_dict = argv[++i];
    } else if (arg == "--wordle-target" && i + 1 < argc) {
      config.wordle_target = ToLowerAscii(argv[++i]);
    } else if (arg == "--wordle-max-steps" && i + 1 < argc) {
      config.wordle_max_steps = static_cast<size_t>(std::stoul(argv[++i]));
    } else if (arg == "--interactive") {
      config.wordle_interactive = true;
    } else if (arg == "--adversarial") {
      config.wordle_adversarial = true;
    } else if (arg == "--profile") {
      config.wordle_profile = true;
    } else if (arg == "--wordle-hard") {
      config.wordle_hard = true;
    } else if (arg == "--connections-words" && i + 1 < argc) {
      config.connections_words = argv[++i];
    } else if (arg == "--embeddings" && i + 1 < argc) {
      config.embeddings_path = argv[++i];
    } else if (arg == "--embeddings-format" && i + 1 < argc) {
      config.embeddings_format = argv[++i];
    } else if (arg == "--connections-dot" && i + 1 < argc) {
      config.connections_dot = argv[++i];
    } else if (arg == "--connections-pca-dims" && i + 1 < argc) {
      config.connections_pca_dims = std::stoi(argv[++i]);
    } else if (arg == "--connections-red-herrings" && i + 1 < argc) {
      config.connections_red_herrings = static_cast<size_t>(std::stoul(argv[++i]));
    } else if (arg == "--connections-interactive") {
      config.connections_interactive = true;
    } else if (arg == "--connections-demo") {
      config.connections_demo = true;
    } else if (arg == "--connections-shuffle") {
      config.connections_shuffle = true;
    } else if (arg == "--connections-hard") {
      config.connections_hard = true;
    } else if (arg == "--connections-lexical-weight" && i + 1 < argc) {
      config.connections_lexical_weight = std::stod(argv[++i]);
    } else if (arg == "--allow-fallback") {
      config.allow_fallback = true;
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      PrintUsage(argv[0]);
      return 1;
    }
  }

  bool ran_any = false;

  if (!config.wordle_dict.empty() || !config.wordle_target.empty()) {
    if (config.wordle_dict.empty()) {
      std::cerr << "Wordle requires --wordle-dict.\n";
      return 1;
    }
    if (config.wordle_adversarial && !config.wordle_interactive) {
      std::cerr << "--adversarial requires --interactive.\n";
      return 1;
    }
    if (config.wordle_adversarial && !config.wordle_target.empty()) {
      std::cerr << "Choose either --adversarial or --wordle-target.\n";
      return 1;
    }
    aletheia::WordleSolver wordle;
    if (!wordle.LoadDictionary(config.wordle_dict)) {
      std::cerr << "Failed to load wordle dictionary: "
                << config.wordle_dict << "\n";
      return 1;
    }

    bool has_target = false;
    aletheia::PackedWord target_packed{};
    if (!config.wordle_target.empty()) {
      std::string normalized =
          aletheia::WordleSolver::NormalizeWord(config.wordle_target);
      if (!aletheia::WordleSolver::IsValidWord(normalized)) {
        std::cerr << "Invalid Wordle target: " << config.wordle_target << "\n";
        return 1;
      }
      config.wordle_target = normalized;
      target_packed = aletheia::WordleSolver::EncodeWord(config.wordle_target);
      has_target = true;
    }

    if (config.wordle_interactive) {
      std::vector<size_t> remaining(wordle.words().size());
      std::iota(remaining.begin(), remaining.end(), 0);
      std::vector<size_t> all_indices(wordle.words().size());
      std::iota(all_indices.begin(), all_indices.end(), 0);
      const size_t initial_count = remaining.size();
      const bool adversarial = config.wordle_adversarial;
      const bool auto_pattern = has_target || adversarial;
      const bool hard_mode = config.wordle_hard;
      size_t steps_taken = 0;
      std::vector<size_t> next;
      next.reserve(remaining.size());

      std::cout << "\n[Wordle Interactive]\n";
      if (adversarial) {
        std::cout << "Adversarial mode enabled.\n";
      } else if (auto_pattern) {
        std::cout << "Auto feedback enabled.\n";
      }
      if (hard_mode) {
        std::cout << "Hard mode enabled.\n";
      }
      std::cout << "Type '?' for help.\n";
      while (true) {
        if (remaining.empty()) {
          std::cout << "Remaining possibilities: 0\n";
          std::cout << "No valid candidates remain. Check your inputs.\n";
          break;
        }
        if (steps_taken >= config.wordle_max_steps) {
          std::cout << "Out of rounds (" << config.wordle_max_steps << ").\n";
          break;
        }

        double entropy = 0.0;
        auto start = std::chrono::high_resolution_clock::now();
        const std::vector<size_t>& guess_pool =
            hard_mode ? remaining : all_indices;
        std::string suggestion =
            wordle.BestGuess(guess_pool, remaining, &entropy);
        auto end = std::chrono::high_resolution_clock::now();
        auto micros =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start)
                .count();

        std::cout << "Suggested guess: " << suggestion << " entropy="
                  << std::fixed << std::setprecision(4) << entropy << "\n";
        std::cout << "Round " << (steps_taken + 1) << " of "
                  << config.wordle_max_steps << "\n";
        std::cout << "Remaining possibilities: " << remaining.size() << "\n";
        PrintEntropyBar(remaining.size(), initial_count);
        std::cout << "Compute latency: " << micros << "us\n";
        if (auto_pattern) {
          std::cout << "Enter guess (or press Enter to accept suggestion), or "
                       "22222 to finish: ";
        } else {
          std::cout << "Enter guess and pattern (e.g., RAISE 00102), a pattern "
                       "(00102), or 22222 to finish: ";
        }

        std::string line;
        if (!std::getline(std::cin, line)) {
          break;
        }
        line = TrimWhitespace(line);

        if (line == "help" || line == "?") {
          PrintInteractiveHelp();
          continue;
        }
        if (line == "quit" || line == "exit") {
          break;
        }
        if (line == "22222") {
          std::cout << "Solved.\n";
          break;
        }

        std::string guess_input;
        std::string pattern_input;
        if (auto_pattern) {
          if (line.empty()) {
            guess_input = suggestion;
          } else {
            std::istringstream iss(line);
            if (!(iss >> guess_input)) {
              continue;
            }
          }
        } else {
          if (line.empty()) {
            std::cout << "Pattern for " << ToUpperAscii(suggestion) << ": ";
            if (!std::getline(std::cin, line)) {
              break;
            }
            line = TrimWhitespace(line);
            if (line.empty()) {
              continue;
            }
          }

          if (IsPatternOnly(line)) {
            guess_input = suggestion;
            pattern_input = line;
          } else {
            std::istringstream iss(line);
            if (!(iss >> guess_input)) {
              continue;
            }
            if (guess_input == "22222") {
              std::cout << "Solved.\n";
              break;
            }
            if (!(iss >> pattern_input)) {
              std::cout << "Expected guess and pattern.\n";
              continue;
            }
          }
        }

        std::string guess =
            aletheia::WordleSolver::NormalizeWord(guess_input);
        if (!aletheia::WordleSolver::IsValidWord(guess)) {
          std::cout << "Invalid guess: " << guess_input << "\n";
          continue;
        }
        if (hard_mode) {
          bool allowed = false;
          for (size_t idx : remaining) {
            if (wordle.words()[idx].text == guess) {
              allowed = true;
              break;
            }
          }
          if (!allowed) {
            std::cout << "Hard mode: guess must match all revealed hints.\n";
            continue;
          }
        }

        aletheia::PackedWord guess_packed =
            aletheia::WordleSolver::EncodeWord(guess);

        if (adversarial) {
          int pattern_value = SelectAdversarialPattern(
              guess_packed, remaining, wordle.words(), nullptr);
          pattern_input = aletheia::WordleSolver::PatternString(pattern_value);
        } else if (auto_pattern) {
          int pattern_value =
              aletheia::WordleSolver::Pattern(guess_packed, target_packed);
          pattern_input = aletheia::WordleSolver::PatternString(pattern_value);
        } else {
          if (!ParsePatternString(pattern_input, nullptr)) {
            std::cout << "Invalid pattern: " << pattern_input
                      << " (use 5 digits of 0/1/2)\n";
            continue;
          }
        }

        size_t before_count = remaining.size();
        next.clear();
        auto alloc_start = std::chrono::high_resolution_clock::now();
        if (next.capacity() < before_count) {
          next.reserve(before_count);
        }
        auto alloc_end = std::chrono::high_resolution_clock::now();
        auto filter_start = std::chrono::high_resolution_clock::now();
        aletheia::WordleSolver::FilterCandidates(wordle.words(),
                                                 remaining,
                                                 guess,
                                                 pattern_input,
                                                 &next);
        auto filter_end = std::chrono::high_resolution_clock::now();

        size_t match_count = next.size();
        if (match_count == 0) {
          std::cout << "Pattern is inconsistent with remaining words.\n";
          continue;
        }

        double info_bits = 0.0;
        double p = static_cast<double>(match_count) /
                   static_cast<double>(before_count);
        if (p > 0.0) {
          info_bits = -std::log2(p);
        }
        remaining.swap(next);

        PrintColoredPattern(guess, pattern_input);
        std::cout << "Pattern: " << pattern_input << "\n";
        if (config.wordle_profile) {
          auto alloc_us =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  alloc_end - alloc_start)
                  .count();
          auto compute_us =
              std::chrono::duration_cast<std::chrono::microseconds>(
                  filter_end - filter_start)
                  .count();
          std::cout << "Perf: alloc=" << alloc_us
                    << "us compute=" << compute_us << "us\n";
        }

        std::cout << "Information gained: " << std::fixed
                  << std::setprecision(4) << info_bits << " bits\n";
        std::cout << "Remaining possibilities: " << remaining.size() << "\n";
        double bits_remaining =
            remaining.empty() ? 0.0 : std::log2(remaining.size());
        std::cout << "Bits remaining: " << std::fixed << std::setprecision(4)
                  << bits_remaining << "\n";
        double pruned = static_cast<double>(before_count - remaining.size());
        double pruned_pct =
            before_count > 0
                ? (pruned / static_cast<double>(before_count)) * 100.0
                : 0.0;
        std::cout << "Optimization Summary: pruned " << before_count
                  << " -> " << remaining.size() << " ("
                  << std::fixed << std::setprecision(1) << pruned_pct
                  << "%)\n";
        PrintEntropyBar(remaining.size(), initial_count);

        steps_taken++;
        if (pattern_input == "22222") {
          std::cout << "Solved.\n";
          break;
        }
      }
      ran_any = true;
    }

    if (!config.wordle_interactive) {
      std::vector<size_t> all_indices(wordle.words().size());
      std::iota(all_indices.begin(), all_indices.end(), 0);

      std::cout << "\n[Wordle] Dictionary size: " << all_indices.size() << "\n";
      auto start = std::chrono::high_resolution_clock::now();

      if (!config.wordle_target.empty()) {
        auto steps = wordle.SolveToTarget(config.wordle_target,
                                          config.wordle_max_steps);
        auto end = std::chrono::high_resolution_clock::now();
        auto micros =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start)
                .count();

        std::cout << "Target: " << config.wordle_target << "\n";
        std::cout << "Solution path:\n";
        for (size_t i = 0; i < steps.size(); ++i) {
          const auto& step = steps[i];
          std::cout << "  Step " << (i + 1) << ": guess=" << step.guess
                    << " pattern=" << step.pattern
                    << " entropy=" << std::fixed << std::setprecision(4)
                    << step.entropy << " bits=" << step.info_bits
                    << " remaining=" << step.remaining
                    << " -> " << step.remaining_after << "\n";
        }
        std::cout << "Total latency: " << micros << "us\n";
      } else {
        double entropy = 0.0;
        std::string guess =
            wordle.BestGuess(all_indices, all_indices, &entropy);
        auto end = std::chrono::high_resolution_clock::now();
        auto micros =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start)
                .count();
        std::cout << "Best next guess: " << guess
                  << " entropy=" << std::fixed << std::setprecision(4)
                  << entropy << "\n";
        std::cout << "Total latency: " << micros << "us\n";
      }
      ran_any = true;
    }
  }

  if (!config.connections_words.empty() || !config.embeddings_path.empty() ||
      config.allow_fallback || config.connections_demo) {
    DemoConnectionsPuzzle demo;
    bool use_demo = config.connections_demo || config.connections_words.empty();
    std::vector<std::string> words;
    std::vector<std::string> labels;
    if (config.connections_demo && !config.connections_words.empty()) {
      std::cout << "[Connections] --connections-demo enabled; ignoring "
                   "--connections-words.\n";
    }
    if (use_demo) {
      std::vector<DemoConnectionsPuzzle> puzzles =
          BuildDemoConnectionsPuzzles();
      if (puzzles.empty()) {
        std::cerr << "No demo puzzles available.\n";
        return 1;
      }
      std::mt19937 rng(static_cast<uint32_t>(
          std::chrono::high_resolution_clock::now().time_since_epoch().count()));
      std::uniform_int_distribution<size_t> dist(0, puzzles.size() - 1);
      size_t puzzle_index = dist(rng);
      demo = puzzles[puzzle_index];
      words = demo.words;
      labels = demo.labels;
      std::cout << "[Connections] Using built-in demo puzzle #"
                << (puzzle_index + 1) << ".\n";
    } else {
      words = LoadWordList(config.connections_words);
      if (words.size() != 16) {
        std::cerr << "Connections expects 16 words, got " << words.size()
                  << ".\n";
        return 1;
      }
    }

    if (config.connections_shuffle && !use_demo) {
      std::mt19937 rng(static_cast<uint32_t>(
          std::chrono::high_resolution_clock::now().time_since_epoch().count()));
      std::shuffle(words.begin(), words.end(), rng);
      std::cout << "[Connections] Shuffled word order.\n";
    } else if (config.connections_shuffle && use_demo) {
      std::cout << "[Connections] Demo puzzle is fixed; ignoring shuffle.\n";
    }

    std::unordered_set<std::string> needed(words.begin(), words.end());
    aletheia::EmbeddingStore embeddings;
    bool loaded = false;
    if (!config.embeddings_path.empty()) {
      if (config.embeddings_format == "word2vec") {
        loaded = embeddings.LoadWord2VecBinary(config.embeddings_path, needed);
      } else if (config.embeddings_format == "text") {
        loaded = embeddings.LoadText(config.embeddings_path, needed);
      } else {
        std::cerr << "Unknown embeddings format: " << config.embeddings_format
                  << "\n";
        return 1;
      }

      if (!loaded && !config.allow_fallback) {
        std::cerr << "Failed to load embeddings: " << config.embeddings_path
                  << "\n";
        return 1;
      }
    } else if (!config.allow_fallback) {
      std::cerr << "Connections requires --embeddings or --allow-fallback.\n";
      return 1;
    }

    int fallback_dims = embeddings.dimension() > 0 ? embeddings.dimension()
                                                   : 64;
    std::vector<Eigen::VectorXd> vectors;
    vectors.reserve(words.size());
    for (const auto& word : words) {
      Eigen::VectorXd vec;
      if (loaded && embeddings.GetVector(word, &vec)) {
        vectors.push_back(std::move(vec));
      } else if (config.allow_fallback) {
        vectors.push_back(FallbackEmbedding(word, fallback_dims));
      } else {
        std::cerr << "Missing embedding for word: " << word << "\n";
        return 1;
      }
    }

    aletheia::SimilarityEngine similarity;
    if (config.connections_hard) {
      similarity.BuildMatrixHybrid(vectors, words,
                                   config.connections_lexical_weight);
    } else {
      similarity.BuildMatrix(vectors);
    }

    int pca_dims = config.connections_pca_dims;
    if (pca_dims <= 0) {
      pca_dims = 2;
    }
    PcaResult pca = ComputePcaProjection(vectors, pca_dims);

    std::vector<std::vector<int>> group_indices;
    double best_score = 0.0;
    long long micros = 0;
    bool has_best_score = false;
    if (use_demo) {
      group_indices.assign(demo.labels.size(), {});
      std::unordered_map<std::string, size_t> demo_group_by_word;
      for (size_t g = 0; g < demo.groups.size(); ++g) {
        for (int idx : demo.groups[g]) {
          if (idx >= 0 && static_cast<size_t>(idx) < demo.words.size()) {
            demo_group_by_word[demo.words[idx]] = g;
          }
        }
      }
      for (size_t i = 0; i < words.size(); ++i) {
        auto it = demo_group_by_word.find(words[i]);
        if (it != demo_group_by_word.end() &&
            it->second < group_indices.size()) {
          group_indices[it->second].push_back(static_cast<int>(i));
        }
      }
    } else {
      auto start = std::chrono::high_resolution_clock::now();
      aletheia::ConnectionsSolver solver(similarity.matrix());
      std::vector<uint16_t> groups = solver.SolveBestPartition();
      auto end = std::chrono::high_resolution_clock::now();
      micros =
          std::chrono::duration_cast<std::chrono::microseconds>(end - start)
              .count();
      best_score = solver.BestScore();
      has_best_score = true;

      group_indices.reserve(groups.size());
      for (size_t g = 0; g < groups.size(); ++g) {
        uint16_t mask = groups[g];
        std::vector<int> indices;
        indices.reserve(4);
        for (size_t i = 0; i < words.size(); ++i) {
          if (mask & (1U << i)) {
            indices.push_back(static_cast<int>(i));
          }
        }
        group_indices.push_back(std::move(indices));
      }
    }

    if (has_best_score) {
      std::cout << "\n[Connections] Best score: " << std::fixed
                << std::setprecision(4) << best_score << "\n";
    } else {
      std::cout << "\n[Connections] Demo puzzle loaded.\n";
    }
    std::cout << "PCA dims: " << pca_dims << "\n";
    if (pca.eigenvalues.size() > 0) {
      double total = pca.eigenvalues.sum();
      if (total > 0.0) {
        std::cout << "PCA explained variance (top): "
                  << std::fixed << std::setprecision(3)
                  << (pca.eigenvalues[0] / total) * 100.0 << "%\n";
      }
    }

    std::vector<int> group_of(words.size(), -1);
    std::vector<double> group_confidence;
    std::vector<double> group_avg_sim;
    group_confidence.reserve(group_indices.size());
    group_avg_sim.reserve(group_indices.size());

    for (size_t g = 0; g < group_indices.size(); ++g) {
      for (int idx : group_indices[g]) {
        if (idx >= 0 && static_cast<size_t>(idx) < group_of.size()) {
          group_of[idx] = static_cast<int>(g);
        }
      }
      double confidence = ClusterConfidence(vectors, group_indices[g]);
      double avg_sim = AverageWithinGroupSimilarity(similarity.matrix(),
                                                    group_indices[g]);
      group_confidence.push_back(confidence);
      group_avg_sim.push_back(avg_sim);
    }

    if (labels.empty()) {
      labels = BuildDefaultGroupLabels(group_indices.size());
    }

    if (!config.connections_interactive) {
      for (size_t g = 0; g < group_indices.size(); ++g) {
        std::cout << "  " << labels[g] << ": ";
        bool first = true;
        for (int idx : group_indices[g]) {
          if (!first) {
            std::cout << ", ";
          }
          std::cout << words[idx];
          first = false;
        }
        std::cout << " | confidence=" << std::fixed << std::setprecision(3)
                  << group_confidence[g] << " avg_sim="
                  << std::fixed << std::setprecision(3)
                  << group_avg_sim[g] << "\n";
      }
    }

    if (!config.connections_interactive &&
        config.connections_red_herrings > 0 &&
        pca.projected.rows() == static_cast<int>(words.size()) &&
        !group_indices.empty()) {
      std::vector<Eigen::VectorXd> centroids(group_indices.size());
      for (size_t g = 0; g < group_indices.size(); ++g) {
        Eigen::VectorXd centroid =
            Eigen::VectorXd::Zero(pca.projected.cols());
        for (int index : group_indices[g]) {
          centroid += pca.projected.row(index).transpose();
        }
        centroid /= static_cast<double>(group_indices[g].size());
        centroids[g] = std::move(centroid);
      }

      struct Ambiguity {
        double margin = 0.0;
        size_t index = 0;
      };
      std::vector<Ambiguity> ambiguities;
      ambiguities.reserve(words.size());
      for (size_t i = 0; i < words.size(); ++i) {
        int own_group = group_of[i];
        if (own_group < 0) {
          continue;
        }
        Eigen::VectorXd point = pca.projected.row(static_cast<int>(i)).transpose();
        double best = (point - centroids[own_group]).norm();
        double second = std::numeric_limits<double>::infinity();
        for (size_t g = 0; g < centroids.size(); ++g) {
          if (static_cast<int>(g) == own_group) {
            continue;
          }
          double dist = (point - centroids[g]).norm();
          if (dist < second) {
            second = dist;
          }
        }
        double margin = second - best;
        ambiguities.push_back({margin, i});
      }
      std::sort(ambiguities.begin(), ambiguities.end(),
                [](const Ambiguity& a, const Ambiguity& b) {
                  return a.margin < b.margin;
                });
      size_t limit = std::min(config.connections_red_herrings,
                              ambiguities.size());
      if (limit > 0) {
        std::cout << "Red herrings (small margin): ";
        for (size_t i = 0; i < limit; ++i) {
          if (i > 0) {
            std::cout << ", ";
          }
          std::cout << words[ambiguities[i].index];
        }
        std::cout << "\n";
      }
    }

    if (!config.connections_dot.empty()) {
      if (WriteConnectionsDot(config.connections_dot, words,
                              similarity.matrix(), group_of, pca)) {
        std::cout << "Wrote dot file: " << config.connections_dot << "\n";
      } else {
        std::cout << "Failed to write dot file: "
                  << config.connections_dot << "\n";
      }
    }
    if (config.connections_interactive) {
      RunConnectionsInteractive(words, group_of, group_indices,
                                labels, group_avg_sim);
    }
    if (has_best_score) {
      std::cout << "Total latency: " << micros << "us\n";
    }
    ran_any = true;
  }

  if (!ran_any) {
    PrintUsage(argv[0]);
  }

  return 0;
}
