#include "internal.hpp"

namespace thagc::syntax {

std::string function_name_from_header(const std::string& line) {
  const std::size_t func_pos = line.find("func ");
  if (func_pos == std::string::npos) {
    return "";
  }
  std::size_t start = func_pos + 5;
  while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
    ++start;
  }
  std::size_t end = start;
  while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_')) {
    ++end;
  }
  return line.substr(start, end - start);
}

std::vector<ParsedFunctionParam> function_param_specs_from_header(const std::string& line) {
  std::vector<ParsedFunctionParam> out;
  const std::size_t func_pos = line.find("func ");
  if (func_pos == std::string::npos) {
    return out;
  }
  const std::size_t lparen = line.find('(', func_pos + 5);
  const std::size_t rparen = line.find(')', lparen == std::string::npos ? 0 : lparen + 1);
  if (lparen == std::string::npos || rparen == std::string::npos || rparen < lparen) {
    return out;
  }
  const std::string param_block = line.substr(lparen + 1, rparen - lparen - 1);
  std::size_t i = 0;
  while (i < param_block.size()) {
    std::size_t comma = param_block.find(',', i);
    if (comma == std::string::npos) {
      comma = param_block.size();
    }
    std::string part = trim(param_block.substr(i, comma - i));
    if (!part.empty()) {
      const std::size_t colon = part.find(':');
      ParsedFunctionParam parsed;
      if (colon == std::string::npos) {
        parsed.name = trim(part);
      } else {
        parsed.name = trim(part.substr(0, colon));
        parsed.type = trim(part.substr(colon + 1));
      }
      if (!parsed.name.empty()) {
        out.push_back(std::move(parsed));
      }
    }
    i = comma + 1;
  }
  return out;
}

std::vector<std::string> function_params_from_header(const std::string& line) {
  std::vector<std::string> out;
  for (const auto& spec : function_param_specs_from_header(line)) {
    out.push_back(spec.name);
  }
  return out;
}

std::vector<std::string> function_param_types_from_header(const std::string& line) {
  std::vector<std::string> out;
  for (const auto& spec : function_param_specs_from_header(line)) {
    out.push_back(spec.type);
  }
  return out;
}

std::string method_name_from_line(const std::string& line) {
  std::string clean = trim(line);
  if (starts_with(clean, "pub ")) {
    clean = trim(clean.substr(4));
  }
  if (starts_with(clean, "async ")) {
    clean = trim(clean.substr(6));
  }
  if (!starts_with(clean, "func ")) {
    return "";
  }
  return function_name_from_header(clean);
}

std::string function_return_type_from_header(const std::string& line) {
  const std::size_t arrow = line.find("->");
  if (arrow == std::string::npos) {
    return "";
  }
  std::size_t start = arrow + 2;
  while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
    ++start;
  }
  std::size_t end = line.rfind(':');
  if (end == std::string::npos || end <= start) {
    end = line.size();
  }
  return trim(line.substr(start, end - start));
}

bool parse_impl_for_header(const std::string& line, std::string& trait_name, std::string& type_name) {
  std::string clean = trim(line);
  if (!starts_with(clean, "impl ") || !ends_with(clean, ":")) {
    return false;
  }
  clean = trim(clean.substr(5, clean.size() - 6));
  const std::size_t for_pos = clean.find(" for ");
  if (for_pos == std::string::npos) {
    return false;
  }
  trait_name = trim(clean.substr(0, for_pos));
  type_name = trim(clean.substr(for_pos + 5));
  return !trait_name.empty() && !type_name.empty();
}

bool parse_impl_type_header(const std::string& line, std::string& type_name) {
  std::string clean = trim(line);
  if (!starts_with(clean, "impl ") || !ends_with(clean, ":")) {
    return false;
  }
  clean = trim(clean.substr(5, clean.size() - 6));
  if (clean.empty() || clean.find(" for ") != std::string::npos) {
    return false;
  }
  type_name = clean;
  return true;
}

std::string enum_variant_name_from_line(const std::string& line) {
  std::string clean = trim(line);
  if (clean.empty() || clean == ":") {
    return "";
  }
  std::size_t end = 0;
  while (end < clean.size() &&
         (std::isalnum(static_cast<unsigned char>(clean[end])) || clean[end] == '_')) {
    ++end;
  }
  if (end == 0) {
    return "";
  }
  return clean.substr(0, end);
}

std::string enum_variant_payload_type_from_line(const std::string& line) {
  const std::string clean = trim(line);
  const std::size_t lparen = clean.find('(');
  const std::size_t rparen = clean.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || rparen <= lparen + 1) {
    return "";
  }
  std::string payload = trim(clean.substr(lparen + 1, rparen - lparen - 1));
  if (payload.empty()) {
    return "";
  }
  const std::size_t colon = payload.find(':');
  if (colon != std::string::npos) {
    payload = trim(payload.substr(colon + 1));
  }
  return payload;
}

std::string enum_name_from_header(const std::string& line) {
  if (!starts_with(line, "enum ") || !ends_with(line, ":")) {
    return "";
  }
  const std::string body = trim(line.substr(5, line.size() - 6));
  if (body.empty()) {
    return "";
  }
  std::size_t end = 0;
  while (end < body.size() && (std::isalnum(static_cast<unsigned char>(body[end])) || body[end] == '_')) {
    ++end;
  }
  if (end == 0) {
    return "";
  }
  return body.substr(0, end);
}

std::string struct_name_from_header(const std::string& line) {
  if (!starts_with(line, "struct ") || !ends_with(line, ":")) {
    return "";
  }
  const std::string body = trim(line.substr(7, line.size() - 8));
  if (body.empty()) {
    return "";
  }
  std::size_t end = 0;
  while (end < body.size() && (std::isalnum(static_cast<unsigned char>(body[end])) || body[end] == '_')) {
    ++end;
  }
  if (end == 0) {
    return "";
  }
  return body.substr(0, end);
}

std::string struct_field_name_from_line(const std::string& line) {
  auto is_ident_start = [](char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
  };
  auto is_ident_body_local = [](char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
  };
  std::string clean = trim(line);
  if (clean.empty()) {
    return "";
  }
  const std::size_t colon = clean.find(':');
  if (colon != std::string::npos) {
    clean = trim(clean.substr(0, colon));
  }
  if (clean.empty() || !is_ident_start(clean[0])) {
    return "";
  }
  for (std::size_t i = 1; i < clean.size(); ++i) {
    if (!is_ident_body_local(clean[i])) {
      return "";
    }
  }
  return clean;
}

std::string struct_field_type_from_line(const std::string& line) {
  const std::size_t colon = line.find(':');
  if (colon == std::string::npos) {
    return "i32";
  }
  const std::string ty = trim(line.substr(colon + 1));
  return ty.empty() ? "i32" : ty;
}

bool parse_state_header(const std::string& line, std::string& state_name,
                        std::vector<std::string>& variants, std::string& error) {
  auto is_identifier_start_local = [](char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
  };
  auto is_identifier_body_local = [](char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
  };
  state_name.clear();
  variants.clear();
  error.clear();
  if (!starts_with(line, "state ")) {
    return false;
  }
  const std::size_t colon = line.find(':');
  if (colon == std::string::npos || colon <= 6 || colon + 1 >= line.size()) {
    error = "state declaration must follow `state Name: Variant | Variant`";
    return true;
  }
  state_name = trim(line.substr(6, colon - 6));
  if (state_name.empty()) {
    error = "state declaration requires a state set name";
    return true;
  }
  if (!is_identifier_start_local(state_name[0])) {
    error = "state set name must be an identifier";
    return true;
  }
  for (std::size_t i = 1; i < state_name.size(); ++i) {
    if (!is_identifier_body_local(state_name[i])) {
      error = "state set name must be an identifier";
      return true;
    }
  }
  const std::string right = trim(line.substr(colon + 1));
  if (right.empty()) {
    error = "state declaration requires at least one variant";
    return true;
  }
  std::size_t i = 0;
  while (i < right.size()) {
    std::size_t bar = right.find('|', i);
    if (bar == std::string::npos) {
      bar = right.size();
    }
    const std::string variant = trim(right.substr(i, bar - i));
    if (variant.empty()) {
      error = "state declaration contains empty variant";
      return true;
    }
    if (!is_identifier_start_local(variant[0])) {
      error = "state variant '" + variant + "' is not an identifier";
      return true;
    }
    for (std::size_t k = 1; k < variant.size(); ++k) {
      if (!is_identifier_body_local(variant[k])) {
        error = "state variant '" + variant + "' is not an identifier";
        return true;
      }
    }
    variants.push_back(variant);
    i = bar + 1;
  }
  if (variants.size() < 2) {
    error = "state declaration requires at least two variants";
    return true;
  }
  return true;
}

bool parse_extern_function_declaration(const std::string& line, AstExternFunction& out) {
  const std::string clean = trim(line);
  if (!starts_with(clean, "extern func ")) {
    return false;
  }
  const std::size_t name_start = 12;
  const std::size_t lparen = clean.find('(', name_start);
  const std::size_t rparen = clean.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || rparen < lparen) {
    return false;
  }
  out.name = trim(clean.substr(name_start, lparen - name_start));
  if (out.name.empty()) {
    return false;
  }

  const std::string params = trim(clean.substr(lparen + 1, rparen - lparen - 1));
  out.param_types.clear();
  std::size_t i = 0;
  while (i < params.size()) {
    std::size_t comma = params.find(',', i);
    if (comma == std::string::npos) {
      comma = params.size();
    }
    std::string part = trim(params.substr(i, comma - i));
    if (!part.empty()) {
      const std::size_t colon = part.find(':');
      if (colon != std::string::npos && colon + 1 < part.size()) {
        part = trim(part.substr(colon + 1));
      }
      if (!part.empty()) {
        out.param_types.push_back(part);
      }
    }
    i = comma + 1;
  }

  const std::size_t arrow = clean.find("->", rparen);
  if (arrow == std::string::npos) {
    out.return_type = "i32";
  } else {
    out.return_type = trim(clean.substr(arrow + 2));
  }
  return !out.return_type.empty();
}

}  // namespace thagc::syntax
