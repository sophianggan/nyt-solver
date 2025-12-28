#include "Solver.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace aletheia {
namespace {
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

double LexicalSimilarity(const std::string& left, const std::string& right) {
  if (left.empty() || right.empty()) {
    return 0.0;
  }
  if (left == right) {
    return 1.0;
  }
  const size_t len_a = left.size();
  const size_t len_b = right.size();
  const size_t min_len = std::min(len_a, len_b);
  const size_t max_len = std::max(len_a, len_b);

  size_t prefix = 0;
  while (prefix < min_len && left[prefix] == right[prefix]) {
    ++prefix;
  }

  size_t suffix = 0;
  while (suffix < min_len &&
         left[len_a - 1 - suffix] == right[len_b - 1 - suffix]) {
    ++suffix;
  }

  double score = 0.0;
  score += 0.45 * (static_cast<double>(prefix) / max_len);
  score += 0.45 * (static_cast<double>(suffix) / max_len);
  if (len_a == len_b) {
    score += 0.05;
  }

  if (len_a == len_b && len_a > 1) {
    std::string sorted_a = left;
    std::string sorted_b = right;
    std::sort(sorted_a.begin(), sorted_a.end());
    std::sort(sorted_b.begin(), sorted_b.end());
    if (sorted_a == sorted_b) {
      score += 0.25;
    }
  }

  if (score > 1.0) {
    score = 1.0;
  }
  return score;
}
}  // namespace

bool EmbeddingStore::LoadWord2VecBinary(
    const std::string& path,
    const std::unordered_set<std::string>& needed) {
  std::ifstream infile(path, std::ios::binary);
  if (!infile) {
    return false;
  }

  size_t vocab_size = 0;
  int dims = 0;
  infile >> vocab_size >> dims;
  if (!infile || dims <= 0) {
    return false;
  }
  infile.get();

  dimension_ = dims;
  vectors_.clear();
  const bool load_all = needed.empty();
  size_t found = 0;

  for (size_t i = 0; i < vocab_size; ++i) {
    std::string word;
    infile >> word;
    if (!infile) {
      break;
    }
    infile.get();
    std::string key = ToLowerAscii(word);

    std::vector<float> buffer(dims);
    infile.read(reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(dims * sizeof(float)));
    if (!infile) {
      return false;
    }

    if (load_all || needed.count(key) > 0) {
      Eigen::VectorXd vec(dims);
      for (int d = 0; d < dims; ++d) {
        vec[d] = static_cast<double>(buffer[d]);
      }
      vectors_[key] = std::move(vec);
      if (!load_all && ++found == needed.size()) {
        break;
      }
    }

    if (infile.peek() == '\n') {
      infile.get();
    }
  }
  return !vectors_.empty();
}

bool EmbeddingStore::LoadText(
    const std::string& path,
    const std::unordered_set<std::string>& needed) {
  std::ifstream infile(path);
  if (!infile) {
    return false;
  }

  vectors_.clear();
  dimension_ = 0;
  const bool load_all = needed.empty();

  std::string line;
  while (std::getline(infile, line)) {
    if (line.empty()) {
      continue;
    }
    std::istringstream iss(line);
    std::string word;
    if (!(iss >> word)) {
      continue;
    }
    std::string key = ToLowerAscii(word);

    std::vector<double> values;
    double value = 0.0;
    while (iss >> value) {
      values.push_back(value);
    }
    if (values.empty()) {
      continue;
    }
    if (dimension_ == 0) {
      dimension_ = static_cast<int>(values.size());
    }
    if (static_cast<int>(values.size()) != dimension_) {
      continue;
    }
    if (!load_all && needed.count(key) == 0) {
      continue;
    }

    Eigen::VectorXd vec(dimension_);
    for (int i = 0; i < dimension_; ++i) {
      vec[i] = values[i];
    }
    vectors_[key] = std::move(vec);
  }

  return !vectors_.empty();
}

bool EmbeddingStore::GetVector(const std::string& word,
                               Eigen::VectorXd* out) const {
  auto it = vectors_.find(ToLowerAscii(word));
  if (it == vectors_.end()) {
    return false;
  }
  if (out) {
    *out = it->second;
  }
  return true;
}

void SimilarityEngine::BuildMatrix(
    const std::vector<Eigen::VectorXd>& embeddings) {
  const int n = static_cast<int>(embeddings.size());
  similarity_.resize(n, n);
  for (int i = 0; i < n; ++i) {
    const double norm_i = embeddings[i].norm();
    for (int j = 0; j < n; ++j) {
      const double norm_j = embeddings[j].norm();
      if (norm_i == 0.0 || norm_j == 0.0) {
        similarity_(i, j) = 0.0;
      } else {
        similarity_(i, j) =
            embeddings[i].dot(embeddings[j]) / (norm_i * norm_j);
      }
    }
  }
}

void SimilarityEngine::BuildMatrixHybrid(
    const std::vector<Eigen::VectorXd>& embeddings,
    const std::vector<std::string>& words,
    double lexical_weight) {
  if (embeddings.size() != words.size() || embeddings.empty()) {
    BuildMatrix(embeddings);
    return;
  }
  double weight = std::max(0.0, std::min(1.0, lexical_weight));
  const int n = static_cast<int>(embeddings.size());
  similarity_.resize(n, n);
  for (int i = 0; i < n; ++i) {
    const double norm_i = embeddings[i].norm();
    for (int j = 0; j < n; ++j) {
      const double norm_j = embeddings[j].norm();
      double cosine = 0.0;
      if (norm_i != 0.0 && norm_j != 0.0) {
        cosine = embeddings[i].dot(embeddings[j]) / (norm_i * norm_j);
      }
      double lexical = LexicalSimilarity(words[i], words[j]);
      similarity_(i, j) = (1.0 - weight) * cosine + weight * lexical;
    }
  }
}

ConnectionsSolver::ConnectionsSolver(const Eigen::MatrixXd& similarity)
    : similarity_(similarity) {
  BuildGroups();
}

std::vector<uint16_t> ConnectionsSolver::SolveBestPartition() {
  best_score_ = -std::numeric_limits<double>::infinity();
  best_groups_.clear();
  std::vector<int> current;
  current.reserve(4);
  uint16_t all = static_cast<uint16_t>((1U << kNodeCount) - 1);
  Search(all, 0.0, current);

  std::vector<uint16_t> masks;
  for (int idx : best_groups_) {
    masks.push_back(groups_[idx].mask);
  }
  return masks;
}

void ConnectionsSolver::BuildGroups() {
  groups_.clear();
  groups_.reserve(1820);
  for (auto& bucket : groups_by_node_) {
    bucket.clear();
    bucket.reserve(455);
  }

  for (int i = 0; i < kNodeCount; ++i) {
    for (int j = i + 1; j < kNodeCount; ++j) {
      for (int k = j + 1; k < kNodeCount; ++k) {
        for (int l = k + 1; l < kNodeCount; ++l) {
          uint16_t mask = static_cast<uint16_t>((1U << i) | (1U << j) |
                                                (1U << k) | (1U << l));
          double score = similarity_(i, j) + similarity_(i, k) +
                         similarity_(i, l) + similarity_(j, k) +
                         similarity_(j, l) + similarity_(k, l);
          int idx = static_cast<int>(groups_.size());
          groups_.push_back({mask, score});
          groups_by_node_[i].push_back(idx);
          groups_by_node_[j].push_back(idx);
          groups_by_node_[k].push_back(idx);
          groups_by_node_[l].push_back(idx);
        }
      }
    }
  }
}

int ConnectionsSolver::FirstSetBit(uint16_t mask) {
  for (int i = 0; i < kNodeCount; ++i) {
    if (mask & (1U << i)) {
      return i;
    }
  }
  return -1;
}

void ConnectionsSolver::Search(uint16_t remaining,
                               double score,
                               std::vector<int>& current) {
  if (remaining == 0) {
    if (score > best_score_) {
      best_score_ = score;
      best_groups_ = current;
    }
    return;
  }

  int pivot = FirstSetBit(remaining);
  if (pivot < 0) {
    return;
  }

  for (int group_index : groups_by_node_[pivot]) {
    const Group& group = groups_[group_index];
    if ((group.mask & remaining) != group.mask) {
      continue;
    }
    current.push_back(group_index);
    Search(static_cast<uint16_t>(remaining ^ group.mask),
           score + group.score, current);
    current.pop_back();
  }
}

}  // namespace aletheia
