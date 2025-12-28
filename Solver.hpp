#pragma once

#include <Eigen/Dense>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aletheia {

struct PackedWord {
  uint32_t letters = 0;
  uint32_t mask = 0;
};

struct WordEntry {
  PackedWord packed;
  std::string_view text;
};

class WordleSolver {
 public:
  struct Step {
    std::string guess;
    std::string pattern;
    double entropy = 0.0;
    double info_bits = 0.0;
    size_t remaining = 0;
    size_t remaining_after = 0;
  };

  bool LoadDictionary(const std::string& path);
  void SetWordList(const std::vector<std::string>& words);

  const std::vector<WordEntry>& words() const;

  std::string BestGuess(const std::vector<size_t>& candidates,
                        const std::vector<size_t>& targets,
                        double* entropy_out) const;
  std::vector<Step> SolveToTarget(const std::string& target,
                                  size_t max_steps) const;

  static bool IsValidWord(std::string_view word);
  static std::string NormalizeWord(std::string_view word);
  static PackedWord EncodeWord(std::string_view word);
  static int Pattern(const PackedWord& guess, const PackedWord& target);
  static std::string PatternString(int pattern);
  static bool IsConsistent(std::string_view candidate,
                           std::string_view guess,
                           std::string_view pattern);
  static void FilterCandidates(const std::vector<WordEntry>& words,
                               const std::vector<size_t>& remaining,
                               std::string_view guess,
                               std::string_view pattern,
                               std::vector<size_t>* out);

 private:
  struct WordPool {
    static constexpr size_t kAlignment = 64;

    WordPool() = default;
    ~WordPool() { Clear(); }

    WordPool(const WordPool&) = delete;
    WordPool& operator=(const WordPool&) = delete;

    void Reset(size_t total_bytes) {
      Clear();
      if (total_bytes == 0) {
        return;
      }
      size_t aligned = AlignUp(total_bytes, kAlignment);
      storage_.reset(static_cast<char*>(
          ::operator new(aligned, std::align_val_t(kAlignment))));
      capacity_ = aligned;
      offset_ = 0;
    }

    std::string_view Store(std::string_view word) {
      if (!storage_) {
        return {};
      }
      size_t needed = word.size() + 1;
      if (offset_ + needed > capacity_) {
        return {};
      }
      char* dest = storage_.get() + offset_;
      std::memcpy(dest, word.data(), word.size());
      dest[word.size()] = '\0';
      offset_ += needed;
      return std::string_view(dest, word.size());
    }

    size_t capacity() const { return capacity_; }
    size_t used() const { return offset_; }

   private:
    struct AlignedDeleter {
      void operator()(char* ptr) const noexcept {
        if (ptr) {
          ::operator delete(ptr, std::align_val_t(WordPool::kAlignment));
        }
      }
    };

    static size_t AlignUp(size_t size, size_t alignment) {
      return (size + alignment - 1) / alignment * alignment;
    }

    void Clear() {
      storage_.reset();
      capacity_ = 0;
      offset_ = 0;
    }

    std::unique_ptr<char, AlignedDeleter> storage_;
    size_t capacity_ = 0;
    size_t offset_ = 0;
  };

  static constexpr int kWordLen = 5;
  static constexpr int kAlphabet = 26;
  static constexpr int kLetterBits = 5;
  static constexpr uint32_t kLetterMask = 0x1F;
  static constexpr int kPatternCount = 243;

  std::vector<WordEntry> words_;
  WordPool word_pool_;
  std::vector<std::string> word_storage_;

  size_t BestGuessIndex(const std::vector<size_t>& candidates,
                        const std::vector<size_t>& targets,
                        double* entropy_out) const;
  double EntropyForGuess(size_t guess_index,
                         const std::vector<size_t>& targets) const;
  std::array<int, kPatternCount> PatternCounts(
      size_t guess_index,
      const std::vector<size_t>& targets) const;
};

class EmbeddingStore {
 public:
  bool LoadWord2VecBinary(const std::string& path,
                          const std::unordered_set<std::string>& needed);
  bool LoadText(const std::string& path,
                const std::unordered_set<std::string>& needed);
  bool GetVector(const std::string& word, Eigen::VectorXd* out) const;
  int dimension() const { return dimension_; }

 private:
  int dimension_ = 0;
  std::unordered_map<std::string, Eigen::VectorXd> vectors_;
};

class SimilarityEngine {
 public:
  void BuildMatrix(const std::vector<Eigen::VectorXd>& embeddings);
  const Eigen::MatrixXd& matrix() const { return similarity_; }

 private:
  Eigen::MatrixXd similarity_;
};

class ConnectionsSolver {
 public:
  struct Group {
    uint16_t mask = 0;
    double score = 0.0;
  };

  explicit ConnectionsSolver(const Eigen::MatrixXd& similarity);
  std::vector<uint16_t> SolveBestPartition();
  double BestScore() const { return best_score_; }

 private:
  static constexpr int kNodeCount = 16;
  const Eigen::MatrixXd& similarity_;
  std::vector<Group> groups_;
  std::array<std::vector<int>, kNodeCount> groups_by_node_;
  std::vector<int> best_groups_;
  double best_score_ = -std::numeric_limits<double>::infinity();

  void BuildGroups();
  void Search(uint16_t remaining, double score, std::vector<int>& current);
  static int FirstSetBit(uint16_t mask);
};

}  // namespace aletheia
