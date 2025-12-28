#include "Solver.hpp"

#include <iostream>

namespace {
int g_failures = 0;

void ExpectTrue(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
  }
}

void ExpectFalse(bool condition, const char* message) {
  ExpectTrue(!condition, message);
}

std::string PatternFor(const std::string& guess, const std::string& target) {
  aletheia::PackedWord guess_packed = aletheia::WordleSolver::EncodeWord(guess);
  aletheia::PackedWord target_packed =
      aletheia::WordleSolver::EncodeWord(target);
  int pattern = aletheia::WordleSolver::Pattern(guess_packed, target_packed);
  return aletheia::WordleSolver::PatternString(pattern);
}
}  // namespace

int main() {
  {
    std::string guess = "abbey";
    std::string target = "babes";
    std::string pattern = PatternFor(guess, target);
    ExpectTrue(aletheia::WordleSolver::IsConsistent(target, guess, pattern),
               "duplicate letters should be consistent");
    ExpectFalse(aletheia::WordleSolver::IsConsistent("abbey", guess, pattern),
                "yellow letters cannot be in the same position");
  }

  {
    std::string guess = "cigar";
    std::string target = "cigar";
    std::string pattern = PatternFor(guess, target);
    ExpectTrue(aletheia::WordleSolver::IsConsistent(target, guess, pattern),
               "perfect match should be consistent");
    ExpectFalse(aletheia::WordleSolver::IsConsistent(
                    target, guess, "22220"),
                "mismatched pattern should be inconsistent");
  }

  {
    std::string guess = "mamma";
    std::string target = "gamma";
    std::string pattern = PatternFor(guess, target);
    ExpectTrue(aletheia::WordleSolver::IsConsistent(target, guess, pattern),
               "repeated letters should follow Wordle rules");
  }

  if (g_failures > 0) {
    std::cerr << g_failures << " test(s) failed.\n";
    return 1;
  }
  std::cout << "All tests passed.\n";
  return 0;
}
