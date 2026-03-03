#include "thagc/driver/command_handlers.hpp"

#include <cctype>
#include <iostream>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

namespace {

struct SourceLine {
  int number = 0;
  int indent = 0;
  std::string clean;
};

struct IntentEntry {
  std::string kind;
  std::string header;
  std::string goal;
  std::string strategy;
  int line = 0;
};

static bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

static std::string trim(const std::string& text) {
  std::size_t left = 0;
  while (left < text.size() && std::isspace(static_cast<unsigned char>(text[left]))) {
    ++left;
  }
  std::size_t right = text.size();
  while (right > left && std::isspace(static_cast<unsigned char>(text[right - 1]))) {
    --right;
  }
  return text.substr(left, right - left);
}

static int leading_indent(const std::string& line) {
  int indent = 0;
  for (char ch : line) {
    if (ch == ' ') {
      ++indent;
      continue;
    }
    if (ch == '\t') {
      indent += 2;
      continue;
    }
    break;
  }
  return indent;
}

static std::string strip_comments(const std::string& line) {
  std::size_t cut = line.size();
  const std::size_t hash = line.find('#');
  if (hash != std::string::npos) {
    cut = std::min(cut, hash);
  }
  const std::size_t slash = line.find("//");
  if (slash != std::string::npos) {
    cut = std::min(cut, slash);
  }
  return line.substr(0, cut);
}

static std::vector<SourceLine> split_lines(const std::string& source) {
  std::vector<SourceLine> out;
  std::size_t from = 0;
  int line_no = 1;
  while (from <= source.size()) {
    const std::size_t nl = source.find('\n', from);
    const std::size_t end = nl == std::string::npos ? source.size() : nl;
    const std::string raw = source.substr(from, end - from);
    const std::string clean = trim(strip_comments(raw));
    if (!clean.empty()) {
      out.push_back(SourceLine{line_no, leading_indent(raw), clean});
    }
    if (nl == std::string::npos) {
      break;
    }
    from = nl + 1;
    ++line_no;
  }
  return out;
}

static const std::unordered_set<std::string>& supported_goals() {
  static const std::unordered_set<std::string> kGoals = {
      "off",          "auto_plan",             "reduce_sum",                "map_filter_reduce",
      "deduplicate_sorted",                    "binary_search",             "binary_search_sorted",
      "lower_bound_sorted",                    "upper_bound_sorted",        "count_less_sorted",
      "count_less_equal_sorted",               "count_greater_sorted",      "count_greater_equal_sorted",
      "count_equal_sorted",                    "count_not_equal_sorted",    "count_range_sorted",
      "count_outside_range_sorted",            "two_sum_sorted_exists",     "string_contains",
      "dot_product",                           "polynomial_eval",           "fibonacci_dp",
      "tribonacci_dp",                         "factorial_iterative",       "power_fast",
      "gcd_euclid",                            "is_prime_fast",             "count_divisors_sqrt",
      "interval_cover_greedy",                 "bit_peel_iterative",        "sum_squares_formula",
      "sum_cubes_formula",                     "sum_even_squares_formula",  "sum_odd_squares_formula",
      "sum_even_cubes_formula",                "sum_odd_cubes_formula",     "sum_even_formula",
      "sum_odd_formula",                       "sort_ascending",            "search_element",
      "sqrt_bounded_loop",
  };
  return kGoals;
}

static std::vector<IntentEntry> parse_intent_entries(const std::string& source) {
  const std::vector<SourceLine> lines = split_lines(source);
  std::vector<IntentEntry> out;

  std::size_t i = 0;
  while (i < lines.size()) {
    const SourceLine& line = lines[i];
    if (!starts_with(line.clean, "intent ")) {
      ++i;
      continue;
    }

    IntentEntry entry;
    entry.header = line.clean;
    entry.line = line.number;
    if (starts_with(line.clean, "intent func ")) {
      entry.kind = "func";
    } else if (starts_with(line.clean, "intent loop ")) {
      entry.kind = "loop";
    } else if (starts_with(line.clean, "intent calc")) {
      entry.kind = "calc";
    } else if (starts_with(line.clean, "intent block")) {
      entry.kind = "block";
    } else {
      entry.kind = "unknown";
    }

    ++i;
    while (i < lines.size() && lines[i].indent > line.indent) {
      const std::string body = lines[i].clean;
      if (starts_with(body, "goal:") && entry.goal.empty()) {
        entry.goal = trim(body.substr(5));
      } else if (starts_with(body, "strategy:") && entry.strategy.empty()) {
        entry.strategy = trim(body.substr(9));
      }
      ++i;
    }
    out.push_back(std::move(entry));
  }

  return out;
}

static std::string json_escape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char ch : value) {
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

static int print_help() {
  std::cout << "intent usage:\n";
  std::cout << "  thagc intent explain <input.tg> [--json]\n";
  std::cout << "  thagc intent doctor <input.tg>\n";
  return 0;
}

static bool valid_strategy_pattern(const std::string& strategy) {
  if (strategy.empty()) {
    return true;
  }
  static const std::regex kPattern(R"(^[a-z0-9_]+(\.[a-z0-9_]+)+$)");
  return std::regex_match(strategy, kPattern);
}

static int handle_explain(const std::string& input_path, bool as_json) {
  const std::string source = support::read_text_file(input_path);
  const auto entries = parse_intent_entries(source);

  if (as_json) {
    std::cout << "{\n  \"file\": \"" << json_escape(input_path) << "\",\n  \"entries\": [\n";
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      std::cout << "    {\n"
                << "      \"line\": " << e.line << ",\n"
                << "      \"kind\": \"" << json_escape(e.kind) << "\",\n"
                << "      \"header\": \"" << json_escape(e.header) << "\",\n"
                << "      \"goal\": \"" << json_escape(e.goal) << "\",\n"
                << "      \"strategy\": \"" << json_escape(e.strategy) << "\"\n"
                << "    }";
      if (i + 1 < entries.size()) {
        std::cout << ",";
      }
      std::cout << "\n";
    }
    std::cout << "  ]\n}\n";
    return 0;
  }

  if (entries.empty()) {
    std::cout << "intent explain: no intent annotations found in " << input_path << "\n";
    return 0;
  }
  std::cout << "intent explain: " << entries.size() << " intent annotation(s)\n";
  for (const auto& e : entries) {
    std::cout << "- line " << e.line << " [" << e.kind << "] goal=" << (e.goal.empty() ? "<missing>" : e.goal);
    if (!e.strategy.empty()) {
      std::cout << " strategy=" << e.strategy;
    }
    std::cout << "\n";
  }
  return 0;
}

static int handle_doctor(const std::string& input_path) {
  const std::string source = support::read_text_file(input_path);
  const auto entries = parse_intent_entries(source);
  if (entries.empty()) {
    std::cout << "intent doctor: no intent annotations found\n";
    return 0;
  }

  int errors = 0;
  for (const auto& e : entries) {
    if (e.kind == "unknown") {
      std::cerr << input_path << ":" << e.line << ":1: error E_INTENT_001: unsupported intent header '" << e.header
                << "'\n";
      ++errors;
      continue;
    }
    if (e.goal.empty()) {
      std::cerr << input_path << ":" << e.line << ":1: error E_INTENT_002: missing goal for intent block\n";
      ++errors;
      continue;
    }
    if (supported_goals().find(e.goal) == supported_goals().end()) {
      std::cerr << input_path << ":" << e.line << ":1: error E_INTENT_003: unsupported intent goal '" << e.goal
                << "'\n";
      ++errors;
    }
    if (!valid_strategy_pattern(e.strategy)) {
      std::cerr << input_path << ":" << e.line << ":1: error E_INTENT_004: invalid strategy '" << e.strategy
                << "' (expected dotted identifier path)\n";
      ++errors;
    }
    if (e.goal == "off" && !e.strategy.empty()) {
      std::cerr << input_path << ":" << e.line << ":1: error E_INTENT_005: strategy cannot be set when goal is 'off'\n";
      ++errors;
    }
  }

  if (errors == 0) {
    std::cout << "intent doctor: OK (" << entries.size() << " annotation(s))\n";
    return 0;
  }
  std::cerr << "intent doctor: failed with " << errors << " issue(s)\n";
  return 1;
}

}  // namespace

int handle_intent(const ParsedCommand& cmd) {
  if (cmd.args.empty()) {
    return print_help();
  }
  const std::string sub = cmd.args[0];
  if (sub == "help" || sub == "--help") {
    return print_help();
  }
  if (cmd.args.size() < 2) {
    std::cerr << "ERROR: missing input file for intent " << sub << "\n";
    return 1;
  }
  const std::string input_path = cmd.args[1];
  bool as_json = false;
  for (std::size_t i = 2; i < cmd.args.size(); ++i) {
    if (cmd.args[i] == "--json") {
      as_json = true;
    }
  }
  if (sub == "explain") {
    return handle_explain(input_path, as_json);
  }
  if (sub == "doctor") {
    return handle_doctor(input_path);
  }
  std::cerr << "ERROR: unknown intent subcommand '" << sub << "'\n";
  return 1;
}

}  // namespace thagc::driver
