#include "Solver.hpp"

#ifdef _OPENMP
#include <omp.h>
#endif

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <vector>

#if defined(ALETHEIA_USE_HWY)
#include "hwy/highway.h"
#endif

namespace aletheia {
namespace {
constexpr int kWordLen = 5;
constexpr int kAlphabet = 26;
constexpr int kLetterBits = 5;
constexpr uint32_t kLetterMask = 0x1F;
constexpr int kSolvedPattern = 242;

uint8_t LetterAt(const PackedWord& word, int index) {
  return static_cast<uint8_t>((word.letters >> (index * kLetterBits)) &
                              kLetterMask);
}

}  // namespace

bool WordleSolver::LoadDictionary(const std::string& path) {
  std::ifstream infile(path);
  if (!infile) {
    return false;
  }
  std::vector<std::string> words;
  std::string line;
  while (std::getline(infile, line)) {
    words.push_back(line);
  }
  SetWordList(words);
  return !words_.empty();
}

void WordleSolver::SetWordList(const std::vector<std::string>& words) {
  words_.clear();
  word_storage_.clear();

#if defined(ALETHEIA_USE_WORD_POOL)
  size_t total_bytes = 0;
  for (const auto& word : words) {
    total_bytes += word.size() + 1;
  }
  word_pool_.Reset(total_bytes);

  words_.reserve(words.size());
  for (const auto& word : words) {
    std::string normalized = NormalizeWord(word);
    if (!IsValidWord(normalized)) {
      continue;
    }
    std::string_view stored = word_pool_.Store(normalized);
    if (stored.empty()) {
      continue;
    }
    WordEntry entry;
    entry.text = stored;
    entry.packed = EncodeWord(entry.text);
    words_.push_back(entry);
  }
#else
  words_.reserve(words.size());
  word_storage_.reserve(words.size());
  for (const auto& word : words) {
    std::string normalized = NormalizeWord(word);
    if (!IsValidWord(normalized)) {
      continue;
    }
    word_storage_.push_back(std::move(normalized));
    WordEntry entry;
    entry.text = word_storage_.back();
    entry.packed = EncodeWord(entry.text);
    words_.push_back(entry);
  }
#endif
}

const std::vector<WordEntry>& WordleSolver::words() const { return words_; }

bool WordleSolver::IsValidWord(std::string_view word) {
  if (word.size() != kWordLen) {
    return false;
  }
  for (char c : word) {
    if (c < 'a' || c > 'z') {
      return false;
    }
  }
  return true;
}

std::string WordleSolver::NormalizeWord(std::string_view word) {
  std::string normalized;
  normalized.reserve(word.size());
  for (char c : word) {
    if (c >= 'A' && c <= 'Z') {
      normalized.push_back(static_cast<char>(c - 'A' + 'a'));
    } else if (c >= 'a' && c <= 'z') {
      normalized.push_back(c);
    }
  }
  return normalized;
}

PackedWord WordleSolver::EncodeWord(std::string_view word) {
  PackedWord packed;
  for (int i = 0; i < kWordLen; ++i) {
    uint32_t letter = static_cast<uint32_t>(word[i] - 'a');
    packed.letters |= (letter & kLetterMask) << (i * kLetterBits);
    packed.mask |= 1U << letter;
  }
  return packed;
}

int WordleSolver::Pattern(const PackedWord& guess, const PackedWord& target) {
  std::array<uint8_t, kWordLen> guess_letters{};
  std::array<uint8_t, kWordLen> target_letters{};
  for (int i = 0; i < kWordLen; ++i) {
    guess_letters[i] = LetterAt(guess, i);
    target_letters[i] = LetterAt(target, i);
  }

  std::array<int, kAlphabet> counts{};
  counts.fill(0);
  for (int i = 0; i < kWordLen; ++i) {
    counts[target_letters[i]]++;
  }

  std::array<int, kWordLen> result{};
  result.fill(0);
  for (int i = 0; i < kWordLen; ++i) {
    if (guess_letters[i] == target_letters[i]) {
      result[i] = 2;
      counts[guess_letters[i]]--;
    }
  }

  uint32_t target_mask = target.mask;
  for (int i = 0; i < kWordLen; ++i) {
    if (result[i] != 0) {
      continue;
    }
    uint8_t letter = guess_letters[i];
    if ((target_mask & (1U << letter)) == 0) {
      continue;
    }
    if (counts[letter] > 0) {
      result[i] = 1;
      counts[letter]--;
    }
  }

  int pattern = 0;
  int base = 1;
  for (int i = 0; i < kWordLen; ++i) {
    pattern += result[i] * base;
    base *= 3;
  }
  return pattern;
}

std::string WordleSolver::PatternString(int pattern) {
  std::string out(kWordLen, '0');
  for (int i = 0; i < kWordLen; ++i) {
    out[i] = static_cast<char>('0' + (pattern % 3));
    pattern /= 3;
  }
  return out;
}

bool WordleSolver::IsConsistent(std::string_view candidate,
                                std::string_view guess,
                                std::string_view pattern) {
  if (candidate.size() != kWordLen || guess.size() != kWordLen ||
      pattern.size() != kWordLen) {
    return false;
  }

  std::array<int, kAlphabet> target_counts{};
  target_counts.fill(0);

  for (int i = 0; i < kWordLen; ++i) {
    char p = pattern[i];
    if (p == '2') {
      if (candidate[i] != guess[i]) {
        return false;
      }
    } else if (p == '0' || p == '1') {
      char c = candidate[i];
      if (c < 'a' || c > 'z') {
        return false;
      }
      target_counts[c - 'a']++;
    } else {
      return false;
    }
  }

  for (int i = 0; i < kWordLen; ++i) {
    char p = pattern[i];
    if (p == '2') {
      continue;
    }
    char g = guess[i];
    if (g < 'a' || g > 'z') {
      return false;
    }
    int letter_index = g - 'a';
    if (p == '1') {
      if (candidate[i] == g) {
        return false;
      }
      if (target_counts[letter_index] > 0) {
        target_counts[letter_index]--;
      } else {
        return false;
      }
    } else {
      if (target_counts[letter_index] > 0) {
        return false;
      }
    }
  }

  return true;
}

void WordleSolver::FilterCandidates(const std::vector<WordEntry>& words,
                                    const std::vector<size_t>& remaining,
                                    std::string_view guess,
                                    std::string_view pattern,
                                    std::vector<size_t>* out) {
  if (!out) {
    return;
  }
  out->clear();
  if (guess.size() != kWordLen || pattern.size() != kWordLen) {
    return;
  }

  uint32_t green_mask = 0;
  for (int i = 0; i < kWordLen; ++i) {
    if (pattern[i] == '2') {
      green_mask |= kLetterMask << (i * kLetterBits);
    }
  }

  if (remaining.empty()) {
    return;
  }

#if defined(ALETHEIA_USE_HWY)
  if (green_mask != 0) {
    namespace hn = hwy::HWY_NAMESPACE;

    const PackedWord guess_packed = EncodeWord(guess);
    const uint32_t green_bits = guess_packed.letters & green_mask;

    const hn::ScalableTag<uint32_t> d;
    const size_t lanes = hn::Lanes(d);
    if (lanes > 1) {
      std::vector<uint32_t> packed(lanes);
      std::vector<uint32_t> pass(lanes);
      std::vector<size_t> indices(lanes);

      size_t offset = 0;
      while (offset < remaining.size()) {
        size_t batch = 0;
        for (; batch < lanes && (offset + batch) < remaining.size(); ++batch) {
          size_t index = remaining[offset + batch];
          indices[batch] = index;
          packed[batch] = words[index].packed.letters;
        }
        for (; batch < lanes; ++batch) {
          indices[batch] = static_cast<size_t>(-1);
          packed[batch] = 0;
        }

        auto v = hn::LoadU(d, packed.data());
        auto masked = hn::And(v, hn::Set(d, green_mask));
        auto cmp = hn::Eq(masked, hn::Set(d, green_bits));
        auto pass_vec = hn::IfThenElse(cmp, hn::Set(d, 1u), hn::Zero(d));
        hn::StoreU(pass_vec, d, pass.data());

        for (size_t lane = 0; lane < lanes; ++lane) {
          size_t index = indices[lane];
          if (index == static_cast<size_t>(-1)) {
            continue;
          }
          if (pass[lane] == 0) {
            continue;
          }
          if (IsConsistent(words[index].text, guess, pattern)) {
            out->push_back(index);
          }
        }
        offset += lanes;
      }
      return;
    }
  }
#endif

  for (size_t index : remaining) {
    if (IsConsistent(words[index].text, guess, pattern)) {
      out->push_back(index);
    }
  }
}

size_t WordleSolver::BestGuessIndex(const std::vector<size_t>& candidates,
                                    const std::vector<size_t>& targets,
                                    double* entropy_out) const {
  if (candidates.empty()) {
    if (entropy_out) {
      *entropy_out = 0.0;
    }
    return 0;
  }
  double best_entropy = -std::numeric_limits<double>::infinity();
  size_t best_index = candidates[0];

#ifdef _OPENMP
#pragma omp parallel
  {
    double local_best_entropy = -std::numeric_limits<double>::infinity();
    size_t local_best_index = candidates[0];

#pragma omp for schedule(static)
    for (size_t i = 0; i < candidates.size(); ++i) {
      size_t guess_index = candidates[i];
      double entropy = EntropyForGuess(guess_index, targets);
      if (entropy > local_best_entropy) {
        local_best_entropy = entropy;
        local_best_index = guess_index;
      }
    }

#pragma omp critical
    {
      if (local_best_entropy > best_entropy) {
        best_entropy = local_best_entropy;
        best_index = local_best_index;
      }
    }
  }
#else
  for (size_t guess_index : candidates) {
    double entropy = EntropyForGuess(guess_index, targets);
    if (entropy > best_entropy) {
      best_entropy = entropy;
      best_index = guess_index;
    }
  }
#endif

  if (entropy_out) {
    *entropy_out = best_entropy;
  }
  return best_index;
}

double WordleSolver::EntropyForGuess(
    size_t guess_index,
    const std::vector<size_t>& targets) const {
  if (targets.empty()) {
    return 0.0;
  }
  auto counts = PatternCounts(guess_index, targets);
  double entropy = 0.0;
  const double inv_total = 1.0 / static_cast<double>(targets.size());
  for (int count : counts) {
    if (count == 0) {
      continue;
    }
    double p = count * inv_total;
    entropy -= p * std::log2(p);
  }
  return entropy;
}

std::array<int, WordleSolver::kPatternCount> WordleSolver::PatternCounts(
    size_t guess_index,
    const std::vector<size_t>& targets) const {
  std::array<int, kPatternCount> counts{};
  counts.fill(0);
  const PackedWord& guess = words_[guess_index].packed;
  for (size_t target_index : targets) {
    const PackedWord& target = words_[target_index].packed;
    int pattern = Pattern(guess, target);
    counts[pattern]++;
  }
  return counts;
}

std::string WordleSolver::BestGuess(const std::vector<size_t>& candidates,
                                    const std::vector<size_t>& targets,
                                    double* entropy_out) const {
  size_t best_index = BestGuessIndex(candidates, targets, entropy_out);
  if (candidates.empty()) {
    return {};
  }
  return std::string(words_[best_index].text);
}

std::vector<WordleSolver::Step> WordleSolver::SolveToTarget(
    const std::string& target,
    size_t max_steps) const {
  std::vector<Step> steps;
  if (words_.empty()) {
    return steps;
  }

  std::string normalized = NormalizeWord(target);
  if (!IsValidWord(normalized)) {
    return steps;
  }
  PackedWord target_packed = EncodeWord(normalized);

  std::vector<size_t> remaining(words_.size());
  std::iota(remaining.begin(), remaining.end(), 0);
  std::vector<size_t> next;
  next.reserve(remaining.size());

  for (size_t step = 0; step < max_steps && !remaining.empty(); ++step) {
    double entropy = 0.0;
    size_t best_index = BestGuessIndex(remaining, remaining, &entropy);
    const PackedWord& guess = words_[best_index].packed;
    int pattern = Pattern(guess, target_packed);
    auto counts = PatternCounts(best_index, remaining);

    int pattern_count = counts[pattern];
    double info_bits = 0.0;
    if (pattern_count > 0) {
      double p = static_cast<double>(pattern_count) /
                 static_cast<double>(remaining.size());
      info_bits = -std::log2(p);
    }

    next.clear();
    if (next.capacity() < remaining.size()) {
      next.reserve(remaining.size());
    }
    for (size_t index : remaining) {
      if (Pattern(guess, words_[index].packed) == pattern) {
        next.push_back(index);
      }
    }

    Step entry;
    entry.guess = words_[best_index].text;
    entry.pattern = PatternString(pattern);
    entry.entropy = entropy;
    entry.info_bits = info_bits;
    entry.remaining = remaining.size();
    entry.remaining_after = next.size();
    steps.push_back(std::move(entry));

    if (pattern == kSolvedPattern) {
      break;
    }
    remaining.swap(next);
  }

  return steps;
}

}  // namespace aletheia
