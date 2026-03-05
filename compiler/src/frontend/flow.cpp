#include "internal.hpp"

namespace thagc::syntax {

std::string flow_name_from_header(const std::string& header) {
  if (!starts_with(header, "flow ") || !ends_with(header, ":")) {
    return "";
  }
  std::string body = trim(header.substr(5, header.size() - 6));
  if (body.empty()) {
    return "";
  }
  const std::size_t lparen = body.find('(');
  if (lparen != std::string::npos) {
    body = trim(body.substr(0, lparen));
  }
  if (!is_identifier(body)) {
    return "";
  }
  return body;
}

static bool parse_non_negative_i32(const std::string& text, int& out) {
  const std::string clean = trim(text);
  if (clean.empty()) {
    return false;
  }
  for (char ch : clean) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  out = std::stoi(clean);
  return true;
}

static bool parse_timeout_ms(const std::string& text, int& out) {
  std::string clean = trim(text);
  if (clean.empty()) {
    return false;
  }
  int multiplier = 1;
  if (ends_with(clean, "ms")) {
    clean = trim(clean.substr(0, clean.size() - 2));
  } else if (ends_with(clean, "s")) {
    clean = trim(clean.substr(0, clean.size() - 1));
    multiplier = 1000;
  }
  int value = 0;
  if (!parse_non_negative_i32(clean, value)) {
    return false;
  }
  out = value * multiplier;
  return true;
}

bool parse_flow_step_header(const std::string& line, AstFlowStep& out_step, std::string& error) {
  if (!starts_with(line, "step ")) {
    error = "flow step must start with 'step'";
    return false;
  }
  const std::string body = trim(line.substr(5));
  if (body.empty()) {
    error = "flow step requires action expression";
    return false;
  }
  const std::size_t eq = body.find('=');
  if (eq == std::string::npos) {
    out_step.name.clear();
    out_step.action = body;
    return true;
  }
  const std::string name = trim(body.substr(0, eq));
  const std::string action = trim(body.substr(eq + 1));
  if (!is_identifier(name)) {
    error = "flow step name must be an identifier";
    return false;
  }
  if (action.empty()) {
    error = "flow step requires action expression";
    return false;
  }
  out_step.name = name;
  out_step.action = action;
  return true;
}

void parse_flow_step_directive(AstProgram& program, AstFlowStep& step, const SourceLine& line) {
  const std::string clean = trim(line.clean);
  if (starts_with(clean, "undo ")) {
    if (!step.undo_action.empty()) {
      add_parse_error(program, line.number, "flow step cannot declare multiple undo directives");
      return;
    }
    step.undo_action = trim(clean.substr(5));
    if (step.undo_action.empty()) {
      add_parse_error(program, line.number, "undo directive requires expression");
    }
    return;
  }
  if (starts_with(clean, "retry ")) {
    if (step.has_retry) {
      add_parse_error(program, line.number, "flow step cannot declare multiple retry directives");
      return;
    }
    int retry = 0;
    if (!parse_non_negative_i32(clean.substr(6), retry)) {
      add_parse_error(program, line.number, "retry directive expects non-negative integer");
      return;
    }
    step.retry_count = retry;
    step.has_retry = true;
    return;
  }
  if (starts_with(clean, "timeout ")) {
    if (step.has_timeout) {
      add_parse_error(program, line.number, "flow step cannot declare multiple timeout directives");
      return;
    }
    int timeout_ms = 0;
    if (!parse_timeout_ms(clean.substr(8), timeout_ms)) {
      add_parse_error(program, line.number,
                      "timeout directive expects integer milliseconds or seconds suffix");
      return;
    }
    step.timeout_ms = timeout_ms;
    step.has_timeout = true;
    return;
  }
  if (clean == "idempotent") {
    step.idempotent = true;
    return;
  }
  if (clean == "irreversible") {
    step.irreversible = true;
    return;
  }
  add_parse_error(program, line.number, "unsupported flow step directive: '" + clean + "'");
}

std::string substitute_known_identifiers(const std::string& expr,
                                         const std::unordered_map<std::string, std::string>& known) {
  std::string out;
  bool in_string = false;
  bool escape = false;
  for (std::size_t i = 0; i < expr.size();) {
    const char ch = expr[i];
    if (in_string) {
      out.push_back(ch);
      if (escape) {
        escape = false;
      } else if (ch == '\\') {
        escape = true;
      } else if (ch == '"') {
        in_string = false;
      }
      ++i;
      continue;
    }
    if (ch == '"') {
      in_string = true;
      out.push_back(ch);
      ++i;
      continue;
    }
    if (!(std::isalpha(static_cast<unsigned char>(ch)) || ch == '_')) {
      out.push_back(ch);
      ++i;
      continue;
    }
    const std::size_t start = i;
    ++i;
    while (i < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_')) {
      ++i;
    }
    const std::string ident = expr.substr(start, i - start);
    auto it = known.find(ident);
    if (it != known.end()) {
      out += "(" + it->second + ")";
    } else {
      out += ident;
    }
  }
  return out;
}

}  // namespace thagc::syntax
