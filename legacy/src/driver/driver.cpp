#include "thagore/driver/driver.hpp"

#include "thagore/backend/ir_generator.hpp"
#include "thagore/common/diagnostics.hpp"
#include "thagore/frontend/lexer.hpp"
#include "thagore/frontend/parser.hpp"
#include "thagore/frontend/semantic.hpp"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#if __has_include(<print>)
#include <print>
#endif
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace thagore {
namespace {

struct IntentDirective {
  std::string goal {};
  std::string strategy {};
  bool intentEnabled {true};
};

struct IntentPreprocessResult {
  std::string source {};
  std::unordered_map<std::string, IntentDirective> functionDirectives {};
  std::size_t rewritesApplied {0};
};

auto intentTraceEnabled() -> bool {
  const char *env = std::getenv("THAG_INTENT_TRACE");
  return env != nullptr && env[0] != '\0' && env[0] != '0';
}

auto intentExplainEnabled() -> bool {
  const char *env = std::getenv("THAG_INTENT_EXPLAIN");
  return env != nullptr && env[0] != '\0' && env[0] != '0';
}

auto autoOptEnabled() -> bool {
  const char *env = std::getenv("THAG_AUTO_OPT");
  if (env == nullptr || env[0] == '\0') {
    return true;
  }
  return env[0] != '0';
}

auto trimCopy(std::string_view text) -> std::string {
  std::size_t begin = 0;
  while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

auto leadingSpaces(std::string_view line) -> std::size_t {
  std::size_t out = 0;
  while (out < line.size() && line[out] == ' ') {
    ++out;
  }
  return out;
}

auto toLowerCopy(std::string_view text) -> std::string {
  std::string out(text);
  for (char &ch : out) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return out;
}

auto compactNoSpace(std::string_view text) -> std::string {
  std::string out {};
  out.reserve(text.size());
  for (char ch : text) {
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
      continue;
    }
    out.push_back(ch);
  }
  return out;
}

auto normalizedLowerNoSpace(std::string_view text) -> std::string {
  return compactNoSpace(toLowerCopy(text));
}

auto stripOuterParens(std::string text) -> std::string {
  while (text.size() >= 2 && text.front() == '(' && text.back() == ')') {
    int depth = 0;
    bool wrapsAll = true;
    for (std::size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '(') {
        ++depth;
      } else if (text[i] == ')') {
        --depth;
        if (depth == 0 && i + 1 < text.size()) {
          wrapsAll = false;
          break;
        }
      }
      if (depth < 0) {
        wrapsAll = false;
        break;
      }
    }
    if (!wrapsAll || depth != 0) {
      break;
    }
    text = text.substr(1, text.size() - 2);
  }
  return text;
}

auto reverseComparator(std::string_view op) -> std::string_view {
  if (op == "<") {
    return ">";
  }
  if (op == "<=") {
    return ">=";
  }
  if (op == ">") {
    return "<";
  }
  if (op == ">=") {
    return "<=";
  }
  return op;
}

auto extractIfConditionCompact(const std::string &lineCompact) -> std::optional<std::string> {
  if (!(lineCompact.starts_with("if(") && lineCompact.ends_with("):"))) {
    return std::nullopt;
  }
  auto cond = lineCompact.substr(3, lineCompact.size() - 5);
  cond = stripOuterParens(std::move(cond));
  return cond;
}

auto extractWhileConditionCompact(const std::string &lineCompact) -> std::optional<std::string> {
  if (!(lineCompact.starts_with("while(") && lineCompact.ends_with("):"))) {
    return std::nullopt;
  }
  auto cond = lineCompact.substr(6, lineCompact.size() - 8);
  cond = stripOuterParens(std::move(cond));
  return cond;
}

auto matchComparatorEquivalent(
  std::string_view cond,
  std::string_view left,
  std::string_view op,
  std::string_view right
) -> bool {
  const auto direct = std::string(left) + std::string(op) + std::string(right);
  if (cond == direct) {
    return true;
  }
  const auto rev = reverseComparator(op);
  const auto swapped = std::string(right) + std::string(rev) + std::string(left);
  return cond == swapped;
}

auto ifComparatorMatches(
  const std::string &lineCompact,
  std::string_view left,
  std::string_view op,
  std::string_view right
) -> bool {
  const auto cond = extractIfConditionCompact(lineCompact);
  if (!cond.has_value()) {
    return false;
  }
  return matchComparatorEquivalent(*cond, left, op, right);
}

auto whileComparatorMatches(
  const std::string &lineCompact,
  std::string_view left,
  std::string_view op,
  std::string_view right
) -> bool {
  const auto cond = extractWhileConditionCompact(lineCompact);
  if (!cond.has_value()) {
    return false;
  }
  return matchComparatorEquivalent(*cond, left, op, right);
}

auto returnExprMatches(const std::string &lineCompact, std::string_view expr) -> bool {
  if (lineCompact == std::string("return") + std::string(expr)) {
    return true;
  }
  return lineCompact == std::string("return(") + std::string(expr) + ")";
}

auto tryExtractLoopVarAgainstBound(
  const std::string &lineCompact,
  std::string_view bound,
  std::string &loopVarOut
) -> bool {
  const auto condOpt = extractWhileConditionCompact(lineCompact);
  if (!condOpt.has_value()) {
    return false;
  }
  const auto &cond = *condOpt;

  const auto tryDirect = [&](std::string_view op) -> bool {
    const auto suffix = std::string(op) + std::string(bound);
    if (cond.ends_with(suffix) && cond.size() > suffix.size()) {
      loopVarOut = stripOuterParens(cond.substr(0, cond.size() - suffix.size()));
      return !loopVarOut.empty();
    }
    return false;
  };
  if (tryDirect("<") || tryDirect("<=")) {
    return true;
  }

  const auto tryReverse = [&](std::string_view op) -> bool {
    const auto prefix = std::string(bound) + std::string(op);
    if (cond.starts_with(prefix) && cond.size() > prefix.size()) {
      loopVarOut = stripOuterParens(cond.substr(prefix.size()));
      return !loopVarOut.empty();
    }
    return false;
  };
  if (tryReverse(">") || tryReverse(">=")) {
    return true;
  }
  return false;
}

auto splitLinesNormalized(const std::string &source) -> std::vector<std::string> {
  std::vector<std::string> lines {};
  std::string current {};
  for (char ch : source) {
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      lines.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  lines.push_back(current);
  return lines;
}

auto joinLines(const std::vector<std::string> &lines) -> std::string {
  std::string out {};
  for (std::size_t i = 0; i < lines.size(); ++i) {
    out += lines[i];
    if (i + 1 < lines.size()) {
      out.push_back('\n');
    }
  }
  return out;
}

auto parseIntentFunctionName(std::string_view trimmedLine) -> std::string {
  constexpr std::string_view kPrefix = "intent func ";
  if (!trimmedLine.starts_with(kPrefix)) {
    return {};
  }
  const auto rest = trimCopy(trimmedLine.substr(kPrefix.size()));
  const auto lparen = rest.find('(');
  if (lparen == std::string::npos) {
    return {};
  }
  return trimCopy(std::string_view(rest).substr(0, lparen));
}

auto parseFunctionName(std::string_view trimmedLine) -> std::string {
  constexpr std::string_view kPrefix = "func ";
  if (!trimmedLine.starts_with(kPrefix)) {
    return {};
  }
  const auto rest = trimCopy(trimmedLine.substr(kPrefix.size()));
  const auto lparen = rest.find('(');
  if (lparen == std::string::npos) {
    return {};
  }
  return trimCopy(std::string_view(rest).substr(0, lparen));
}

auto parseFirstParamName(std::string_view trimmedLine) -> std::string {
  const auto lparen = trimmedLine.find('(');
  if (lparen == std::string::npos) {
    return {};
  }
  const auto rparen = trimmedLine.find(')', lparen + 1);
  if (rparen == std::string::npos || rparen <= lparen + 1) {
    return {};
  }
  auto inside = trimCopy(trimmedLine.substr(lparen + 1, rparen - lparen - 1));
  if (inside.empty()) {
    return {};
  }
  const auto comma = inside.find(',');
  if (comma != std::string::npos) {
    inside = trimCopy(std::string_view(inside).substr(0, comma));
  }
  const auto colon = inside.find(':');
  if (colon != std::string::npos) {
    inside = trimCopy(std::string_view(inside).substr(0, colon));
  }
  return inside;
}

auto parseParamNames(std::string_view trimmedLine) -> std::vector<std::string> {
  std::vector<std::string> names {};
  const auto lparen = trimmedLine.find('(');
  if (lparen == std::string::npos) {
    return names;
  }
  const auto rparen = trimmedLine.find(')', lparen + 1);
  if (rparen == std::string::npos || rparen <= lparen + 1) {
    return names;
  }
  std::string inside = trimCopy(trimmedLine.substr(lparen + 1, rparen - lparen - 1));
  if (inside.empty()) {
    return names;
  }
  std::size_t start = 0;
  while (start < inside.size()) {
    auto comma = inside.find(',', start);
    if (comma == std::string::npos) {
      comma = inside.size();
    }
    auto token = trimCopy(std::string_view(inside).substr(start, comma - start));
    if (!token.empty()) {
      const auto colon = token.find(':');
      if (colon != std::string::npos) {
        token = trimCopy(std::string_view(token).substr(0, colon));
      }
      if (!token.empty()) {
        names.push_back(token);
      }
    }
    start = comma + 1;
  }
  return names;
}

auto containsWord(std::string_view haystackLower, std::string_view needleLower) -> bool {
  return haystackLower.find(needleLower) != std::string::npos;
}

auto inferAutoGoalByName(std::string_view functionName) -> std::string {
  const auto loweredName = toLowerCopy(functionName);
  if (containsWord(loweredName, "fib")) {
    return "fibonacci_dp";
  }
  if (containsWord(loweredName, "fact")) {
    return "factorial_iterative";
  }
  if (containsWord(loweredName, "pow") || containsWord(loweredName, "exp")) {
    return "power_fast";
  }
  if (containsWord(loweredName, "gcd")) {
    return "gcd_euclid";
  }
  if (containsWord(loweredName, "prime")) {
    return "is_prime_fast";
  }
  if (containsWord(loweredName, "divisor") || containsWord(loweredName, "factor_count")
      || containsWord(loweredName, "count_div")) {
    return "count_divisors_sqrt";
  }
  if (containsWord(loweredName, "sprinkler") || containsWord(loweredName, "interval_cover")
      || containsWord(loweredName, "cover_plants")) {
    return "interval_cover_greedy";
  }
  if (containsWord(loweredName, "binary_search") || containsWord(loweredName, "bsearch")
      || containsWord(loweredName, "lower_bound")) {
    if (containsWord(loweredName, "lower_bound")) {
      return "lower_bound_sorted";
    }
    return "binary_search_sorted";
  }
  if (containsWord(loweredName, "upper_bound")) {
    return "upper_bound_sorted";
  }
  if (containsWord(loweredName, "count_less_equal") || containsWord(loweredName, "count_le")) {
    return "count_less_equal_sorted";
  }
  if (containsWord(loweredName, "count_less") || containsWord(loweredName, "count_lt")) {
    return "count_less_sorted";
  }
  if (containsWord(loweredName, "count_greater_equal") || containsWord(loweredName, "count_ge")) {
    return "count_greater_equal_sorted";
  }
  if (containsWord(loweredName, "count_greater") || containsWord(loweredName, "count_gt")) {
    return "count_greater_sorted";
  }
  if (containsWord(loweredName, "count_equal") || containsWord(loweredName, "count_occurrence")) {
    return "count_equal_sorted";
  }
  if (containsWord(loweredName, "count_not_equal") || containsWord(loweredName, "count_ne")) {
    return "count_not_equal_sorted";
  }
  if (containsWord(loweredName, "count_range") || containsWord(loweredName, "range_count")) {
    return "count_range_sorted";
  }
  if (containsWord(loweredName, "count_outside_range") || containsWord(loweredName, "range_outside_count")) {
    return "count_outside_range_sorted";
  }
  if (containsWord(loweredName, "bit") || containsWord(loweredName, "popcount")
      || containsWord(loweredName, "peel")) {
    return "bit_peel_iterative";
  }
  if (containsWord(loweredName, "sum_cube") || containsWord(loweredName, "sum_cubes")) {
    return "sum_cubes_formula";
  }
  if (containsWord(loweredName, "sum_even_square") || containsWord(loweredName, "sum_squares_even")) {
    return "sum_even_squares_formula";
  }
  if (containsWord(loweredName, "sum_odd_square") || containsWord(loweredName, "sum_squares_odd")) {
    return "sum_odd_squares_formula";
  }
  if (containsWord(loweredName, "sum_even_cube") || containsWord(loweredName, "sum_cubes_even")) {
    return "sum_even_cubes_formula";
  }
  if (containsWord(loweredName, "sum_odd_cube") || containsWord(loweredName, "sum_cubes_odd")) {
    return "sum_odd_cubes_formula";
  }
  if (containsWord(loweredName, "sum_even")) {
    return "sum_even_formula";
  }
  if (containsWord(loweredName, "sum_odd")) {
    return "sum_odd_formula";
  }
  if (containsWord(loweredName, "sqrt")) {
    return "sqrt_bounded_loop";
  }
  if (containsWord(loweredName, "sum") || containsWord(loweredName, "reduce")) {
    return "reduce_sum";
  }
  if (containsWord(loweredName, "search") || containsWord(loweredName, "find")) {
    return "search_element";
  }
  return {};
}

enum class IntentRewriteKind {
  None,
  FibonacciIterative,
  FactorialIterative,
  PowerBinaryExp,
  GcdEuclidModulo,
  PrimeCheckSqrt,
  DivisorCountSqrt,
  IntervalCoverGreedy,
  BinarySearchSorted,
  LowerBoundSorted,
  UpperBoundSorted,
  CountLessSorted,
  CountLessEqualSorted,
  CountGreaterSorted,
  CountGreaterEqualSorted,
  CountEqualSorted,
  CountNotEqualSorted,
  CountRangeSorted,
  CountOutsideRangeSorted,
  BitPeelIterative,
  ReduceSumFormula,
  SumSquaresFormula,
  SumCubesFormula,
  SumEvenSquaresFormula,
  SumOddSquaresFormula,
  SumEvenCubesFormula,
  SumOddCubesFormula,
  SumEvenFormula,
  SumOddFormula,
  SqrtBoundedLoop,
  SearchIdentity,
};

auto rewriteRuleId(IntentRewriteKind kind) -> std::string_view {
  switch (kind) {
    case IntentRewriteKind::FibonacciIterative:
      return "rule.dp.fib.iterative.v1";
    case IntentRewriteKind::FactorialIterative:
      return "rule.math.factorial.loop.v1";
    case IntentRewriteKind::PowerBinaryExp:
      return "rule.math.pow.binary_exp.v1";
    case IntentRewriteKind::GcdEuclidModulo:
      return "rule.math.gcd.euclid.v1";
    case IntentRewriteKind::PrimeCheckSqrt:
      return "rule.number.prime.sqrt.v1";
    case IntentRewriteKind::DivisorCountSqrt:
      return "rule.number.divisors.sqrt.v1";
    case IntentRewriteKind::IntervalCoverGreedy:
      return "rule.greedy.interval_cover.v1";
    case IntentRewriteKind::BinarySearchSorted:
      return "rule.search.binary.sorted.v1";
    case IntentRewriteKind::LowerBoundSorted:
      return "rule.search.lower_bound.sorted.v1";
    case IntentRewriteKind::UpperBoundSorted:
      return "rule.search.upper_bound.sorted.v1";
    case IntentRewriteKind::CountLessSorted:
      return "rule.search.count_less.sorted.v1";
    case IntentRewriteKind::CountLessEqualSorted:
      return "rule.search.count_less_equal.sorted.v1";
    case IntentRewriteKind::CountGreaterSorted:
      return "rule.search.count_greater.sorted.v1";
    case IntentRewriteKind::CountGreaterEqualSorted:
      return "rule.search.count_greater_equal.sorted.v1";
    case IntentRewriteKind::CountEqualSorted:
      return "rule.search.count_equal.sorted.v1";
    case IntentRewriteKind::CountNotEqualSorted:
      return "rule.search.count_not_equal.sorted.v1";
    case IntentRewriteKind::CountRangeSorted:
      return "rule.search.count_range.sorted.v1";
    case IntentRewriteKind::CountOutsideRangeSorted:
      return "rule.search.count_outside_range.sorted.v1";
    case IntentRewriteKind::BitPeelIterative:
      return "rule.number.bit_peel.iterative.v1";
    case IntentRewriteKind::ReduceSumFormula:
      return "rule.math.sum.formula.v1";
    case IntentRewriteKind::SumSquaresFormula:
      return "rule.math.sum_squares.formula.v1";
    case IntentRewriteKind::SumCubesFormula:
      return "rule.math.sum_cubes.formula.v1";
    case IntentRewriteKind::SumEvenSquaresFormula:
      return "rule.math.sum_even_squares.formula.v1";
    case IntentRewriteKind::SumOddSquaresFormula:
      return "rule.math.sum_odd_squares.formula.v1";
    case IntentRewriteKind::SumEvenCubesFormula:
      return "rule.math.sum_even_cubes.formula.v1";
    case IntentRewriteKind::SumOddCubesFormula:
      return "rule.math.sum_odd_cubes.formula.v1";
    case IntentRewriteKind::SumEvenFormula:
      return "rule.math.sum_even.formula.v1";
    case IntentRewriteKind::SumOddFormula:
      return "rule.math.sum_odd.formula.v1";
    case IntentRewriteKind::SqrtBoundedLoop:
      return "rule.math.sqrt.newton.v1";
    case IntentRewriteKind::SearchIdentity:
      return "rule.search.identity.bounds.v1";
    case IntentRewriteKind::None:
      return "rule.none";
  }
  return "rule.none";
}

struct FunctionBodySignals {
  IntentRewriteKind kind {IntentRewriteKind::None};
  std::string detectedGoal {};
};

auto inferRewriteKindFromBody(
  std::string_view functionName,
  std::string_view headerLine,
  const std::vector<std::string> &sourceLines,
  std::size_t bodyStart,
  std::size_t bodyEnd
) -> FunctionBodySignals {
  FunctionBodySignals out {};
  const auto params = parseParamNames(headerLine);
  if (params.empty()) {
    return out;
  }

  std::vector<std::string> trimmed {};
  std::vector<std::string> compact {};
  for (std::size_t i = bodyStart; i < bodyEnd; ++i) {
    const auto t = trimCopy(sourceLines[i]);
    if (t.empty()) {
      continue;
    }
    trimmed.push_back(t);
    compact.push_back(compactNoSpace(t));
  }

  if (params.size() == 1) {
    const auto n = params[0];
    const auto nCompact = compactNoSpace(n);
    const auto fnCompact = compactNoSpace(functionName);

    bool hasFibBase = false;
    bool hasFibRec = false;
    for (std::size_t i = 0; i < compact.size(); ++i) {
      const auto &c = compact[i];
      if (c == "if(" + nCompact + "<2):" || c == "if(" + nCompact + "<=1):") {
        if (i + 1 < compact.size() && compact[i + 1] == "return" + nCompact) {
          hasFibBase = true;
        }
      }
      if (c == "return" + fnCompact + "(" + nCompact + "-1)+" + fnCompact + "(" + nCompact + "-2)") {
        hasFibRec = true;
      }
    }
    if (hasFibBase && hasFibRec) {
      out.kind = IntentRewriteKind::FibonacciIterative;
      out.detectedGoal = "fibonacci_dp";
      return out;
    }

    bool hasFactBase = false;
    bool hasFactRec = false;
    for (std::size_t i = 0; i < compact.size(); ++i) {
      const auto &c = compact[i];
      if (c == "if(" + nCompact + "<2):" || c == "if(" + nCompact + "<=1):") {
        if (i + 1 < compact.size() && compact[i + 1] == "return1") {
          hasFactBase = true;
        }
      }
      if (c == "return" + nCompact + "*" + fnCompact + "(" + nCompact + "-1)"
          || c == "return" + fnCompact + "(" + nCompact + "-1)*" + nCompact) {
        hasFactRec = true;
      }
    }
    if (hasFactBase && hasFactRec) {
      out.kind = IntentRewriteKind::FactorialIterative;
      out.detectedGoal = "factorial_iterative";
      return out;
    }

    bool hasSqrtPattern = false;
    bool hasIStep = false;
    for (const auto &c : compact) {
      if (c == "while(((i+1)*(i+1))<=" + nCompact + "):") {
        hasSqrtPattern = true;
      }
      if (c == "i=i+1") {
        hasIStep = true;
      }
    }
    if (hasSqrtPattern && hasIStep) {
      out.kind = IntentRewriteKind::SqrtBoundedLoop;
      out.detectedGoal = "sqrt_bounded_loop";
      return out;
    }

    std::string primeLoopVar {};
    bool hasPrimeDivCheck = false;
    bool hasPrimeLoopInc = false;
    bool hasPrimeReturn0 = false;
    bool hasPrimeReturn1 = false;
    for (const auto &c : compact) {
      if (tryExtractLoopVarAgainstBound(c, nCompact, primeLoopVar)) {}
      if (!primeLoopVar.empty()) {
        if (c == primeLoopVar + "=" + primeLoopVar + "+1") {
          hasPrimeLoopInc = true;
        }
        if ((c == "if((" + nCompact + "%"+ primeLoopVar + ")==0):")
            || (c == "if((" + nCompact + "-(" + nCompact + "/" + primeLoopVar + ")*" + primeLoopVar + ")==0):")
            || (c == "if(((" + nCompact + "/" + primeLoopVar + ")*" + primeLoopVar + ")==" + nCompact + "):")) {
          hasPrimeDivCheck = true;
        }
      }
      if (c == "return0") {
        hasPrimeReturn0 = true;
      }
      if (c == "return1") {
        hasPrimeReturn1 = true;
      }
    }
    if (!primeLoopVar.empty() && hasPrimeDivCheck && hasPrimeLoopInc && hasPrimeReturn0 && hasPrimeReturn1) {
      out.kind = IntentRewriteKind::PrimeCheckSqrt;
      out.detectedGoal = "is_prime_fast";
      return out;
    }

    std::string divLoopVar {};
    std::string divCountVar {};
    bool hasDivCheck = false;
    bool hasDivInc = false;
    bool hasDivCountInc = false;
    for (const auto &c : compact) {
      if (tryExtractLoopVarAgainstBound(c, nCompact, divLoopVar)) {}
      if (c.starts_with("return")) {
        const auto candidate = c.substr(6);
        if (!candidate.empty() && std::isalpha(static_cast<unsigned char>(candidate[0])) != 0) {
          divCountVar = candidate;
        }
      }
      if (!divLoopVar.empty() && c == divLoopVar + "=" + divLoopVar + "+1") {
        hasDivInc = true;
      }
    }
    for (const auto &c : compact) {
      if (!divLoopVar.empty()) {
        if ((c == "if((" + nCompact + "%"+ divLoopVar + ")==0):")
            || (c == "if((" + nCompact + "-(" + nCompact + "/" + divLoopVar + ")*" + divLoopVar + ")==0):")
            || (c == "if(((" + nCompact + "/" + divLoopVar + ")*" + divLoopVar + ")==" + nCompact + "):")) {
          hasDivCheck = true;
        }
      }
      if (!divCountVar.empty() && (c == divCountVar + "=" + divCountVar + "+1")) {
        hasDivCountInc = true;
      }
    }
    if (!divLoopVar.empty() && !divCountVar.empty() && hasDivCheck && hasDivInc && hasDivCountInc) {
      out.kind = IntentRewriteKind::DivisorCountSqrt;
      out.detectedGoal = "count_divisors_sqrt";
      return out;
    }

    std::string cubeLoopVar {};
    bool hasCubeStep = false;
    bool hasCubeEvenCond = false;
    bool hasCubeOddCond = false;
    bool hasCubeInc = false;
    bool hasCubeReturn = false;
    for (const auto &c : compact) {
      if (tryExtractLoopVarAgainstBound(c, nCompact, cubeLoopVar)) {}
      if (!cubeLoopVar.empty()) {
        if (c.find("+" + cubeLoopVar + "*" + cubeLoopVar + "*" + cubeLoopVar) != std::string::npos
            && c.find('=') != std::string::npos) {
          hasCubeStep = true;
        }
        if (c == "if((" + cubeLoopVar + "%2)==0):"
            || c == "if((" + cubeLoopVar + "-(" + cubeLoopVar + "/2)*2)==0):") {
          hasCubeEvenCond = true;
        }
        if (c == "if((" + cubeLoopVar + "%2)==1):"
            || c == "if((" + cubeLoopVar + "-(" + cubeLoopVar + "/2)*2)==1):") {
          hasCubeOddCond = true;
        }
        if (c == cubeLoopVar + "=" + cubeLoopVar + "+1") {
          hasCubeInc = true;
        }
      }
      if (c.starts_with("return")) {
        hasCubeReturn = true;
      }
    }
    if (!cubeLoopVar.empty() && hasCubeStep && hasCubeInc && hasCubeReturn && hasCubeEvenCond) {
      out.kind = IntentRewriteKind::SumEvenCubesFormula;
      out.detectedGoal = "sum_even_cubes_formula";
      return out;
    }
    if (!cubeLoopVar.empty() && hasCubeStep && hasCubeInc && hasCubeReturn && hasCubeOddCond) {
      out.kind = IntentRewriteKind::SumOddCubesFormula;
      out.detectedGoal = "sum_odd_cubes_formula";
      return out;
    }
    if (!cubeLoopVar.empty() && hasCubeStep && hasCubeInc && hasCubeReturn) {
      out.kind = IntentRewriteKind::SumCubesFormula;
      out.detectedGoal = "sum_cubes_formula";
      return out;
    }

    std::string squareLoopVar {};
    bool hasSquareStep = false;
    bool hasSquareEvenCond = false;
    bool hasSquareOddCond = false;
    bool hasSquareInc = false;
    bool hasSquareReturn = false;
    for (const auto &c : compact) {
      if (tryExtractLoopVarAgainstBound(c, nCompact, squareLoopVar)) {}
      if (!squareLoopVar.empty()) {
        if (c.find("+" + squareLoopVar + "*" + squareLoopVar) != std::string::npos
            && c.find("+" + squareLoopVar + "*" + squareLoopVar + "*" + squareLoopVar) == std::string::npos
            && c.find('=') != std::string::npos) {
          hasSquareStep = true;
        }
        if (c == "if((" + squareLoopVar + "%2)==0):"
            || c == "if((" + squareLoopVar + "-(" + squareLoopVar + "/2)*2)==0):") {
          hasSquareEvenCond = true;
        }
        if (c == "if((" + squareLoopVar + "%2)==1):"
            || c == "if((" + squareLoopVar + "-(" + squareLoopVar + "/2)*2)==1):") {
          hasSquareOddCond = true;
        }
        if (c == squareLoopVar + "=" + squareLoopVar + "+1") {
          hasSquareInc = true;
        }
      }
      if (c.starts_with("return")) {
        hasSquareReturn = true;
      }
    }
    if (!squareLoopVar.empty() && hasSquareStep && hasSquareInc && hasSquareReturn && hasSquareEvenCond) {
      out.kind = IntentRewriteKind::SumEvenSquaresFormula;
      out.detectedGoal = "sum_even_squares_formula";
      return out;
    }
    if (!squareLoopVar.empty() && hasSquareStep && hasSquareInc && hasSquareReturn && hasSquareOddCond) {
      out.kind = IntentRewriteKind::SumOddSquaresFormula;
      out.detectedGoal = "sum_odd_squares_formula";
      return out;
    }
    if (!squareLoopVar.empty() && hasSquareStep && hasSquareInc && hasSquareReturn) {
      out.kind = IntentRewriteKind::SumSquaresFormula;
      out.detectedGoal = "sum_squares_formula";
      return out;
    }

    std::string loopVar {};
    bool hasSumStep = false;
    bool hasIncStep = false;
    bool hasEvenCond = false;
    bool hasOddCond = false;
    bool hasReturn = false;
    for (const auto &c : compact) {
      if (tryExtractLoopVarAgainstBound(c, nCompact, loopVar)) {}
      if (!loopVar.empty()) {
        if (c.find("+" + loopVar) != std::string::npos && c.find('=') != std::string::npos) {
          hasSumStep = true;
        }
        if (c == loopVar + "=" + loopVar + "+1") {
          hasIncStep = true;
        }
        if (c == "if((" + loopVar + "%2)==0):"
            || c == "if((" + loopVar + "-(" + loopVar + "/2)*2)==0):") {
          hasEvenCond = true;
        }
        if (c == "if((" + loopVar + "%2)==1):"
            || c == "if((" + loopVar + "-(" + loopVar + "/2)*2)==1):") {
          hasOddCond = true;
        }
      }
      if (c.starts_with("return")) {
        hasReturn = true;
      }
    }
    if (!loopVar.empty() && hasSumStep && hasIncStep && hasReturn && hasEvenCond) {
      out.kind = IntentRewriteKind::SumEvenFormula;
      out.detectedGoal = "sum_even_formula";
      return out;
    }
    if (!loopVar.empty() && hasSumStep && hasIncStep && hasReturn && hasOddCond) {
      out.kind = IntentRewriteKind::SumOddFormula;
      out.detectedGoal = "sum_odd_formula";
      return out;
    }
    if (!loopVar.empty() && hasSumStep && hasIncStep && hasReturn) {
      out.kind = IntentRewriteKind::ReduceSumFormula;
      out.detectedGoal = "reduce_sum";
      return out;
    }

  }

  if (params.size() >= 2) {
    const auto first = compactNoSpace(params[0]);
    const auto second = compactNoSpace(params[1]);
    const auto fnCompact = compactNoSpace(functionName);

    bool hasPeelBase = false;
    bool hasPeelRec = false;
    for (std::size_t i = 0; i < compact.size(); ++i) {
      const auto &c = compact[i];
      if (c == "if(" + second + "==0):") {
        if (i + 1 < compact.size() && compact[i + 1] == "return" + first) {
          hasPeelBase = true;
        }
      }
      if (c.starts_with("return" + fnCompact + "(") && c.find("," + second + "-1)") != std::string::npos
          && (c.find(first + "/2") != std::string::npos || c.find("floor(" + first + "/2)") != std::string::npos)
          && (c.find(first + "%2") != std::string::npos
              || c.find(first + "-(" + first + "/2)*2") != std::string::npos
              || c.find(first + "-((" + first + "/2)*2)") != std::string::npos)) {
        hasPeelRec = true;
      }
    }
    if (hasPeelBase && hasPeelRec) {
      out.kind = IntentRewriteKind::BitPeelIterative;
      out.detectedGoal = "bit_peel_iterative";
      return out;
    }

    std::string powerLoopVar {};
    std::string powerReturnVar {};
    for (const auto &c : compact) {
      if (tryExtractLoopVarAgainstBound(c, second, powerLoopVar)) {}
      if (c.starts_with("return")) {
        const auto candidate = c.substr(6);
        if (!candidate.empty() && std::isalpha(static_cast<unsigned char>(candidate[0])) != 0) {
          powerReturnVar = candidate;
        }
      }
    }
    bool hasPowMul = false;
    bool hasPowInc = false;
    bool hasPowInit = false;
    for (const auto &c : compact) {
      if (!powerReturnVar.empty()) {
        if (c == powerReturnVar + "=1" || c == "let" + powerReturnVar + "=1") {
          hasPowInit = true;
        }
        if (c == powerReturnVar + "=" + powerReturnVar + "*" + first
            || c == powerReturnVar + "=" + first + "*" + powerReturnVar) {
          hasPowMul = true;
        }
      }
      if (!powerLoopVar.empty() && c == powerLoopVar + "=" + powerLoopVar + "+1") {
        hasPowInc = true;
      }
    }
    if (!powerLoopVar.empty() && !powerReturnVar.empty() && hasPowInit && hasPowMul && hasPowInc) {
      out.kind = IntentRewriteKind::PowerBinaryExp;
      out.detectedGoal = "power_fast";
      return out;
    }

    std::string gcdA {};
    std::string gcdB {};
    bool hasGcdWhile = false;
    bool hasGcdCond = false;
    bool hasGcdSub = false;
    bool hasGcdRet = false;
    for (const auto &c : compact) {
      if (c.starts_with("while(") && c.ends_with("):")) {
        const auto body = c.substr(6, c.size() - 8);
        const auto p = body.find("!=");
        if (p != std::string::npos && p > 0 && p + 2 < body.size()) {
          gcdA = body.substr(0, p);
          gcdB = body.substr(p + 2);
        }
      }
      if (!gcdA.empty() && !gcdB.empty()
          && (c == "while(" + gcdA + "!=" + gcdB + "):" || c == "while(" + gcdB + "!=" + gcdA + "):")) {
        hasGcdWhile = true;
      }
      if (!gcdA.empty() && !gcdB.empty()
          && (c == "if(" + gcdA + ">" + gcdB + "):" || c == "if(" + gcdB + ">" + gcdA + "):")) {
        hasGcdCond = true;
      }
      if (!gcdA.empty() && !gcdB.empty()
          && (c == gcdA + "=" + gcdA + "-" + gcdB || c == gcdB + "=" + gcdB + "-" + gcdA)) {
        hasGcdSub = true;
      }
      if (!gcdA.empty() && !gcdB.empty() && (c == "return" + gcdA || c == "return" + gcdB)) {
        hasGcdRet = true;
      }
    }
    if (hasGcdWhile && hasGcdCond && hasGcdSub && hasGcdRet) {
      out.kind = IntentRewriteKind::GcdEuclidModulo;
      out.detectedGoal = "gcd_euclid";
      return out;
    }

    if (params.size() >= 3) {
      const auto arrParam = compactNoSpace(params[0]);
      const auto nParam = compactNoSpace(params[1]);
      const auto targetParam = compactNoSpace(params[2]);

      std::string loopVar {};
      bool hasLowerBoundIf = false;
      bool hasLowerBoundRetLoop = false;
      bool hasLowerBoundRetN = false;
      bool hasEqTarget = false;
      bool hasGtTarget = false;
      bool hasUpperBoundIf = false;
      bool hasUpperBoundRetLoop = false;
      bool hasUpperBoundRetN = false;
      bool hasRetNMinusLoop = false;
      bool hasRetZero = false;
      bool hasRetLoop = false;
      bool hasRetNeg1 = false;
      bool hasInc = false;
      std::string countLeftVar {};
      std::string countRightVar {};
      bool hasCountLeftWhile = false;
      bool hasCountRightWhile = false;
      bool hasCountRightInit = false;
      bool hasCountReturn = false;
      bool hasCountNotEqualReturn = false;
      for (const auto &c : compact) {
        if (tryExtractLoopVarAgainstBound(c, nParam, loopVar)) {}
        const auto leftSig = "<" + nParam + ")and(" + arrParam + "[";
        const auto rightSig = "<" + nParam + ")and(" + arrParam + "[";
        if (c.starts_with("while((")
            && c.find(leftSig) != std::string::npos
            && c.find("<" + targetParam + ")):" ) != std::string::npos) {
          const auto leftPos = std::size_t {7};
          const auto endPos = c.find("<" + nParam + ")");
          if (endPos != std::string::npos && endPos > leftPos) {
            countLeftVar = c.substr(leftPos, endPos - leftPos);
            hasCountLeftWhile = true;
          }
        }
        if (c.starts_with("while((")
            && c.find(rightSig) != std::string::npos
            && c.find("==" + targetParam + ")):" ) != std::string::npos) {
          const auto leftPos = std::size_t {7};
          const auto endPos = c.find("<" + nParam + ")");
          if (endPos != std::string::npos && endPos > leftPos) {
            countRightVar = c.substr(leftPos, endPos - leftPos);
            hasCountRightWhile = true;
          }
        }
        if (!loopVar.empty()) {
          if (ifComparatorMatches(c, arrParam + "[" + loopVar + "]", ">=", targetParam)) {
            hasLowerBoundIf = true;
          }
          if (ifComparatorMatches(c, arrParam + "[" + loopVar + "]", "==", targetParam)) {
            hasEqTarget = true;
          }
          if (ifComparatorMatches(c, arrParam + "[" + loopVar + "]", ">", targetParam)) {
            hasGtTarget = true;
            hasUpperBoundIf = true;
          }
          if (returnExprMatches(c, loopVar)) {
            hasUpperBoundRetLoop = true;
            hasLowerBoundRetLoop = true;
            hasRetLoop = true;
          }
          if (returnExprMatches(c, nParam + "-" + loopVar)) {
            hasRetNMinusLoop = true;
          }
          if (c == loopVar + "=" + loopVar + "+1") {
            hasInc = true;
          }
        }
        if (!countLeftVar.empty() && !countRightVar.empty()
            && (c == "let" + countRightVar + "=" + countLeftVar
                || c == countRightVar + "=" + countLeftVar)) {
          hasCountRightInit = true;
        }
        if (!countLeftVar.empty() && !countRightVar.empty()
            && returnExprMatches(c, countRightVar + "-" + countLeftVar)) {
          hasCountReturn = true;
        }
        if (!countLeftVar.empty() && !countRightVar.empty()) {
          if (returnExprMatches(c, nParam + "-(" + countRightVar + "-" + countLeftVar + ")")
              || returnExprMatches(c, nParam + "-" + countRightVar + "+" + countLeftVar)) {
            hasCountNotEqualReturn = true;
          }
        }
        if (returnExprMatches(c, nParam)) {
          hasUpperBoundRetN = true;
          hasLowerBoundRetN = true;
        }
        if (c == "return-1") {
          hasRetNeg1 = true;
        }
        if (c == "return0") {
          hasRetZero = true;
        }
      }
      if (!loopVar.empty() && hasLowerBoundIf && hasLowerBoundRetLoop && hasInc && hasLowerBoundRetN) {
        out.kind = IntentRewriteKind::LowerBoundSorted;
        out.detectedGoal = "lower_bound_sorted";
        return out;
      }
      if (!loopVar.empty() && hasUpperBoundIf && hasUpperBoundRetLoop && hasInc && hasUpperBoundRetN) {
        out.kind = IntentRewriteKind::UpperBoundSorted;
        out.detectedGoal = "upper_bound_sorted";
        return out;
      }
      if (!loopVar.empty() && hasUpperBoundIf && hasRetNMinusLoop && hasInc && hasRetZero) {
        out.kind = IntentRewriteKind::CountGreaterSorted;
        out.detectedGoal = "count_greater_sorted";
        return out;
      }
      if (!loopVar.empty() && hasLowerBoundIf && hasRetNMinusLoop && hasInc && hasRetZero) {
        out.kind = IntentRewriteKind::CountGreaterEqualSorted;
        out.detectedGoal = "count_greater_equal_sorted";
        return out;
      }
      if (!countLeftVar.empty() && !countRightVar.empty() && !hasCountRightInit) {
        for (const auto &c : compact) {
          if (c == "let" + countRightVar + "=" + countLeftVar || c == countRightVar + "=" + countLeftVar) {
            hasCountRightInit = true;
            break;
          }
        }
      }
      if (!countLeftVar.empty() && !countRightVar.empty()
          && hasCountLeftWhile && hasCountRightWhile && hasCountRightInit && hasCountReturn) {
        out.kind = IntentRewriteKind::CountEqualSorted;
        out.detectedGoal = "count_equal_sorted";
        return out;
      }
      if (!countLeftVar.empty() && !countRightVar.empty()
          && hasCountLeftWhile && hasCountRightWhile && hasCountRightInit && hasCountNotEqualReturn) {
        out.kind = IntentRewriteKind::CountNotEqualSorted;
        out.detectedGoal = "count_not_equal_sorted";
        return out;
      }
      if (!countLeftVar.empty() && !countRightVar.empty()
          && hasCountLeftWhile && hasCountRightWhile && hasCountNotEqualReturn) {
        out.kind = IntentRewriteKind::CountNotEqualSorted;
        out.detectedGoal = "count_not_equal_sorted";
        return out;
      }
      if (!loopVar.empty() && hasEqTarget && hasGtTarget && hasRetLoop && hasInc && hasRetNeg1) {
        out.kind = IntentRewriteKind::BinarySearchSorted;
        out.detectedGoal = "binary_search_sorted";
        return out;
      }
    }

    if (params.size() >= 4) {
      const auto arrParam = compactNoSpace(params[0]);
      const auto nParam = compactNoSpace(params[1]);
      const auto loParam = compactNoSpace(params[2]);
      const auto hiParam = compactNoSpace(params[3]);
      std::string leftVar {};
      std::string rightVar {};
      bool hasLeftWhile = false;
      bool hasRightWhile = false;
      bool hasRightInit = false;
      bool hasRangeReturn = false;
      bool hasOutsideRangeReturn = false;
      std::string scanVar {};
      std::string outsideCountVar {};
      bool hasOutsideIf = false;
      bool hasOutsideInc = false;
      bool hasOutsideReturn = false;
      for (const auto &c : compact) {
        if (tryExtractLoopVarAgainstBound(c, nParam, scanVar)) {}
        const auto sharedSig = "<" + nParam + ")and(" + arrParam + "[";
        if (c.starts_with("while((")
            && c.find(sharedSig) != std::string::npos
            && c.find("<" + loParam + ")):" ) != std::string::npos) {
          const auto leftPos = std::size_t {7};
          const auto endPos = c.find("<" + nParam + ")");
          if (endPos != std::string::npos && endPos > leftPos) {
            leftVar = c.substr(leftPos, endPos - leftPos);
            hasLeftWhile = true;
          }
        }
        if (c.starts_with("while((")
            && c.find(sharedSig) != std::string::npos
            && c.find("<=" + hiParam + ")):" ) != std::string::npos) {
          const auto leftPos = std::size_t {7};
          const auto endPos = c.find("<" + nParam + ")");
          if (endPos != std::string::npos && endPos > leftPos) {
            rightVar = c.substr(leftPos, endPos - leftPos);
            hasRightWhile = true;
          }
        }
        if (!leftVar.empty() && !rightVar.empty()
            && (c == "let" + rightVar + "=" + leftVar || c == rightVar + "=" + leftVar)) {
          hasRightInit = true;
        }
        if (!leftVar.empty() && !rightVar.empty() && c == "return" + rightVar + "-" + leftVar) {
          hasRangeReturn = true;
        }
        if (!leftVar.empty() && !rightVar.empty()) {
          if (returnExprMatches(c, nParam + "-(" + rightVar + "-" + leftVar + ")")
              || returnExprMatches(c, nParam + "-" + rightVar + "+" + leftVar)) {
            hasOutsideRangeReturn = true;
          }
        }

        if (!scanVar.empty()) {
          const auto cond = extractIfConditionCompact(c);
          if (cond.has_value() && cond->find("or") != std::string::npos) {
            const auto leftA = std::string(arrParam) + "[" + scanVar + "]<" + std::string(loParam);
            const auto leftB = std::string(loParam) + ">" + std::string(arrParam) + "[" + scanVar + "]";
            const auto rightA = std::string(arrParam) + "[" + scanVar + "]>" + std::string(hiParam);
            const auto rightB = std::string(hiParam) + "<" + std::string(arrParam) + "[" + scanVar + "]";
            const bool hasLoCmp = cond->find(leftA) != std::string::npos || cond->find(leftB) != std::string::npos;
            const bool hasHiCmp = cond->find(rightA) != std::string::npos || cond->find(rightB) != std::string::npos;
            if (hasLoCmp && hasHiCmp) {
              hasOutsideIf = true;
            }
          }
        }

        const auto eqPos = c.find('=');
        if (eqPos != std::string::npos && eqPos > 0 && eqPos + 1 < c.size()) {
          const auto lhs = c.substr(0, eqPos);
          const auto rhs = c.substr(eqPos + 1);
          if (rhs == lhs + "+1") {
            if (outsideCountVar.empty()) {
              outsideCountVar = lhs;
            }
            if (lhs == outsideCountVar) {
              hasOutsideInc = true;
            }
          }
        }
        if (!outsideCountVar.empty() && returnExprMatches(c, outsideCountVar)) {
          hasOutsideReturn = true;
        }
      }
      if (!leftVar.empty() && !rightVar.empty() && !hasRightInit) {
        for (const auto &c : compact) {
          if (c == "let" + rightVar + "=" + leftVar || c == rightVar + "=" + leftVar) {
            hasRightInit = true;
            break;
          }
        }
      }
      if (!leftVar.empty() && !rightVar.empty() && hasLeftWhile && hasRightWhile && hasRightInit && hasRangeReturn) {
        out.kind = IntentRewriteKind::CountRangeSorted;
        out.detectedGoal = "count_range_sorted";
        return out;
      }
      if (!leftVar.empty() && !rightVar.empty()
          && hasLeftWhile && hasRightWhile && hasRightInit && hasOutsideRangeReturn) {
        out.kind = IntentRewriteKind::CountOutsideRangeSorted;
        out.detectedGoal = "count_outside_range_sorted";
        return out;
      }
      if (!scanVar.empty() && hasOutsideIf && hasOutsideInc && hasOutsideReturn) {
        out.kind = IntentRewriteKind::CountOutsideRangeSorted;
        out.detectedGoal = "count_outside_range_sorted";
        return out;
      }
    }

    const auto n = first;
    const auto target = second;
    std::string loopVar {};
    bool hasIfEq = false;
    bool hasRetLoop = false;
    bool hasRetNeg1 = false;
    for (const auto &c : compact) {
      if (tryExtractLoopVarAgainstBound(c, n, loopVar)) {}
      if (!loopVar.empty() && ifComparatorMatches(c, loopVar, "==", target)) {
        hasIfEq = true;
      }
      if (!loopVar.empty() && returnExprMatches(c, loopVar)) {
        hasRetLoop = true;
      }
      if (c == "return-1") {
        hasRetNeg1 = true;
      }
    }
    if (!loopVar.empty() && hasIfEq && hasRetLoop && hasRetNeg1) {
      out.kind = IntentRewriteKind::SearchIdentity;
      out.detectedGoal = "search_element";
      return out;
    }
  }

  return out;
}

auto selectRewriteKindByStrategy(std::string_view rawStrategy) -> IntentRewriteKind {
  const auto strategy = toLowerCopy(rawStrategy);
  if (strategy.empty()) {
    return IntentRewriteKind::None;
  }
  if (strategy == "dp.fibonacci.iterative" || strategy == "dp.fib.v1") {
    return IntentRewriteKind::FibonacciIterative;
  }
  if (strategy == "math.factorial.iterative" || strategy == "math.factorial.loop.v1") {
    return IntentRewriteKind::FactorialIterative;
  }
  if (strategy == "math.pow.binary_exp" || strategy == "pow.binary_exp" || strategy == "math.pow.fast.v1") {
    return IntentRewriteKind::PowerBinaryExp;
  }
  if (strategy == "math.gcd.euclid" || strategy == "math.gcd.modulo.v1") {
    return IntentRewriteKind::GcdEuclidModulo;
  }
  if (strategy == "number.prime.sqrt" || strategy == "number.prime.sqrt.v1") {
    return IntentRewriteKind::PrimeCheckSqrt;
  }
  if (strategy == "number.divisors.sqrt" || strategy == "number.divisors.sqrt.v1") {
    return IntentRewriteKind::DivisorCountSqrt;
  }
  if (strategy == "greedy.sweep.v1" || strategy == "greedy.sweep.interval_cover.v1"
      || strategy == "greedy.interval_cover.v1") {
    return IntentRewriteKind::IntervalCoverGreedy;
  }
  if (strategy == "search.binary.v1" || strategy == "search.binary.sorted.v1") {
    return IntentRewriteKind::BinarySearchSorted;
  }
  if (strategy == "search.lower_bound.v1" || strategy == "search.lower_bound.sorted.v1") {
    return IntentRewriteKind::LowerBoundSorted;
  }
  if (strategy == "search.upper_bound.v1" || strategy == "search.upper_bound.sorted.v1") {
    return IntentRewriteKind::UpperBoundSorted;
  }
  if (strategy == "search.count_less.v1" || strategy == "search.count_less.sorted.v1") {
    return IntentRewriteKind::CountLessSorted;
  }
  if (strategy == "search.count_less_equal.v1" || strategy == "search.count_less_equal.sorted.v1"
      || strategy == "search.count_le.v1") {
    return IntentRewriteKind::CountLessEqualSorted;
  }
  if (strategy == "search.count_greater.v1" || strategy == "search.count_greater.sorted.v1"
      || strategy == "search.count_gt.v1") {
    return IntentRewriteKind::CountGreaterSorted;
  }
  if (strategy == "search.count_greater_equal.v1" || strategy == "search.count_greater_equal.sorted.v1"
      || strategy == "search.count_ge.v1") {
    return IntentRewriteKind::CountGreaterEqualSorted;
  }
  if (strategy == "search.count_equal.v1" || strategy == "search.count_equal.sorted.v1") {
    return IntentRewriteKind::CountEqualSorted;
  }
  if (strategy == "search.count_not_equal.v1" || strategy == "search.count_not_equal.sorted.v1"
      || strategy == "search.count_ne.v1") {
    return IntentRewriteKind::CountNotEqualSorted;
  }
  if (strategy == "search.count_range.v1" || strategy == "search.count_range.sorted.v1") {
    return IntentRewriteKind::CountRangeSorted;
  }
  if (strategy == "search.count_outside_range.v1" || strategy == "search.count_outside_range.sorted.v1") {
    return IntentRewriteKind::CountOutsideRangeSorted;
  }
  if (strategy == "number.bit_peel.iterative" || strategy == "number.bit_peel.fold.v1") {
    return IntentRewriteKind::BitPeelIterative;
  }
  if (strategy == "math.sum.formula.v1") {
    return IntentRewriteKind::ReduceSumFormula;
  }
  if (strategy == "math.sum_squares.formula.v1") {
    return IntentRewriteKind::SumSquaresFormula;
  }
  if (strategy == "math.sum_cubes.formula.v1") {
    return IntentRewriteKind::SumCubesFormula;
  }
  if (strategy == "math.sum_even_squares.formula.v1") {
    return IntentRewriteKind::SumEvenSquaresFormula;
  }
  if (strategy == "math.sum_odd_squares.formula.v1") {
    return IntentRewriteKind::SumOddSquaresFormula;
  }
  if (strategy == "math.sum_even_cubes.formula.v1") {
    return IntentRewriteKind::SumEvenCubesFormula;
  }
  if (strategy == "math.sum_odd_cubes.formula.v1") {
    return IntentRewriteKind::SumOddCubesFormula;
  }
  if (strategy == "math.sum_even.formula.v1") {
    return IntentRewriteKind::SumEvenFormula;
  }
  if (strategy == "math.sum_odd.formula.v1") {
    return IntentRewriteKind::SumOddFormula;
  }
  if (strategy == "math.sqrt.newton.v1") {
    return IntentRewriteKind::SqrtBoundedLoop;
  }
  if (strategy == "search.identity.bounds.v1") {
    return IntentRewriteKind::SearchIdentity;
  }
  return IntentRewriteKind::None;
}

auto selectRewriteKind(
  std::string_view functionName,
  std::string_view rawGoal,
  std::string_view rawStrategy
) -> IntentRewriteKind {
  const auto pinned = selectRewriteKindByStrategy(rawStrategy);
  if (pinned != IntentRewriteKind::None) {
    return pinned;
  }
  if (!trimCopy(rawStrategy).empty()) {
    return IntentRewriteKind::None;
  }

  auto goal = toLowerCopy(rawGoal);
  if (goal == "auto_plan") {
    goal = inferAutoGoalByName(functionName);
  }
  if (goal == "fibonacci_dp" || goal == "fibonacci_iterative") {
    return IntentRewriteKind::FibonacciIterative;
  }
  if (goal == "factorial_iterative") {
    return IntentRewriteKind::FactorialIterative;
  }
  if (goal == "power_fast" || goal == "pow_fast" || goal == "binary_exponentiation") {
    return IntentRewriteKind::PowerBinaryExp;
  }
  if (goal == "gcd_euclid" || goal == "gcd_modulo") {
    return IntentRewriteKind::GcdEuclidModulo;
  }
  if (goal == "is_prime_fast" || goal == "prime_check_sqrt" || goal == "prime_sqrt") {
    return IntentRewriteKind::PrimeCheckSqrt;
  }
  if (goal == "count_divisors_sqrt" || goal == "divisor_count_sqrt") {
    return IntentRewriteKind::DivisorCountSqrt;
  }
  if (goal == "interval_cover_greedy" || goal == "sprinkler_cover_min") {
    return IntentRewriteKind::IntervalCoverGreedy;
  }
  if (goal == "binary_search_sorted" || goal == "binary_search") {
    return IntentRewriteKind::BinarySearchSorted;
  }
  if (goal == "lower_bound_sorted" || goal == "lower_bound") {
    return IntentRewriteKind::LowerBoundSorted;
  }
  if (goal == "upper_bound_sorted" || goal == "upper_bound") {
    return IntentRewriteKind::UpperBoundSorted;
  }
  if (goal == "count_less_sorted" || goal == "count_less" || goal == "count_lt_sorted") {
    return IntentRewriteKind::CountLessSorted;
  }
  if (goal == "count_less_equal_sorted" || goal == "count_less_equal" || goal == "count_le_sorted") {
    return IntentRewriteKind::CountLessEqualSorted;
  }
  if (goal == "count_greater_sorted" || goal == "count_greater" || goal == "count_gt_sorted") {
    return IntentRewriteKind::CountGreaterSorted;
  }
  if (goal == "count_greater_equal_sorted" || goal == "count_greater_equal" || goal == "count_ge_sorted") {
    return IntentRewriteKind::CountGreaterEqualSorted;
  }
  if (goal == "count_equal_sorted" || goal == "count_equal") {
    return IntentRewriteKind::CountEqualSorted;
  }
  if (goal == "count_not_equal_sorted" || goal == "count_not_equal" || goal == "count_ne_sorted") {
    return IntentRewriteKind::CountNotEqualSorted;
  }
  if (goal == "count_range_sorted" || goal == "count_range") {
    return IntentRewriteKind::CountRangeSorted;
  }
  if (goal == "count_outside_range_sorted" || goal == "count_outside_range") {
    return IntentRewriteKind::CountOutsideRangeSorted;
  }
  if (goal == "bit_peel_iterative" || goal == "bit_peel_fold" || goal == "recursive_bit_peel") {
    return IntentRewriteKind::BitPeelIterative;
  }
  if (goal == "reduce_sum") {
    return IntentRewriteKind::ReduceSumFormula;
  }
  if (goal == "sum_squares_formula" || goal == "reduce_sum_squares") {
    return IntentRewriteKind::SumSquaresFormula;
  }
  if (goal == "sum_cubes_formula" || goal == "reduce_sum_cubes") {
    return IntentRewriteKind::SumCubesFormula;
  }
  if (goal == "sum_even_squares_formula" || goal == "reduce_sum_even_squares") {
    return IntentRewriteKind::SumEvenSquaresFormula;
  }
  if (goal == "sum_odd_squares_formula" || goal == "reduce_sum_odd_squares") {
    return IntentRewriteKind::SumOddSquaresFormula;
  }
  if (goal == "sum_even_cubes_formula" || goal == "reduce_sum_even_cubes") {
    return IntentRewriteKind::SumEvenCubesFormula;
  }
  if (goal == "sum_odd_cubes_formula" || goal == "reduce_sum_odd_cubes") {
    return IntentRewriteKind::SumOddCubesFormula;
  }
  if (goal == "sum_even_formula" || goal == "reduce_sum_even") {
    return IntentRewriteKind::SumEvenFormula;
  }
  if (goal == "sum_odd_formula" || goal == "reduce_sum_odd") {
    return IntentRewriteKind::SumOddFormula;
  }
  if (goal == "sqrt_bounded_loop") {
    return IntentRewriteKind::SqrtBoundedLoop;
  }
  if (goal == "search_element") {
    return IntentRewriteKind::SearchIdentity;
  }
  return IntentRewriteKind::None;
}

void appendFibonacciIterativeBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view paramName
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}if ({} < 2):", indent1, paramName));
  out.push_back(std::format("{}return {}", indent2, paramName));
  out.push_back(std::format("{}let a = 0", indent1));
  out.push_back(std::format("{}let b = 1", indent1));
  out.push_back(std::format("{}let i = 2", indent1));
  out.push_back(std::format("{}while (i <= {}):", indent1, paramName));
  out.push_back(std::format("{}let c = a + b", indent2));
  out.push_back(std::format("{}a = b", indent2));
  out.push_back(std::format("{}b = c", indent2));
  out.push_back(std::format("{}i = i + 1", indent2));
  out.push_back(std::format("{}return b", indent1));
}

void appendFactorialIterativeBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view paramName
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}if ({} <= 1):", indent1, paramName));
  out.push_back(std::format("{}return 1", indent2));
  out.push_back(std::format("{}let i = 2", indent1));
  out.push_back(std::format("{}let acc = 1", indent1));
  out.push_back(std::format("{}while (i <= {}):", indent1, paramName));
  out.push_back(std::format("{}acc = acc * i", indent2));
  out.push_back(std::format("{}i = i + 1", indent2));
  out.push_back(std::format("{}return acc", indent1));
}

void appendPowerBinaryExpBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view baseParam,
  std::string_view expParam
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  const std::string indent3(baseIndent + 12, ' ');
  out.push_back(std::format("{}if ({} < 0):", indent1, expParam));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}let e = {}", indent1, expParam));
  out.push_back(std::format("{}let b = {}", indent1, baseParam));
  out.push_back(std::format("{}let result = 1", indent1));
  out.push_back(std::format("{}while (e > 0):", indent1));
  out.push_back(std::format("{}let odd = e - (e / 2) * 2", indent2));
  out.push_back(std::format("{}if (odd == 1):", indent2));
  out.push_back(std::format("{}result = result * b", indent3));
  out.push_back(std::format("{}b = b * b", indent2));
  out.push_back(std::format("{}e = e / 2", indent2));
  out.push_back(std::format("{}return result", indent1));
}

void appendGcdEuclidBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view aParam,
  std::string_view bParam
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}let x = {}", indent1, aParam));
  out.push_back(std::format("{}let y = {}", indent1, bParam));
  out.push_back(std::format("{}if (x < 0):", indent1));
  out.push_back(std::format("{}x = 0 - x", indent2));
  out.push_back(std::format("{}if (y < 0):", indent1));
  out.push_back(std::format("{}y = 0 - y", indent2));
  out.push_back(std::format("{}while (y != 0):", indent1));
  out.push_back(std::format("{}let t = x - (x / y) * y", indent2));
  out.push_back(std::format("{}x = y", indent2));
  out.push_back(std::format("{}y = t", indent2));
  out.push_back(std::format("{}return x", indent1));
}

void appendPrimeCheckSqrtBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view paramName
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  const std::string indent3(baseIndent + 12, ' ');
  out.push_back(std::format("{}if ({} < 2):", indent1, paramName));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}if ({} == 2):", indent1, paramName));
  out.push_back(std::format("{}return 1", indent2));
  out.push_back(std::format("{}let even = {} - ({} / 2) * 2", indent1, paramName, paramName));
  out.push_back(std::format("{}if (even == 0):", indent1));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}let i = 3", indent1));
  out.push_back(std::format("{}while ((i * i) <= {}):", indent1, paramName));
  out.push_back(std::format("{}let rem = {} - ({} / i) * i", indent2, paramName, paramName));
  out.push_back(std::format("{}if (rem == 0):", indent2));
  out.push_back(std::format("{}return 0", indent3));
  out.push_back(std::format("{}i = i + 2", indent2));
  out.push_back(std::format("{}return 1", indent1));
}

void appendDivisorCountSqrtBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view paramName
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  const std::string indent3(baseIndent + 12, ' ');
  const std::string indent4(baseIndent + 16, ' ');
  out.push_back(std::format("{}if ({} <= 0):", indent1, paramName));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}let i = 1", indent1));
  out.push_back(std::format("{}let count = 0", indent1));
  out.push_back(std::format("{}while ((i * i) <= {}):", indent1, paramName));
  out.push_back(std::format("{}let rem = {} - ({} / i) * i", indent2, paramName, paramName));
  out.push_back(std::format("{}if (rem == 0):", indent2));
  out.push_back(std::format("{}if ((i * i) == {}):", indent3, paramName));
  out.push_back(std::format("{}count = count + 1", indent4));
  out.push_back(std::format("{}else:", indent3));
  out.push_back(std::format("{}count = count + 2", indent4));
  out.push_back(std::format("{}i = i + 1", indent2));
  out.push_back(std::format("{}return count", indent1));
}

void appendIntervalCoverGreedyBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view treesParam,
  std::string_view treeCountParam,
  std::string_view leftParam,
  std::string_view rightParam,
  std::string_view sprinklerCountParam
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}let i = 0", indent1));
  out.push_back(std::format("{}let j = 0", indent1));
  out.push_back(std::format("{}let used = 0", indent1));
  out.push_back(std::format("{}while (i < {}):", indent1, treeCountParam));
  out.push_back(std::format("{}let need = {}[i]", indent2, treesParam));
  out.push_back(std::format("{}let best = need - 1", indent2));
  out.push_back(
    std::format(
      "{}while ((j < {}) and ({}[j] <= need)):",
      indent2,
      sprinklerCountParam,
      leftParam
    )
  );
  out.push_back(std::format("{}if ({}[j] > best):", indent2 + "    ", rightParam));
  out.push_back(std::format("{}best = {}[j]", indent2 + "        ", rightParam));
  out.push_back(std::format("{}j = j + 1", indent2 + "    "));
  out.push_back(std::format("{}if (best < need):", indent2));
  out.push_back(std::format("{}return -1", indent2 + "    "));
  out.push_back(std::format("{}used = used + 1", indent2));
  out.push_back(std::format("{}while ((i < {}) and ({}[i] <= best)):", indent2, treeCountParam, treesParam));
  out.push_back(std::format("{}i = i + 1", indent2 + "    "));
  out.push_back(std::format("{}return used", indent1));
}

void appendBinarySearchSortedBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view arrParam,
  std::string_view nParam,
  std::string_view targetParam
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  const std::string indent3(baseIndent + 12, ' ');
  out.push_back(std::format("{}let l = 0", indent1));
  out.push_back(std::format("{}let r = {} - 1", indent1, nParam));
  out.push_back(std::format("{}while (l <= r):", indent1));
  out.push_back(std::format("{}let mid = l + (r - l) / 2", indent2));
  out.push_back(std::format("{}if ({}[mid] == {}):", indent2, arrParam, targetParam));
  out.push_back(std::format("{}return mid", indent3));
  out.push_back(std::format("{}if ({}[mid] < {}):", indent2, arrParam, targetParam));
  out.push_back(std::format("{}l = mid + 1", indent3));
  out.push_back(std::format("{}else:", indent2));
  out.push_back(std::format("{}r = mid - 1", indent3));
  out.push_back(std::format("{}return -1", indent1));
}

void appendLowerBoundSortedBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view arrParam,
  std::string_view nParam,
  std::string_view targetParam
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  const std::string indent3(baseIndent + 12, ' ');
  out.push_back(std::format("{}let l = 0", indent1));
  out.push_back(std::format("{}let r = {}", indent1, nParam));
  out.push_back(std::format("{}while (l < r):", indent1));
  out.push_back(std::format("{}let mid = l + (r - l) / 2", indent2));
  out.push_back(std::format("{}if ({}[mid] < {}):", indent2, arrParam, targetParam));
  out.push_back(std::format("{}l = mid + 1", indent3));
  out.push_back(std::format("{}else:", indent2));
  out.push_back(std::format("{}r = mid", indent3));
  out.push_back(std::format("{}return l", indent1));
}

void appendUpperBoundSortedBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view arrParam,
  std::string_view nParam,
  std::string_view targetParam
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  const std::string indent3(baseIndent + 12, ' ');
  out.push_back(std::format("{}let l = 0", indent1));
  out.push_back(std::format("{}let r = {}", indent1, nParam));
  out.push_back(std::format("{}while (l < r):", indent1));
  out.push_back(std::format("{}let mid = l + (r - l) / 2", indent2));
  out.push_back(std::format("{}if ({}[mid] <= {}):", indent2, arrParam, targetParam));
  out.push_back(std::format("{}l = mid + 1", indent3));
  out.push_back(std::format("{}else:", indent2));
  out.push_back(std::format("{}r = mid", indent3));
  out.push_back(std::format("{}return l", indent1));
}

void appendCountLessSortedBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view arrParam,
  std::string_view nParam,
  std::string_view targetParam
) {
  appendLowerBoundSortedBody(out, baseIndent, arrParam, nParam, targetParam);
}

void appendCountLessEqualSortedBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view arrParam,
  std::string_view nParam,
  std::string_view targetParam
) {
  appendUpperBoundSortedBody(out, baseIndent, arrParam, nParam, targetParam);
}

void appendCountGreaterSortedBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view arrParam,
  std::string_view nParam,
  std::string_view targetParam
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  const std::string indent3(baseIndent + 12, ' ');
  out.push_back(std::format("{}let l = 0", indent1));
  out.push_back(std::format("{}let r = {}", indent1, nParam));
  out.push_back(std::format("{}while (l < r):", indent1));
  out.push_back(std::format("{}let mid = l + (r - l) / 2", indent2));
  out.push_back(std::format("{}if ({}[mid] <= {}):", indent2, arrParam, targetParam));
  out.push_back(std::format("{}l = mid + 1", indent3));
  out.push_back(std::format("{}else:", indent2));
  out.push_back(std::format("{}r = mid", indent3));
  out.push_back(std::format("{}return {} - l", indent1, nParam));
}

void appendCountGreaterEqualSortedBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view arrParam,
  std::string_view nParam,
  std::string_view targetParam
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  const std::string indent3(baseIndent + 12, ' ');
  out.push_back(std::format("{}let l = 0", indent1));
  out.push_back(std::format("{}let r = {}", indent1, nParam));
  out.push_back(std::format("{}while (l < r):", indent1));
  out.push_back(std::format("{}let mid = l + (r - l) / 2", indent2));
  out.push_back(std::format("{}if ({}[mid] < {}):", indent2, arrParam, targetParam));
  out.push_back(std::format("{}l = mid + 1", indent3));
  out.push_back(std::format("{}else:", indent2));
  out.push_back(std::format("{}r = mid", indent3));
  out.push_back(std::format("{}return {} - l", indent1, nParam));
}

void appendCountEqualSortedBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view arrParam,
  std::string_view nParam,
  std::string_view targetParam
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  const std::string indent3(baseIndent + 12, ' ');
  out.push_back(std::format("{}let ll = 0", indent1));
  out.push_back(std::format("{}let rr = {}", indent1, nParam));
  out.push_back(std::format("{}while (ll < rr):", indent1));
  out.push_back(std::format("{}let mid = ll + (rr - ll) / 2", indent2));
  out.push_back(std::format("{}if ({}[mid] < {}):", indent2, arrParam, targetParam));
  out.push_back(std::format("{}ll = mid + 1", indent3));
  out.push_back(std::format("{}else:", indent2));
  out.push_back(std::format("{}rr = mid", indent3));
  out.push_back(std::format("{}let left = ll", indent1));
  out.push_back(std::format("{}ll = left", indent1));
  out.push_back(std::format("{}rr = {}", indent1, nParam));
  out.push_back(std::format("{}while (ll < rr):", indent1));
  out.push_back(std::format("{}let mid = ll + (rr - ll) / 2", indent2));
  out.push_back(std::format("{}if ({}[mid] <= {}):", indent2, arrParam, targetParam));
  out.push_back(std::format("{}ll = mid + 1", indent3));
  out.push_back(std::format("{}else:", indent2));
  out.push_back(std::format("{}rr = mid", indent3));
  out.push_back(std::format("{}return ll - left", indent1));
}

void appendCountNotEqualSortedBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view arrParam,
  std::string_view nParam,
  std::string_view targetParam
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  const std::string indent3(baseIndent + 12, ' ');
  out.push_back(std::format("{}let ll = 0", indent1));
  out.push_back(std::format("{}let rr = {}", indent1, nParam));
  out.push_back(std::format("{}while (ll < rr):", indent1));
  out.push_back(std::format("{}let mid = ll + (rr - ll) / 2", indent2));
  out.push_back(std::format("{}if ({}[mid] < {}):", indent2, arrParam, targetParam));
  out.push_back(std::format("{}ll = mid + 1", indent3));
  out.push_back(std::format("{}else:", indent2));
  out.push_back(std::format("{}rr = mid", indent3));
  out.push_back(std::format("{}let left = ll", indent1));
  out.push_back(std::format("{}ll = left", indent1));
  out.push_back(std::format("{}rr = {}", indent1, nParam));
  out.push_back(std::format("{}while (ll < rr):", indent1));
  out.push_back(std::format("{}let mid = ll + (rr - ll) / 2", indent2));
  out.push_back(std::format("{}if ({}[mid] <= {}):", indent2, arrParam, targetParam));
  out.push_back(std::format("{}ll = mid + 1", indent3));
  out.push_back(std::format("{}else:", indent2));
  out.push_back(std::format("{}rr = mid", indent3));
  out.push_back(std::format("{}let eq = ll - left", indent1));
  out.push_back(std::format("{}return {} - eq", indent1, nParam));
}

void appendCountRangeSortedBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view arrParam,
  std::string_view nParam,
  std::string_view lowParam,
  std::string_view highParam
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  const std::string indent3(baseIndent + 12, ' ');
  out.push_back(std::format("{}if ({} > {}):", indent1, lowParam, highParam));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}let ll = 0", indent1));
  out.push_back(std::format("{}let rr = {}", indent1, nParam));
  out.push_back(std::format("{}while (ll < rr):", indent1));
  out.push_back(std::format("{}let mid = ll + (rr - ll) / 2", indent2));
  out.push_back(std::format("{}if ({}[mid] < {}):", indent2, arrParam, lowParam));
  out.push_back(std::format("{}ll = mid + 1", indent3));
  out.push_back(std::format("{}else:", indent2));
  out.push_back(std::format("{}rr = mid", indent3));
  out.push_back(std::format("{}let left = ll", indent1));
  out.push_back(std::format("{}ll = left", indent1));
  out.push_back(std::format("{}rr = {}", indent1, nParam));
  out.push_back(std::format("{}while (ll < rr):", indent1));
  out.push_back(std::format("{}let mid = ll + (rr - ll) / 2", indent2));
  out.push_back(std::format("{}if ({}[mid] <= {}):", indent2, arrParam, highParam));
  out.push_back(std::format("{}ll = mid + 1", indent3));
  out.push_back(std::format("{}else:", indent2));
  out.push_back(std::format("{}rr = mid", indent3));
  out.push_back(std::format("{}return ll - left", indent1));
}

void appendCountOutsideRangeSortedBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view arrParam,
  std::string_view nParam,
  std::string_view lowParam,
  std::string_view highParam
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  const std::string indent3(baseIndent + 12, ' ');
  out.push_back(std::format("{}if ({} > {}):", indent1, lowParam, highParam));
  out.push_back(std::format("{}return {}", indent2, nParam));
  out.push_back(std::format("{}let ll = 0", indent1));
  out.push_back(std::format("{}let rr = {}", indent1, nParam));
  out.push_back(std::format("{}while (ll < rr):", indent1));
  out.push_back(std::format("{}let mid = ll + (rr - ll) / 2", indent2));
  out.push_back(std::format("{}if ({}[mid] < {}):", indent2, arrParam, lowParam));
  out.push_back(std::format("{}ll = mid + 1", indent3));
  out.push_back(std::format("{}else:", indent2));
  out.push_back(std::format("{}rr = mid", indent3));
  out.push_back(std::format("{}let left = ll", indent1));
  out.push_back(std::format("{}ll = left", indent1));
  out.push_back(std::format("{}rr = {}", indent1, nParam));
  out.push_back(std::format("{}while (ll < rr):", indent1));
  out.push_back(std::format("{}let mid = ll + (rr - ll) / 2", indent2));
  out.push_back(std::format("{}if ({}[mid] <= {}):", indent2, arrParam, highParam));
  out.push_back(std::format("{}ll = mid + 1", indent3));
  out.push_back(std::format("{}else:", indent2));
  out.push_back(std::format("{}rr = mid", indent3));
  out.push_back(std::format("{}let inside = ll - left", indent1));
  out.push_back(std::format("{}return {} - inside", indent1, nParam));
}

void appendBitPeelIterativeBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view valueParam,
  std::string_view stepsParam
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}let cur = {}", indent1, valueParam));
  out.push_back(std::format("{}let steps = {}", indent1, stepsParam));
  out.push_back(std::format("{}let acc = 0", indent1));
  out.push_back(std::format("{}if (steps <= 0):", indent1));
  out.push_back(std::format("{}return cur", indent2));
  out.push_back(std::format("{}while (steps > 0):", indent1));
  out.push_back(std::format("{}acc = acc + (cur - (cur / 2) * 2)", indent2));
  out.push_back(std::format("{}cur = cur / 2", indent2));
  out.push_back(std::format("{}steps = steps - 1", indent2));
  out.push_back(std::format("{}return cur + acc", indent1));
}

void appendReduceSumFormulaBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view paramName
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}if ({} <= 0):", indent1, paramName));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}let left = {}", indent1, paramName));
  out.push_back(std::format("{}let right = {} + 1", indent1, paramName));
  out.push_back(std::format("{}let even = left - (left / 2) * 2", indent1));
  out.push_back(std::format("{}if (even == 0):", indent1));
  out.push_back(std::format("{}left = left / 2", indent2));
  out.push_back(std::format("{}else:", indent1));
  out.push_back(std::format("{}right = right / 2", indent2));
  out.push_back(std::format("{}return left * right", indent1));
}

void appendSumSquaresFormulaBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view paramName
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}if ({} <= 0):", indent1, paramName));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}let a = {} * ({} + 1)", indent1, paramName, paramName));
  out.push_back(std::format("{}let b = a / 2", indent1));
  out.push_back(std::format("{}let c = b * ({} * 2 + 1)", indent1, paramName));
  out.push_back(std::format("{}return c / 3", indent1));
}

void appendSumCubesFormulaBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view paramName
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}if ({} <= 0):", indent1, paramName));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}let a = {} * ({} + 1)", indent1, paramName, paramName));
  out.push_back(std::format("{}let t = a / 2", indent1));
  out.push_back(std::format("{}return t * t", indent1));
}

void appendSumEvenSquaresFormulaBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view paramName
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}if ({} <= 0):", indent1, paramName));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}let m = {} / 2", indent1, paramName));
  out.push_back(std::format("{}let p = m * (m + 1)", indent1));
  out.push_back(std::format("{}let q = p * (m * 2 + 1)", indent1));
  out.push_back(std::format("{}return (q / 6) * 4", indent1));
}

void appendSumOddSquaresFormulaBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view paramName
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}if ({} <= 0):", indent1, paramName));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}let m = ({} + 1) / 2", indent1, paramName));
  out.push_back(std::format("{}let a = m * (m * 2 - 1)", indent1));
  out.push_back(std::format("{}let b = a * (m * 2 + 1)", indent1));
  out.push_back(std::format("{}return b / 3", indent1));
}

void appendSumEvenCubesFormulaBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view paramName
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}if ({} <= 0):", indent1, paramName));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}let m = {} / 2", indent1, paramName));
  out.push_back(std::format("{}let p = m * (m + 1)", indent1));
  out.push_back(std::format("{}let q = p * p", indent1));
  out.push_back(std::format("{}return q + q", indent1));
}

void appendSumOddCubesFormulaBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view paramName
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}if ({} <= 0):", indent1, paramName));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}let m = ({} + 1) / 2", indent1, paramName));
  out.push_back(std::format("{}let m2 = m * m", indent1));
  out.push_back(std::format("{}return m2 * (m2 * 2 - 1)", indent1));
}

void appendSumEvenFormulaBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view paramName
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}if ({} <= 0):", indent1, paramName));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}let m = {} / 2", indent1, paramName));
  out.push_back(std::format("{}return m * (m + 1)", indent1));
}

void appendSumOddFormulaBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view paramName
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}if ({} <= 0):", indent1, paramName));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}let m = ({} + 1) / 2", indent1, paramName));
  out.push_back(std::format("{}return m * m", indent1));
}

void appendSqrtBoundedLoopBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view paramName
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}if ({} <= 0):", indent1, paramName));
  out.push_back(std::format("{}return 0", indent2));
  out.push_back(std::format("{}let x = {}", indent1, paramName));
  out.push_back(std::format("{}let y = (x + 1) / 2", indent1));
  out.push_back(std::format("{}while (y < x):", indent1));
  out.push_back(std::format("{}x = y", indent2));
  out.push_back(std::format("{}y = (x + ({} / x)) / 2", indent2, paramName));
  out.push_back(std::format("{}return x", indent1));
}

void appendSearchIdentityBody(
  std::vector<std::string> &out,
  std::size_t baseIndent,
  std::string_view nParam,
  std::string_view targetParam
) {
  const std::string indent1(baseIndent + 4, ' ');
  const std::string indent2(baseIndent + 8, ' ');
  out.push_back(std::format("{}if ({} < 0):", indent1, targetParam));
  out.push_back(std::format("{}return -1", indent2));
  out.push_back(std::format("{}if ({} >= {}):", indent1, targetParam, nParam));
  out.push_back(std::format("{}return -1", indent2));
  out.push_back(std::format("{}return {}", indent1, targetParam));
}

auto preprocessIntentSource(const std::string &source) -> IntentPreprocessResult {
  IntentPreprocessResult result {};
  const auto lines = splitLinesNormalized(source);
  std::vector<std::string> stripped {};
  stripped.reserve(lines.size());

  std::size_t i = 0;
  while (i < lines.size()) {
    const auto line = lines[i];
    const auto trimmed = trimCopy(line);
    if (trimmed.starts_with("intent ")) {
      const std::size_t baseIndent = leadingSpaces(line);
      if (trimmed.starts_with("intent func ")) {
        const auto fnName = parseIntentFunctionName(trimmed);
        std::string goal {};
        std::string strategy {};
        bool intentEnabled = true;
        std::size_t j = i + 1;
        while (j < lines.size()) {
          const auto nestedTrimmed = trimCopy(lines[j]);
          if (nestedTrimmed.empty()) {
            ++j;
            continue;
          }
          if (leadingSpaces(lines[j]) <= baseIndent) {
            break;
          }
          if (nestedTrimmed.starts_with("goal:")) {
            goal = trimCopy(std::string_view(nestedTrimmed).substr(5));
          } else if (nestedTrimmed.starts_with("strategy:")) {
            strategy = trimCopy(std::string_view(nestedTrimmed).substr(9));
          }

          const auto normalized = normalizedLowerNoSpace(nestedTrimmed);
          if (normalized.find("intent==false") != std::string::npos
              || normalized.find("intent=false") != std::string::npos) {
            intentEnabled = false;
          }
          ++j;
        }
        if (toLowerCopy(goal) == "off") {
          intentEnabled = false;
        }
        if (!fnName.empty()) {
          result.functionDirectives[fnName] = IntentDirective {
            .goal = goal,
            .strategy = strategy,
            .intentEnabled = intentEnabled,
          };
          if (intentTraceEnabled()) {
            std::cout << std::format(
              "thag: intent directive fn={} goal={} strategy={} enabled={}\n",
              fnName,
              goal,
              strategy,
              intentEnabled ? "true" : "false"
            );
          }
        }
        i = j;
        continue;
      }
      std::size_t j = i + 1;
      while (j < lines.size()) {
        const auto nestedTrimmed = trimCopy(lines[j]);
        if (nestedTrimmed.empty()) {
          ++j;
          continue;
        }
        if (leadingSpaces(lines[j]) <= baseIndent) {
          break;
        }
        ++j;
      }
      i = j;
      continue;
    }
    stripped.push_back(line);
    ++i;
  }

  std::vector<std::string> rewritten {};
  rewritten.reserve(stripped.size() + 16);
  i = 0;
  while (i < stripped.size()) {
    const auto line = stripped[i];
    const auto trimmed = trimCopy(line);
    if (!trimmed.starts_with("func ")) {
      rewritten.push_back(line);
      ++i;
      continue;
    }

    const std::size_t baseIndent = leadingSpaces(line);
    std::size_t j = i + 1;
    while (j < stripped.size()) {
      const auto nestedTrimmed = trimCopy(stripped[j]);
      if (nestedTrimmed.empty()) {
        ++j;
        continue;
      }
      if (leadingSpaces(stripped[j]) <= baseIndent) {
        break;
      }
      ++j;
    }

    const auto fnName = parseFunctionName(trimmed);
    const auto directiveIt = result.functionDirectives.find(fnName);
    const bool shouldExplain =
      intentTraceEnabled() || (intentExplainEnabled() && directiveIt != result.functionDirectives.end());

    IntentRewriteKind rewriteKind = IntentRewriteKind::None;
    std::string selectedGoal {};
    std::string selectedStrategy {};
    std::string skipReason {};
    std::string selectionSource {};
    if (directiveIt != result.functionDirectives.end()) {
      selectedGoal = directiveIt->second.goal;
      selectedStrategy = directiveIt->second.strategy;
      if (!directiveIt->second.intentEnabled) {
        skipReason = "intent-disabled";
      } else {
        rewriteKind = selectRewriteKind(fnName, selectedGoal, selectedStrategy);
        if (rewriteKind != IntentRewriteKind::None) {
          if (!selectedStrategy.empty()) {
            selectionSource = "strategy-pin";
          } else if (toLowerCopy(selectedGoal) == "auto_plan") {
            selectionSource = "auto-plan-name";
          } else if (!selectedGoal.empty()) {
            selectionSource = "goal";
          } else {
            selectionSource = "goal-implicit";
          }
        }
      }
      if (rewriteKind == IntentRewriteKind::None && skipReason.empty()) {
        if (!selectedStrategy.empty()) {
          skipReason = "strategy-not-supported";
        } else if (!selectedGoal.empty() && toLowerCopy(selectedGoal) != "auto_plan") {
          skipReason = "goal-not-matched";
        }
        const auto inferred = inferRewriteKindFromBody(fnName, trimmed, stripped, i + 1, j);
        if (rewriteKind == IntentRewriteKind::None
            && (selectedGoal.empty() || toLowerCopy(selectedGoal) == "auto_plan")) {
          rewriteKind = inferred.kind;
          if (!inferred.detectedGoal.empty()) {
            selectedGoal = inferred.detectedGoal;
            skipReason.clear();
            selectionSource = "auto-plan-body";
          }
        }
      }
    } else if (autoOptEnabled()) {
      const auto inferred = inferRewriteKindFromBody(fnName, trimmed, stripped, i + 1, j);
      rewriteKind = inferred.kind;
      if (!inferred.detectedGoal.empty()) {
        selectedGoal = inferred.detectedGoal;
        selectionSource = "auto-detect-body";
      }
    }

    if (rewriteKind == IntentRewriteKind::None) {
      if (shouldExplain) {
        IntentRewriteKind candidateKind = IntentRewriteKind::None;
        if (!selectedStrategy.empty()) {
          candidateKind = selectRewriteKindByStrategy(selectedStrategy);
        } else if (!selectedGoal.empty()) {
          candidateKind = selectRewriteKind(fnName, selectedGoal, "");
        }
        const auto candidateRule = rewriteRuleId(candidateKind);
        if (!selectedGoal.empty() || !selectedStrategy.empty()) {
          if (!skipReason.empty()) {
            std::cout << std::format(
              "thag: rewrite skipped fn={} goal={} strategy={} candidate_rule={} reason={} hint=use goal: off or a supported goal/strategy\n",
              fnName,
              selectedGoal,
              selectedStrategy,
              candidateRule,
              skipReason
            );
          } else {
            std::cout << std::format(
              "thag: rewrite skipped fn={} goal={} strategy={} candidate_rule={}\n",
              fnName,
              selectedGoal,
              selectedStrategy,
              candidateRule
            );
          }
        } else {
          std::cout << std::format("thag: rewrite skipped fn={}\n", fnName);
        }
      }
      rewritten.push_back(line);
      ++i;
      continue;
    }

    rewritten.push_back(line);
    const auto params = parseParamNames(trimmed);
    auto keepOriginalBody = [&]() {
      for (std::size_t k = i + 1; k < j; ++k) {
        rewritten.push_back(stripped[k]);
      }
    };

    switch (rewriteKind) {
      case IntentRewriteKind::FibonacciIterative:
        if (params.empty()) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendFibonacciIterativeBody(rewritten, baseIndent, params[0]);
        break;
      case IntentRewriteKind::FactorialIterative:
        if (params.empty()) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendFactorialIterativeBody(rewritten, baseIndent, params[0]);
        break;
      case IntentRewriteKind::ReduceSumFormula:
        if (params.empty()) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendReduceSumFormulaBody(rewritten, baseIndent, params[0]);
        break;
      case IntentRewriteKind::SumSquaresFormula:
        if (params.empty()) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendSumSquaresFormulaBody(rewritten, baseIndent, params[0]);
        break;
      case IntentRewriteKind::SumCubesFormula:
        if (params.empty()) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendSumCubesFormulaBody(rewritten, baseIndent, params[0]);
        break;
      case IntentRewriteKind::SumEvenSquaresFormula:
        if (params.empty()) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendSumEvenSquaresFormulaBody(rewritten, baseIndent, params[0]);
        break;
      case IntentRewriteKind::SumOddSquaresFormula:
        if (params.empty()) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendSumOddSquaresFormulaBody(rewritten, baseIndent, params[0]);
        break;
      case IntentRewriteKind::SumEvenCubesFormula:
        if (params.empty()) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendSumEvenCubesFormulaBody(rewritten, baseIndent, params[0]);
        break;
      case IntentRewriteKind::SumOddCubesFormula:
        if (params.empty()) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendSumOddCubesFormulaBody(rewritten, baseIndent, params[0]);
        break;
      case IntentRewriteKind::SumEvenFormula:
        if (params.empty()) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendSumEvenFormulaBody(rewritten, baseIndent, params[0]);
        break;
      case IntentRewriteKind::SumOddFormula:
        if (params.empty()) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendSumOddFormulaBody(rewritten, baseIndent, params[0]);
        break;
      case IntentRewriteKind::SqrtBoundedLoop:
        if (params.empty()) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendSqrtBoundedLoopBody(rewritten, baseIndent, params[0]);
        break;
      case IntentRewriteKind::PowerBinaryExp:
        if (params.size() < 2) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendPowerBinaryExpBody(rewritten, baseIndent, params[0], params[1]);
        break;
      case IntentRewriteKind::GcdEuclidModulo:
        if (params.size() < 2) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendGcdEuclidBody(rewritten, baseIndent, params[0], params[1]);
        break;
      case IntentRewriteKind::PrimeCheckSqrt:
        if (params.empty()) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendPrimeCheckSqrtBody(rewritten, baseIndent, params[0]);
        break;
      case IntentRewriteKind::DivisorCountSqrt:
        if (params.empty()) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendDivisorCountSqrtBody(rewritten, baseIndent, params[0]);
        break;
      case IntentRewriteKind::IntervalCoverGreedy:
        if (params.size() < 5) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendIntervalCoverGreedyBody(
          rewritten,
          baseIndent,
          params[0],
          params[1],
          params[2],
          params[3],
          params[4]
        );
        break;
      case IntentRewriteKind::BinarySearchSorted:
        if (params.size() < 3) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendBinarySearchSortedBody(rewritten, baseIndent, params[0], params[1], params[2]);
        break;
      case IntentRewriteKind::LowerBoundSorted:
        if (params.size() < 3) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendLowerBoundSortedBody(rewritten, baseIndent, params[0], params[1], params[2]);
        break;
      case IntentRewriteKind::UpperBoundSorted:
        if (params.size() < 3) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendUpperBoundSortedBody(rewritten, baseIndent, params[0], params[1], params[2]);
        break;
      case IntentRewriteKind::CountLessSorted:
        if (params.size() < 3) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendCountLessSortedBody(rewritten, baseIndent, params[0], params[1], params[2]);
        break;
      case IntentRewriteKind::CountLessEqualSorted:
        if (params.size() < 3) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendCountLessEqualSortedBody(rewritten, baseIndent, params[0], params[1], params[2]);
        break;
      case IntentRewriteKind::CountGreaterSorted:
        if (params.size() < 3) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendCountGreaterSortedBody(rewritten, baseIndent, params[0], params[1], params[2]);
        break;
      case IntentRewriteKind::CountGreaterEqualSorted:
        if (params.size() < 3) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendCountGreaterEqualSortedBody(rewritten, baseIndent, params[0], params[1], params[2]);
        break;
      case IntentRewriteKind::CountEqualSorted:
        if (params.size() < 3) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendCountEqualSortedBody(rewritten, baseIndent, params[0], params[1], params[2]);
        break;
      case IntentRewriteKind::CountNotEqualSorted:
        if (params.size() < 3) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendCountNotEqualSortedBody(rewritten, baseIndent, params[0], params[1], params[2]);
        break;
      case IntentRewriteKind::CountRangeSorted:
        if (params.size() < 4) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendCountRangeSortedBody(rewritten, baseIndent, params[0], params[1], params[2], params[3]);
        break;
      case IntentRewriteKind::CountOutsideRangeSorted:
        if (params.size() < 4) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendCountOutsideRangeSortedBody(rewritten, baseIndent, params[0], params[1], params[2], params[3]);
        break;
      case IntentRewriteKind::BitPeelIterative:
        if (params.size() < 2) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendBitPeelIterativeBody(rewritten, baseIndent, params[0], params[1]);
        break;
      case IntentRewriteKind::SearchIdentity:
        if (params.size() < 2) {
          keepOriginalBody();
          i = j;
          continue;
        }
        appendSearchIdentityBody(rewritten, baseIndent, params[0], params[1]);
        break;
      case IntentRewriteKind::None:
        keepOriginalBody();
        i = j;
        continue;
    }
    if (shouldExplain) {
      if (!selectedGoal.empty() || !selectedStrategy.empty()) {
        const auto selectedRule = rewriteRuleId(rewriteKind);
        std::cout << std::format(
          "thag: rewrite applied fn={} goal={} strategy={} selected_rule={} selection_source={}\n",
          fnName,
          selectedGoal,
          selectedStrategy,
          selectedRule,
          selectionSource
        );
      } else {
        std::cout << std::format("thag: rewrite applied fn={}\n", fnName);
      }
    }
    result.rewritesApplied += 1;
    i = j;
  }

  result.source = joinLines(rewritten);
  return result;
}

auto parseArgs(const std::vector<std::string> &args) -> Result<DriverOptions, Diagnostic> {
  DriverOptions options {};
  std::size_t i = 1;
  if (i < args.size() && args[i] == "build") {
    options.mode = DriverMode::BuildExecutable;
    ++i;
  }

  for (; i < args.size(); ++i) {
    const auto &arg = args[i];
    if (arg == "--emit-ir") {
      options.emitIR = true;
      continue;
    }
    if (arg == "--emit-obj") {
      options.emitObject = true;
      continue;
    }
    if (arg == "--release") {
      options.release = true;
      continue;
    }
    if (arg == "-o") {
      if (i + 1 >= args.size()) {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::InvalidCli,
          .message = "Missing output path after -o.",
          .span = {},
        });
      }
      options.outputFile = args[++i];
      continue;
    }
    if (arg.starts_with("--opt=")) {
      const auto level = arg.substr(6);
      if (level.empty() || level.size() > 1 || level[0] < '0' || level[0] > '3') {
        return std::unexpected(Diagnostic {
          .code = ErrorCode::InvalidCli,
          .message = std::format("Invalid optimization level '{}'.", level),
          .span = {},
        });
      }
      options.optLevel = level[0] - '0';
      continue;
    }
    if (arg.starts_with('-')) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::InvalidCli,
        .message = std::format("Unknown flag '{}'.", arg),
        .span = {},
      });
    }
    options.inputFile = arg;
  }

  if (options.inputFile.empty()) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::InvalidCli,
      .message =
        "Missing input file. Usage: thag [build] <input.tg> [--emit-ir] [--emit-obj] [--release] -o <output>",
      .span = {},
    });
  }

  if (options.mode == DriverMode::CompileOnly && !options.emitIR && !options.emitObject) {
    options.emitIR = true;
  }

  if (options.outputFile.empty()) {
    const auto stem = std::filesystem::path(options.inputFile).stem().string();
    if (options.mode == DriverMode::BuildExecutable) {
      options.outputFile = stem + ".exe";
    } else {
      options.outputFile = options.emitObject ? stem + ".o" : stem + ".ll";
    }
  }
  return options;
}

auto readFile(const std::string &path) -> Result<std::string, Diagnostic> {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::IoError,
      .message = std::format("Cannot open input file '{}'.", path),
      .span = {},
    });
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

struct ImportExportMap {
  std::string namespacePrefix {};
  std::unordered_map<std::string, std::string> functions {};
  std::unordered_map<std::string, std::string> structs {};
};

struct ModuleCacheEntry {
  ImportExportMap exports {};
};

auto sanitizeNamespace(const std::string &key) -> std::string {
  std::size_t hashed = std::hash<std::string> {}(key);
  return std::format("__mod_{:x}_", hashed);
}

auto parseModuleFile(const std::filesystem::path &path) -> Result<std::unique_ptr<ModuleDecl>, Diagnostic> {
  auto source = readFile(path.string());
  if (!source) {
    return std::unexpected(source.error());
  }
  const auto preprocessed = preprocessIntentSource(source.value());
  if (preprocessed.rewritesApplied > 0) {
    std::cout << std::format(
      "thag: intent optimizer applied {} rewrite(s) in {}\n",
      preprocessed.rewritesApplied,
      path.string()
    );
  }
  Lexer lexer {};
  auto tokens = lexer.tokenize(preprocessed.source, path.string());
  if (!tokens) {
    return std::unexpected(tokens.error());
  }
  Parser parser {};
  auto module = parser.parseModule(tokens.value());
  if (!module) {
    return std::unexpected(module.error());
  }
  return module;
}

auto resolveImportPath(const std::filesystem::path &baseDir, const std::string &rawPath)
  -> Result<std::filesystem::path, Diagnostic> {
  const std::filesystem::path raw {rawPath};
  std::vector<std::filesystem::path> candidates {};

  auto pushCandidates = [&](const std::filesystem::path &root, bool includeFolderAsFile) {
    if (raw.has_extension()) {
      candidates.push_back(root / raw);
    } else {
      if (includeFolderAsFile) {
        candidates.push_back(root / raw);
      }
      candidates.push_back(root / (raw.string() + ".tg"));
      candidates.push_back(root / raw / "mod.tg");
      candidates.push_back(root / (raw.string() + "/mod.tg"));
    }
  };

  if (raw.is_absolute()) {
    pushCandidates(std::filesystem::path {}, true);
  } else {
    const bool isBareModuleName = !raw.has_parent_path() && !raw.has_extension();
    if (isBareModuleName) {
      // Flat module resolution: local module first, then standard library folder.
      pushCandidates(baseDir, true);
      pushCandidates(std::filesystem::current_path() / "lib", false);
      pushCandidates(std::filesystem::current_path(), true);
    } else {
      pushCandidates(baseDir, true);
      pushCandidates(std::filesystem::current_path(), true);
    }
  }

  for (const auto &candidate : candidates) {
    if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
      return std::filesystem::weakly_canonical(candidate);
    }
  }

  return std::unexpected(Diagnostic {
    .code = ErrorCode::IoError,
    .message = std::format("Cannot resolve import '{}'.", rawPath),
    .span = {},
  });
}

void renameTypeNames(TypePtr &type, const std::unordered_map<std::string, std::string> &structRenames) {
  if (!type) {
    return;
  }
  if (type->base == BaseType::Struct) {
    if (auto it = structRenames.find(type->name); it != structRenames.end()) {
      type->name = it->second;
    }
  } else if (type->base == BaseType::Array && type->elementType) {
    renameTypeNames(type->elementType, structRenames);
  }
}

void renameExprNames(
  std::unique_ptr<Expr> &expr,
  const std::unordered_map<std::string, std::string> &funcRenames,
  const std::unordered_map<std::string, std::string> &structRenames
);

void renameStmtNames(
  std::unique_ptr<Stmt> &stmt,
  const std::unordered_map<std::string, std::string> &funcRenames,
  const std::unordered_map<std::string, std::string> &structRenames
) {
  if (!stmt) {
    return;
  }
  switch (stmt->kind) {
    case NodeKind::LetStmt: {
      auto &s = static_cast<LetStmt &>(*stmt);
      renameExprNames(s.init, funcRenames, structRenames);
      return;
    }
    case NodeKind::AssignStmt: {
      auto &s = static_cast<AssignStmt &>(*stmt);
      renameExprNames(s.value, funcRenames, structRenames);
      return;
    }
    case NodeKind::MemberAssignStmt: {
      auto &s = static_cast<MemberAssignStmt &>(*stmt);
      renameExprNames(s.value, funcRenames, structRenames);
      return;
    }
    case NodeKind::ArrayAssignStmt: {
      auto &s = static_cast<ArrayAssignStmt &>(*stmt);
      renameExprNames(s.index, funcRenames, structRenames);
      renameExprNames(s.value, funcRenames, structRenames);
      return;
    }
    case NodeKind::ReturnStmt: {
      auto &s = static_cast<ReturnStmt &>(*stmt);
      if (s.value) {
        renameExprNames(s.value, funcRenames, structRenames);
      }
      return;
    }
    case NodeKind::IfStmt: {
      auto &s = static_cast<IfStmt &>(*stmt);
      renameExprNames(s.condition, funcRenames, structRenames);
      for (auto &nested : s.thenBlock->statements) {
        renameStmtNames(nested, funcRenames, structRenames);
      }
      if (s.elseBlock) {
        for (auto &nested : s.elseBlock->statements) {
          renameStmtNames(nested, funcRenames, structRenames);
        }
      }
      return;
    }
    case NodeKind::LoopStmt: {
      auto &s = static_cast<LoopStmt &>(*stmt);
      if (s.condition) {
        renameExprNames(s.condition, funcRenames, structRenames);
      }
      for (auto &nested : s.body->statements) {
        renameStmtNames(nested, funcRenames, structRenames);
      }
      return;
    }
    case NodeKind::ExprStmt: {
      auto &s = static_cast<ExprStmt &>(*stmt);
      renameExprNames(s.expr, funcRenames, structRenames);
      return;
    }
    case NodeKind::BlockStmt: {
      auto &s = static_cast<BlockStmt &>(*stmt);
      for (auto &nested : s.statements) {
        renameStmtNames(nested, funcRenames, structRenames);
      }
      return;
    }
    default:
      return;
  }
}

void renameExprNames(
  std::unique_ptr<Expr> &expr,
  const std::unordered_map<std::string, std::string> &funcRenames,
  const std::unordered_map<std::string, std::string> &structRenames
) {
  if (!expr) {
    return;
  }
  switch (expr->kind) {
    case NodeKind::BinaryExpr: {
      auto &e = static_cast<BinaryExpr &>(*expr);
      renameExprNames(e.left, funcRenames, structRenames);
      renameExprNames(e.right, funcRenames, structRenames);
      return;
    }
    case NodeKind::CallExpr: {
      auto &e = static_cast<CallExpr &>(*expr);
      if (auto fn = funcRenames.find(e.callee); fn != funcRenames.end()) {
        e.callee = fn->second;
      } else if (auto st = structRenames.find(e.callee); st != structRenames.end()) {
        e.callee = st->second;
      }
      for (auto &arg : e.args) {
        renameExprNames(arg, funcRenames, structRenames);
      }
      return;
    }
    case NodeKind::MemberExpr: {
      auto &e = static_cast<MemberExpr &>(*expr);
      renameExprNames(e.object, funcRenames, structRenames);
      return;
    }
    case NodeKind::MethodCallExpr: {
      auto &e = static_cast<MethodCallExpr &>(*expr);
      renameExprNames(e.object, funcRenames, structRenames);
      for (auto &arg : e.args) {
        renameExprNames(arg, funcRenames, structRenames);
      }
      return;
    }
    case NodeKind::IndexExpr: {
      auto &e = static_cast<IndexExpr &>(*expr);
      renameExprNames(e.array, funcRenames, structRenames);
      renameExprNames(e.index, funcRenames, structRenames);
      return;
    }
    case NodeKind::ArrayLiteralExpr: {
      auto &e = static_cast<ArrayLiteralExpr &>(*expr);
      for (auto &item : e.elements) {
        renameExprNames(item, funcRenames, structRenames);
      }
      return;
    }
    default:
      return;
  }
}

void rewriteQualifiedCallsInExpr(std::unique_ptr<Expr> &expr, const std::unordered_map<std::string, ImportExportMap> &imports);

void rewriteQualifiedCallsInStmt(std::unique_ptr<Stmt> &stmt, const std::unordered_map<std::string, ImportExportMap> &imports) {
  if (!stmt) {
    return;
  }
  switch (stmt->kind) {
    case NodeKind::LetStmt: {
      auto &s = static_cast<LetStmt &>(*stmt);
      rewriteQualifiedCallsInExpr(s.init, imports);
      return;
    }
    case NodeKind::AssignStmt: {
      auto &s = static_cast<AssignStmt &>(*stmt);
      rewriteQualifiedCallsInExpr(s.value, imports);
      return;
    }
    case NodeKind::MemberAssignStmt: {
      auto &s = static_cast<MemberAssignStmt &>(*stmt);
      rewriteQualifiedCallsInExpr(s.value, imports);
      return;
    }
    case NodeKind::ArrayAssignStmt: {
      auto &s = static_cast<ArrayAssignStmt &>(*stmt);
      rewriteQualifiedCallsInExpr(s.index, imports);
      rewriteQualifiedCallsInExpr(s.value, imports);
      return;
    }
    case NodeKind::ReturnStmt: {
      auto &s = static_cast<ReturnStmt &>(*stmt);
      if (s.value) {
        rewriteQualifiedCallsInExpr(s.value, imports);
      }
      return;
    }
    case NodeKind::IfStmt: {
      auto &s = static_cast<IfStmt &>(*stmt);
      rewriteQualifiedCallsInExpr(s.condition, imports);
      for (auto &nested : s.thenBlock->statements) {
        rewriteQualifiedCallsInStmt(nested, imports);
      }
      if (s.elseBlock) {
        for (auto &nested : s.elseBlock->statements) {
          rewriteQualifiedCallsInStmt(nested, imports);
        }
      }
      return;
    }
    case NodeKind::LoopStmt: {
      auto &s = static_cast<LoopStmt &>(*stmt);
      if (s.condition) {
        rewriteQualifiedCallsInExpr(s.condition, imports);
      }
      for (auto &nested : s.body->statements) {
        rewriteQualifiedCallsInStmt(nested, imports);
      }
      return;
    }
    case NodeKind::ExprStmt: {
      auto &s = static_cast<ExprStmt &>(*stmt);
      rewriteQualifiedCallsInExpr(s.expr, imports);
      return;
    }
    case NodeKind::BlockStmt: {
      auto &s = static_cast<BlockStmt &>(*stmt);
      for (auto &nested : s.statements) {
        rewriteQualifiedCallsInStmt(nested, imports);
      }
      return;
    }
    default:
      return;
  }
}

void rewriteQualifiedCallsInExpr(std::unique_ptr<Expr> &expr, const std::unordered_map<std::string, ImportExportMap> &imports) {
  if (!expr) {
    return;
  }
  switch (expr->kind) {
    case NodeKind::BinaryExpr: {
      auto &e = static_cast<BinaryExpr &>(*expr);
      rewriteQualifiedCallsInExpr(e.left, imports);
      rewriteQualifiedCallsInExpr(e.right, imports);
      return;
    }
    case NodeKind::CallExpr: {
      auto &e = static_cast<CallExpr &>(*expr);
      for (auto &arg : e.args) {
        rewriteQualifiedCallsInExpr(arg, imports);
      }
      return;
    }
    case NodeKind::MemberExpr: {
      auto &e = static_cast<MemberExpr &>(*expr);
      rewriteQualifiedCallsInExpr(e.object, imports);
      return;
    }
    case NodeKind::MethodCallExpr: {
      auto &e = static_cast<MethodCallExpr &>(*expr);
      rewriteQualifiedCallsInExpr(e.object, imports);
      for (auto &arg : e.args) {
        rewriteQualifiedCallsInExpr(arg, imports);
      }
      if (e.object && e.object->kind == NodeKind::IdentifierExpr) {
        const auto &id = static_cast<const IdentifierExpr &>(*e.object);
        if (auto it = imports.find(id.name); it != imports.end()) {
          auto fn = it->second.functions.find(e.method);
          if (fn != it->second.functions.end()) {
            expr = std::make_unique<CallExpr>(fn->second, std::move(e.args), e.span);
            return;
          }
          auto st = it->second.structs.find(e.method);
          if (st != it->second.structs.end()) {
            expr = std::make_unique<CallExpr>(st->second, std::move(e.args), e.span);
            return;
          }
        }
      }
      return;
    }
    case NodeKind::IndexExpr: {
      auto &e = static_cast<IndexExpr &>(*expr);
      rewriteQualifiedCallsInExpr(e.array, imports);
      rewriteQualifiedCallsInExpr(e.index, imports);
      return;
    }
    case NodeKind::ArrayLiteralExpr: {
      auto &e = static_cast<ArrayLiteralExpr &>(*expr);
      for (auto &item : e.elements) {
        rewriteQualifiedCallsInExpr(item, imports);
      }
      return;
    }
    default:
      return;
  }
}

auto rewriteQualifiedCallsInModule(
  ModuleDecl &module,
  const std::unordered_map<std::string, ImportExportMap> &imports
) -> void {
  for (auto &fn : module.functions) {
    if (fn->body) {
      for (auto &stmt : fn->body->statements) {
        rewriteQualifiedCallsInStmt(stmt, imports);
      }
    }
  }
  for (auto &stmt : module.topLevelStatements) {
    rewriteQualifiedCallsInStmt(stmt, imports);
  }
}

auto renameModuleSymbols(std::unique_ptr<ModuleDecl> &module, const std::string &namespacePrefix) -> ImportExportMap {
  ImportExportMap exports {};
  exports.namespacePrefix = namespacePrefix;

  std::unordered_map<std::string, std::string> structRenames {};
  for (auto &st : module->structs) {
    const std::string original = st->name;
    const std::string renamed = namespacePrefix + original;
    exports.structs.emplace(original, renamed);
    structRenames.emplace(original, renamed);
    st->name = renamed;
  }

  std::unordered_map<std::string, std::string> functionRenames {};
  for (auto &fn : module->functions) {
    if (fn->isExtern) {
      continue;
    }
    const std::string original = fn->name;
    const std::string renamed = namespacePrefix + original;
    functionRenames.emplace(original, renamed);
    if (fn->methodOwner.empty()) {
      exports.functions.emplace(fn->sourceName, renamed);
    }
    fn->name = renamed;
  }

  for (auto &st : module->structs) {
    for (auto &field : st->fields) {
      renameTypeNames(field.type, structRenames);
    }
  }

  for (auto &fn : module->functions) {
    if (fn->methodOwner == "__module__") {
      fn->methodOwner.clear();
    }
    if (!fn->methodOwner.empty()) {
      if (auto it = structRenames.find(fn->methodOwner); it != structRenames.end()) {
        fn->methodOwner = it->second;
      }
    }
    for (auto &param : fn->params) {
      renameTypeNames(param.type, structRenames);
    }
    renameTypeNames(fn->returnType, structRenames);
    if (fn->body) {
      for (auto &stmt : fn->body->statements) {
        renameStmtNames(stmt, functionRenames, structRenames);
      }
    }
  }

  module->imports.clear();
  return exports;
}

auto loadImportedModule(
  const std::filesystem::path &modulePath,
  std::unordered_map<std::string, ModuleCacheEntry> &cache,
  std::unordered_set<std::string> &loading,
  std::vector<std::unique_ptr<StructDecl>> &importedStructs,
  std::vector<std::unique_ptr<FunctionDecl>> &importedFunctions
) -> Result<ImportExportMap, Diagnostic> {
  const auto canonical = std::filesystem::weakly_canonical(modulePath).string();
  if (auto it = cache.find(canonical); it != cache.end()) {
    return it->second.exports;
  }
  if (loading.contains(canonical)) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::SemanticError,
      .message = std::format("Import cycle detected at '{}'.", canonical),
      .span = {},
    });
  }
  loading.insert(canonical);

  auto parsed = parseModuleFile(canonical);
  if (!parsed) {
    loading.erase(canonical);
    return std::unexpected(parsed.error());
  }

  auto module = std::move(parsed.value());
  std::unordered_map<std::string, ImportExportMap> importAliases {};
  const auto moduleDir = std::filesystem::path(canonical).parent_path();
  for (const auto &imp : module->imports) {
    auto resolved = resolveImportPath(moduleDir, imp.path);
    if (!resolved) {
      loading.erase(canonical);
      return std::unexpected(resolved.error());
    }
    auto imported = loadImportedModule(resolved.value(), cache, loading, importedStructs, importedFunctions);
    if (!imported) {
      loading.erase(canonical);
      return std::unexpected(imported.error());
    }
    importAliases[imp.alias] = imported.value();
  }
  rewriteQualifiedCallsInModule(*module, importAliases);

  if (!module->topLevelStatements.empty()) {
    loading.erase(canonical);
    return std::unexpected(Diagnostic {
      .code = ErrorCode::SemanticError,
      .message = std::format("Imported module '{}' cannot contain top-level executable statements.", canonical),
      .span = module->span,
    });
  }

  auto exports = renameModuleSymbols(module, sanitizeNamespace(canonical));
  for (auto &st : module->structs) {
    importedStructs.push_back(std::move(st));
  }
  for (auto &fn : module->functions) {
    importedFunctions.push_back(std::move(fn));
  }

  cache.emplace(canonical, ModuleCacheEntry {.exports = exports});
  loading.erase(canonical);
  return exports;
}

auto createTargetMachine(const DriverOptions &options) -> Result<std::unique_ptr<llvm::TargetMachine>, Diagnostic> {
  const auto triple = llvm::sys::getDefaultTargetTriple();
  std::string error;
  const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple, error);
  if (target == nullptr) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = std::format("Cannot initialize target '{}': {}", triple, error),
      .span = {},
    });
  }

  llvm::TargetOptions targetOptions;
  auto relocationModel = std::optional<llvm::Reloc::Model>();
  auto machine = std::unique_ptr<llvm::TargetMachine>(
    target->createTargetMachine(triple, "generic", "", targetOptions, relocationModel)
  );
  if (!machine) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = "Failed to create LLVM target machine.",
      .span = {},
    });
  }
  return machine;
}

auto findLLVMTool(const std::string &toolName) -> std::optional<std::filesystem::path> {
  if (const char *binEnv = std::getenv("THAG_LLVM_BIN"); binEnv != nullptr && *binEnv != '\0') {
    const auto candidate = std::filesystem::path(binEnv) / toolName;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }

  if (const char *llvmDir = std::getenv("LLVM_DIR"); llvmDir != nullptr && *llvmDir != '\0') {
    auto base = std::filesystem::path(llvmDir);
    if (base.filename() == "llvm") {
      base = base.parent_path().parent_path().parent_path();
    }
    const auto candidate = base / "bin" / toolName;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }

  const auto localRoot = std::filesystem::current_path() / "llvm";
  if (std::filesystem::exists(localRoot)) {
    for (const auto &entry : std::filesystem::directory_iterator(localRoot)) {
      if (!entry.is_directory()) {
        continue;
      }
      const auto candidate = entry.path() / "bin" / toolName;
      if (std::filesystem::exists(candidate)) {
        return candidate;
      }
    }
  }

  if (auto resolved = llvm::sys::findProgramByName(toolName)) {
    return std::filesystem::path(*resolved);
  }
  return std::nullopt;
}

auto runTool(const std::filesystem::path &tool, const std::vector<std::string> &arguments, std::string_view stage)
  -> Result<void, Diagnostic> {
  std::vector<std::string> argvStrings {};
  argvStrings.reserve(arguments.size() + 1);
  argvStrings.push_back(tool.string());
  argvStrings.insert(argvStrings.end(), arguments.begin(), arguments.end());

  llvm::SmallVector<llvm::StringRef, 16> argvRefs {};
  argvRefs.reserve(argvStrings.size());
  for (const auto &arg : argvStrings) {
    argvRefs.push_back(arg);
  }

  std::string errorMessage {};
  const int exitCode = llvm::sys::ExecuteAndWait(
    tool.string(),
    argvRefs,
    std::nullopt,
    {},
    0,
    0,
    &errorMessage
  );

  if (exitCode != 0) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = std::format("{} failed with exit code {}: {}", stage, exitCode, errorMessage),
      .span = {},
    });
  }
  return {};
}

auto linkExecutable(
  const DriverOptions &options,
  const std::filesystem::path &objectPath,
  const std::filesystem::path &outputPath,
  const std::filesystem::path &runtimeLibPath
) -> Result<void, Diagnostic> {
#if defined(_WIN32)
  auto lld = findLLVMTool("lld-link.exe");
  if (!lld) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = "Cannot find lld-link.exe. Set THAG_LLVM_BIN or LLVM_DIR.",
      .span = {},
    });
  }

  std::vector<std::string> args {
    "/NOLOGO",
    std::format("/OUT:{}", outputPath.string()),
    "/SUBSYSTEM:CONSOLE",
    objectPath.string(),
    runtimeLibPath.string(),
  };

  if (options.release) {
    args.push_back("/OPT:REF");
    args.push_back("/OPT:ICF");
    args.push_back("/DEBUG:NONE");
    args.push_back("/INCREMENTAL:NO");
  }

  auto linkResult = runTool(*lld, args, "lld-link");
  if (!linkResult) {
    return std::unexpected(linkResult.error());
  }

  if (options.release) {
    auto strip = findLLVMTool("llvm-strip.exe");
    if (!strip) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Cannot find llvm-strip.exe for --release mode.",
        .span = {},
      });
    }

    auto stripResult = runTool(*strip, {"--strip-all", outputPath.string()}, "llvm-strip");
    if (!stripResult) {
      return std::unexpected(stripResult.error());
    }
  }

  return {};
#else
  auto cxx = findLLVMTool("clang++");
  if (!cxx) {
    return std::unexpected(Diagnostic {
      .code = ErrorCode::CodegenError,
      .message = "Cannot find clang++. Set THAG_LLVM_BIN or LLVM_DIR.",
      .span = {},
    });
  }

  std::vector<std::string> args {
    "-no-pie",
    objectPath.string(),
    runtimeLibPath.string(),
    "-o",
    outputPath.string(),
  };
#if defined(__APPLE__)
  std::string llvmRoot {};
  if (const char *llvmDir = std::getenv("LLVM_DIR"); llvmDir != nullptr && *llvmDir != '\0') {
    auto path = std::filesystem::path(llvmDir);
    if (path.filename() == "llvm") {
      path = path.parent_path().parent_path().parent_path();
    }
    llvmRoot = path.string();
  } else if (std::filesystem::exists("/opt/homebrew/opt/llvm/lib/c++")) {
    llvmRoot = "/opt/homebrew/opt/llvm";
  } else if (std::filesystem::exists("/usr/local/opt/llvm/lib/c++")) {
    llvmRoot = "/usr/local/opt/llvm";
  }

  args.push_back("-stdlib=libc++");
  if (!llvmRoot.empty()) {
    args.push_back("-L" + llvmRoot + "/lib/c++");
    args.push_back("-Wl,-rpath," + llvmRoot + "/lib/c++");
  }
  args.push_back("-lc++");
  args.push_back("-lc++abi");
#else
  args.push_back("-lstdc++");
#endif

  auto linkResult = runTool(*cxx, args, "clang++");
  if (!linkResult) {
    return std::unexpected(linkResult.error());
  }

  if (options.release) {
    auto strip = findLLVMTool("llvm-strip");
    if (!strip) {
      return std::unexpected(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = "Cannot find llvm-strip for --release mode.",
        .span = {},
      });
    }

    auto stripResult = runTool(*strip, {"--strip-all", outputPath.string()}, "llvm-strip");
    if (!stripResult) {
      return std::unexpected(stripResult.error());
    }
  }

  return {};
#endif
}

} // namespace

auto Driver::run(const std::vector<std::string> &args) -> int {
  auto options = parseArgs(args);
  if (!options) {
    printDiagnostic(options.error());
    return 1;
  }

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  auto targetMachine = createTargetMachine(options.value());
  if (!targetMachine) {
    printDiagnostic(targetMachine.error());
    return 1;
  }

  auto rootPath = std::filesystem::weakly_canonical(std::filesystem::path(options->inputFile));
  auto moduleAst = parseModuleFile(rootPath);
  if (!moduleAst) {
    printDiagnostic(moduleAst.error());
    return 1;
  }

  std::unordered_map<std::string, ModuleCacheEntry> cache {};
  std::unordered_set<std::string> loading {};
  std::vector<std::unique_ptr<StructDecl>> importedStructs {};
  std::vector<std::unique_ptr<FunctionDecl>> importedFunctions {};
  std::unordered_map<std::string, ImportExportMap> rootAliases {};

  const auto baseDir = rootPath.parent_path();
  for (const auto &imp : moduleAst.value()->imports) {
    auto resolved = resolveImportPath(baseDir, imp.path);
    if (!resolved) {
      printDiagnostic(resolved.error());
      return 1;
    }
    auto imported = loadImportedModule(
      resolved.value(),
      cache,
      loading,
      importedStructs,
      importedFunctions
    );
    if (!imported) {
      printDiagnostic(imported.error());
      return 1;
    }
    rootAliases[imp.alias] = imported.value();
  }

  rewriteQualifiedCallsInModule(*moduleAst.value(), rootAliases);
  moduleAst.value()->imports.clear();

  for (auto &st : importedStructs) {
    moduleAst.value()->structs.insert(moduleAst.value()->structs.begin(), std::move(st));
  }
  for (auto &fn : importedFunctions) {
    moduleAst.value()->functions.insert(moduleAst.value()->functions.begin(), std::move(fn));
  }

  SemanticAnalyzer semantic {};
  auto typed = semantic.analyze(std::move(moduleAst.value()));
  if (!typed) {
    printDiagnostic(typed.error());
    return 1;
  }

  llvm::LLVMContext context;
  IRGenerator generator {context};
  auto module = generator.lower(typed.value(), options->inputFile);
  if (!module) {
    printDiagnostic(module.error());
    return 1;
  }

#if LLVM_VERSION_MAJOR >= 21
  module.value()->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));
#else
  module.value()->setTargetTriple(llvm::sys::getDefaultTargetTriple());
#endif
  module.value()->setDataLayout(targetMachine.value()->createDataLayout());

  auto opt = BackendPipeline::optimizeModule(*module.value(), options->optLevel);
  if (!opt) {
    printDiagnostic(opt.error());
    return 1;
  }

  if (options->emitIR) {
    auto result = BackendPipeline::emitIR(*module.value(), options->outputFile);
    if (!result) {
      printDiagnostic(result.error());
      return 1;
    }
  }

  const auto outputPath = std::filesystem::path(options->outputFile);
  const auto objectPath = (options->mode == DriverMode::BuildExecutable)
#if defined(_WIN32)
    ? outputPath.parent_path() / (outputPath.stem().string() + ".obj")
#else
    ? outputPath.parent_path() / (outputPath.stem().string() + ".o")
#endif
    : outputPath;

  if (options->emitObject || options->mode == DriverMode::BuildExecutable) {
    auto result = BackendPipeline::emitObject(*module.value(), *targetMachine.value(), objectPath.string());
    if (!result) {
      printDiagnostic(result.error());
      return 1;
    }
  }

  if (options->mode == DriverMode::BuildExecutable) {
    const auto thagExe = std::filesystem::absolute(std::filesystem::path(args[0]));
    const auto runtimeLib = thagExe.parent_path()
#if defined(_WIN32)
      / "thag_runtime.lib";
#else
      / "libthag_runtime.a";
#endif
    if (!std::filesystem::exists(runtimeLib)) {
      printDiagnostic(Diagnostic {
        .code = ErrorCode::CodegenError,
        .message = std::format("Missing runtime library '{}'.", runtimeLib.string()),
        .span = {},
      });
      return 1;
    }

    auto linkResult = linkExecutable(options.value(), objectPath, outputPath, runtimeLib);
    if (!linkResult) {
      printDiagnostic(linkResult.error());
      return 1;
    }
  }

#if defined(__cpp_lib_print) && __cpp_lib_print >= 202207L
  std::print("thag: emitted {}\n", options->outputFile);
#else
  std::cout << std::format("thag: emitted {}\n", options->outputFile);
#endif
  return 0;
}

} // namespace thagore
