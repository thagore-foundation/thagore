#include "thagc/driver/command_handlers.hpp"

#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace thagc::driver {

namespace {

struct LspMessage {
  std::string json;
  bool ok = false;
};

static bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

static std::string trim_copy(const std::string& text) {
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

static std::string json_escape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char ch : text) {
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

static void write_lsp_payload(const std::string& payload) {
  std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
  std::cout.flush();
}

static std::string extract_json_field_raw(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  std::size_t pos = json.find(needle);
  if (pos == std::string::npos) {
    return "";
  }
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) {
    return "";
  }
  ++pos;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }
  if (pos >= json.size()) {
    return "";
  }
  if (json[pos] == '"') {
    std::size_t end = pos + 1;
    bool escape = false;
    while (end < json.size()) {
      const char ch = json[end];
      if (escape) {
        escape = false;
      } else if (ch == '\\') {
        escape = true;
      } else if (ch == '"') {
        ++end;
        break;
      }
      ++end;
    }
    return json.substr(pos, end - pos);
  }
  if (json[pos] == '{' || json[pos] == '[') {
    const char open = json[pos];
    const char close = open == '{' ? '}' : ']';
    int depth = 0;
    std::size_t end = pos;
    bool in_string = false;
    bool escape = false;
    while (end < json.size()) {
      const char ch = json[end];
      if (in_string) {
        if (escape) {
          escape = false;
        } else if (ch == '\\') {
          escape = true;
        } else if (ch == '"') {
          in_string = false;
        }
      } else {
        if (ch == '"') {
          in_string = true;
        } else if (ch == open) {
          ++depth;
        } else if (ch == close) {
          --depth;
          if (depth == 0) {
            ++end;
            break;
          }
        }
      }
      ++end;
    }
    return json.substr(pos, end - pos);
  }
  std::size_t end = pos;
  while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != '\n' && json[end] != '\r') {
    ++end;
  }
  return trim_copy(json.substr(pos, end - pos));
}

static std::string unquote_json(const std::string& maybe_quoted) {
  if (maybe_quoted.size() < 2 || maybe_quoted.front() != '"' || maybe_quoted.back() != '"') {
    return maybe_quoted;
  }
  std::string out;
  out.reserve(maybe_quoted.size() - 2);
  bool escape = false;
  for (std::size_t i = 1; i + 1 < maybe_quoted.size(); ++i) {
    const char ch = maybe_quoted[i];
    if (escape) {
      switch (ch) {
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        default:
          out.push_back(ch);
          break;
      }
      escape = false;
      continue;
    }
    if (ch == '\\') {
      escape = true;
      continue;
    }
    out.push_back(ch);
  }
  return out;
}

static std::string extract_json_string(const std::string& json, const std::string& key) {
  return unquote_json(extract_json_field_raw(json, key));
}

static int extract_json_int(const std::string& json, const std::string& key) {
  const std::string raw = extract_json_field_raw(json, key);
  if (raw.empty()) {
    return 0;
  }
  try {
    return std::stoi(raw);
  } catch (...) {
    return 0;
  }
}

static LspMessage read_lsp_message() {
  LspMessage msg;
  std::string line;
  int content_length = 0;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      break;
    }
    if (starts_with(line, "Content-Length:")) {
      const std::string value = trim_copy(line.substr(std::string("Content-Length:").size()));
      try {
        content_length = std::stoi(value);
      } catch (...) {
        content_length = 0;
      }
    }
  }
  if (content_length <= 0) {
    return msg;
  }
  msg.json.resize(static_cast<std::size_t>(content_length));
  std::cin.read(msg.json.data(), content_length);
  msg.ok = static_cast<int>(std::cin.gcount()) == content_length;
  return msg;
}

static std::vector<std::string> split_lines(const std::string& source) {
  std::vector<std::string> out;
  std::istringstream in(source);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    out.push_back(line);
  }
  return out;
}

static bool is_ident_char(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

static std::string word_at(const std::string& line, int column) {
  if (line.empty()) {
    return "";
  }
  int col = column;
  if (col < 0) {
    col = 0;
  }
  if (col >= static_cast<int>(line.size())) {
    col = static_cast<int>(line.size()) - 1;
  }
  int left = col;
  while (left >= 0 && !is_ident_char(line[static_cast<std::size_t>(left)])) {
    --left;
  }
  if (left < 0) {
    return "";
  }
  int start = left;
  while (start > 0 && is_ident_char(line[static_cast<std::size_t>(start - 1)])) {
    --start;
  }
  int end = left;
  while (end + 1 < static_cast<int>(line.size()) && is_ident_char(line[static_cast<std::size_t>(end + 1)])) {
    ++end;
  }
  return line.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start + 1));
}

static bool find_definition_line(const std::vector<std::string>& lines, const std::string& symbol, int& out_line,
                                 int& out_char) {
  if (symbol.empty()) {
    return false;
  }
  const std::vector<std::string> patterns = {
      "func " + symbol + "(", "let " + symbol, "struct " + symbol + ":", "enum " + symbol + ":", "type " + symbol + " ="};
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const std::string& line = lines[i];
    for (const std::string& pattern : patterns) {
      const std::size_t pos = line.find(pattern);
      if (pos != std::string::npos) {
        out_line = static_cast<int>(i);
        out_char = static_cast<int>(pos + (pattern.rfind(symbol, 0) == 0 ? 0 : pattern.find(symbol)));
        return true;
      }
    }
  }
  return false;
}

}  // namespace

int handle_lsp(const ParsedCommand& cmd) {
  bool stdio_mode = false;
  for (const std::string& arg : cmd.args) {
    if (arg == "--stdio") {
      stdio_mode = true;
      break;
    }
  }
  if (!stdio_mode) {
    std::cerr << "ERROR: lsp currently supports --stdio only\n";
    return 1;
  }

  std::unordered_map<std::string, std::string> documents;
  bool running = true;
  while (running) {
    const LspMessage msg = read_lsp_message();
    if (!msg.ok) {
      break;
    }
    const std::string id_raw = extract_json_field_raw(msg.json, "id");
    const std::string method = extract_json_string(msg.json, "method");

    if (method == "initialize") {
      std::string payload = "{"
                            "\"jsonrpc\":\"2.0\","
                            "\"id\":" +
                            id_raw +
                            ",\"result\":{\"capabilities\":{"
                            "\"textDocumentSync\":1,"
                            "\"definitionProvider\":true,"
                            "\"completionProvider\":{\"triggerCharacters\":[\".\"]}"
                            "}}}";
      write_lsp_payload(payload);
      continue;
    }
    if (method == "initialized") {
      continue;
    }
    if (method == "shutdown") {
      std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":null}";
      write_lsp_payload(payload);
      continue;
    }
    if (method == "exit") {
      running = false;
      continue;
    }
    if (method == "textDocument/didOpen") {
      const std::string uri = extract_json_string(msg.json, "uri");
      const std::string text = extract_json_string(msg.json, "text");
      if (!uri.empty()) {
        documents[uri] = text;
      }
      continue;
    }
    if (method == "textDocument/didChange") {
      const std::string uri = extract_json_string(msg.json, "uri");
      const std::string text = extract_json_string(msg.json, "text");
      if (!uri.empty() && !text.empty()) {
        documents[uri] = text;
      }
      continue;
    }
    if (method == "textDocument/completion") {
      std::string payload = "{"
                            "\"jsonrpc\":\"2.0\","
                            "\"id\":" +
                            id_raw +
                            ",\"result\":{\"isIncomplete\":false,\"items\":["
                            "{\"label\":\"func\",\"kind\":14},"
                            "{\"label\":\"let\",\"kind\":14},"
                            "{\"label\":\"if\",\"kind\":14},"
                            "{\"label\":\"while\",\"kind\":14},"
                            "{\"label\":\"for\",\"kind\":14},"
                            "{\"label\":\"match\",\"kind\":14},"
                            "{\"label\":\"struct\",\"kind\":14},"
                            "{\"label\":\"enum\",\"kind\":14},"
                            "{\"label\":\"state\",\"kind\":14}"
                            "]}}";
      write_lsp_payload(payload);
      continue;
    }
    if (method == "textDocument/definition") {
      const std::string uri = extract_json_string(msg.json, "uri");
      const int line = extract_json_int(msg.json, "line");
      const int character = extract_json_int(msg.json, "character");
      auto it = documents.find(uri);
      if (it == documents.end()) {
        const std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}";
        write_lsp_payload(payload);
        continue;
      }
      const std::vector<std::string> lines = split_lines(it->second);
      if (line < 0 || line >= static_cast<int>(lines.size())) {
        const std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}";
        write_lsp_payload(payload);
        continue;
      }
      const std::string symbol = word_at(lines[static_cast<std::size_t>(line)], character);
      int def_line = 0;
      int def_char = 0;
      if (!find_definition_line(lines, symbol, def_line, def_char)) {
        const std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":[]}";
        write_lsp_payload(payload);
        continue;
      }
      const std::string payload =
          "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
          ",\"result\":[{\"uri\":\"" + json_escape(uri) + "\",\"range\":{\"start\":{\"line\":" +
          std::to_string(def_line) + ",\"character\":" + std::to_string(def_char) +
          "},\"end\":{\"line\":" + std::to_string(def_line) + ",\"character\":" + std::to_string(def_char + 1) +
          "}}}]}";
      write_lsp_payload(payload);
      continue;
    }
    if (!id_raw.empty()) {
      const std::string payload =
          "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw +
          ",\"error\":{\"code\":-32601,\"message\":\"method not found\"}}";
      write_lsp_payload(payload);
    }
  }
  return 0;
}

}  // namespace thagc::driver
