#include "Solver.hpp"

#include <gtest/gtest.h>

namespace {
std::string PatternFor(const std::string& guess, const std::string& target) {
  aletheia::PackedWord guess_packed = aletheia::WordleSolver::EncodeWord(guess);
  aletheia::PackedWord target_packed =
      aletheia::WordleSolver::EncodeWord(target);
  int pattern = aletheia::WordleSolver::Pattern(guess_packed, target_packed);
  return aletheia::WordleSolver::PatternString(pattern);
}
}  // namespace

TEST(WordleConsistency, DuplicateLetterRule) {
  std::string guess = "abbey";
  std::string target = "babes";
  std::string pattern = PatternFor(guess, target);
  EXPECT_TRUE(aletheia::WordleSolver::IsConsistent(target, guess, pattern));
  EXPECT_FALSE(aletheia::WordleSolver::IsConsistent("abbey", guess, pattern));
}

TEST(WordleConsistency, PerfectMatch) {
  std::string guess = "cigar";
  std::string target = "cigar";
  std::string pattern = PatternFor(guess, target);
  EXPECT_TRUE(aletheia::WordleSolver::IsConsistent(target, guess, pattern));
  EXPECT_FALSE(
      aletheia::WordleSolver::IsConsistent(target, guess, "22220"));
}

TEST(WordleConsistency, RepeatedLetters) {
  std::string guess = "mamma";
  std::string target = "gamma";
  std::string pattern = PatternFor(guess, target);
  EXPECT_TRUE(aletheia::WordleSolver::IsConsistent(target, guess, pattern));
}

TEST(WordleConsistency, GrayConsumesDuplicates) {
  std::string guess = "sassy";
  std::string target = "assay";
  std::string pattern = PatternFor(guess, target);
  EXPECT_TRUE(aletheia::WordleSolver::IsConsistent(target, guess, pattern));
  EXPECT_FALSE(aletheia::WordleSolver::IsConsistent("sassy", guess, pattern));
}

TEST(WordleConsistency, YellowPositionInvalid) {
  std::string guess = "stare";
  std::string target = "crate";
  std::string pattern = PatternFor(guess, target);
  EXPECT_TRUE(aletheia::WordleSolver::IsConsistent(target, guess, pattern));
  EXPECT_FALSE(aletheia::WordleSolver::IsConsistent("stare", guess, pattern));
}
