#include "thagc/driver/command_handlers.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "thagc/driver/common.hpp"
#include "thagc/driver/pipeline.hpp"
#include "thagc/shared/diag.hpp"
#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

namespace {

struct StateFinding {
  support::Diagnostic diagnostic;
};

static bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
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

static std::filesystem::path unique_temp_root() {
  const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() / ("thag-state-" + std::to_string(static_cast<long long>(stamp)));
}

static bool is_state_code(const std::string& code) {
  return starts_with(code, "E_STATE_") || starts_with(code, "W_STATE_") || starts_with(code, "E_TYPESTATE_");
}

static std::vector<StateFinding> collect_state_findings(const support::DiagnosticSink& diag) {
  std::vector<StateFinding> out;
  for (const auto& d : diag.diagnostics()) {
    if (is_state_code(d.code)) {
      out.push_back(StateFinding{d});
    }
  }
  return out;
}

static std::string findings_json(const std::string& source_path, const std::vector<StateFinding>& findings) {
  std::string out = "{\n";
  out += "  \"file\": \"" + json_escape(source_path) + "\",\n";
  out += "  \"findings\": [\n";
  for (std::size_t i = 0; i < findings.size(); ++i) {
    const auto& d = findings[i].diagnostic;
    out += "    {\n";
    out += "      \"level\": \"" +
           std::string(d.level == support::DiagnosticLevel::Warning ? "warning" : "error") + "\",\n";
    out += "      \"code\": \"" + json_escape(d.code) + "\",\n";
    out += "      \"message\": \"" + json_escape(d.message) + "\",\n";
    out += "      \"file\": \"" + json_escape(d.file) + "\",\n";
    out += "      \"line\": " + std::to_string(d.line) + ",\n";
    out += "      \"column\": " + std::to_string(d.column) + ",\n";
    out += "      \"fix\": \"" + json_escape(support::diagnostic_fix_suggestion(d)) + "\"\n";
    out += "    }";
    if (i + 1 < findings.size()) {
      out += ",";
    }
    out += "\n";
  }
  out += "  ]\n";
  out += "}\n";
  return out;
}

static void print_usage() {
  std::cout << "state usage:\n";
  std::cout << "  thagc state explain <input.tg> [--json] [--out <file>]\n";
  std::cout << "  thagc state doctor <input.tg>\n";
}

static bool run_state_analysis(const std::string& input_path, const CompilerPipeline& pipeline, support::DiagnosticSink& diag,
                               std::filesystem::path& temp_root) {
  temp_root = unique_temp_root();
  std::error_code ec;
  std::filesystem::create_directories(temp_root, ec);
  if (ec) {
    diag.error("E_STATE_DRIVER_001", "cannot create temp directory for state analysis: " + ec.message());
    return false;
  }

  BuildOptions options;
  options.input_path = input_path;
#if defined(_WIN32)
  options.output_path = (temp_root / "state-analysis.exe").string();
#else
  options.output_path = (temp_root / "state-analysis.bin").string();
#endif
  options.emit_llvm = true;
  options.llvm_ir_path = (temp_root / "state-analysis.ll").string();
  if (!apply_target_config(options, "", diag)) {
    return false;
  }
  return pipeline.build(options, diag);
}

static void cleanup_temp(const std::filesystem::path& root) {
  std::error_code ec;
  if (!root.empty()) {
    std::filesystem::remove_all(root, ec);
  }
}

static int print_non_state_errors(const support::DiagnosticSink& diag) {
  int count = 0;
  for (const auto& d : diag.diagnostics()) {
    if (is_state_code(d.code)) {
      continue;
    }
    const char* level = d.level == support::DiagnosticLevel::Warning ? "warning" : "error";
    if (!d.file.empty()) {
      const int line = d.line > 0 ? d.line : 1;
      const int col = d.column > 0 ? d.column : 1;
      std::cerr << d.file << ":" << line << ":" << col << ": " << level << " " << d.code << ": " << d.message
                << "\n";
    } else {
      std::cerr << level << " " << d.code << ": " << d.message << "\n";
    }
    ++count;
  }
  return count;
}

}  // namespace

int handle_state(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  if (cmd.args.empty()) {
    print_usage();
    return 0;
  }
  const std::string mode = cmd.args[0];
  if (mode == "help" || mode == "--help") {
    print_usage();
    return 0;
  }
  if (mode != "explain" && mode != "doctor") {
    std::cerr << "ERROR: unknown state subcommand '" << mode << "'\n";
    print_usage();
    return 1;
  }
  if (cmd.args.size() < 2) {
    std::cerr << "ERROR: missing input file for state " << mode << "\n";
    print_usage();
    return 1;
  }

  const std::string input_path = cmd.args[1];
  bool as_json = false;
  std::string out_file;
  for (std::size_t i = 2; i < cmd.args.size(); ++i) {
    const std::string& arg = cmd.args[i];
    if (arg == "--json") {
      as_json = true;
      continue;
    }
    if (arg == "--out" && i + 1 < cmd.args.size()) {
      out_file = cmd.args[++i];
      continue;
    }
    if (starts_with(arg, "--out=")) {
      out_file = arg.substr(6);
      continue;
    }
  }

  std::filesystem::path temp_root;
  const bool build_ok = run_state_analysis(input_path, pipeline, diag, temp_root);
  const std::vector<StateFinding> findings = collect_state_findings(diag);

  if (mode == "explain") {
    if (as_json) {
      const std::string payload = findings_json(input_path, findings);
      if (!out_file.empty()) {
        support::write_text_file(out_file, payload);
        std::cout << "state explain: wrote " << out_file << "\n";
      } else {
        std::cout << payload;
      }
    } else {
      if (findings.empty()) {
        std::cout << "state explain: no typestate finding in " << input_path << "\n";
      } else {
        std::cout << "state explain: " << findings.size() << " finding(s)\n";
        for (const auto& f : findings) {
          const auto& d = f.diagnostic;
          const char* level = d.level == support::DiagnosticLevel::Warning ? "warning" : "error";
          const int line = d.line > 0 ? d.line : 1;
          const int col = d.column > 0 ? d.column : 1;
          std::cout << "- " << d.file << ":" << line << ":" << col << ": " << level << " " << d.code << ": "
                    << d.message << "\n";
          const std::string hint = support::diagnostic_fix_suggestion(d);
          if (!hint.empty()) {
            std::cout << "  help: " << hint << "\n";
          }
        }
      }
    }
    print_non_state_errors(diag);
    cleanup_temp(temp_root);
    return build_ok && !diag.has_errors() ? 0 : 1;
  }

  if (findings.empty()) {
    std::cout << "state doctor: no typestate issue detected\n";
  } else {
    std::map<std::string, int> counts;
    for (const auto& f : findings) {
      counts[f.diagnostic.code] += 1;
    }
    std::cout << "state doctor summary:\n";
    for (const auto& item : counts) {
      std::cout << "- " << item.first << ": " << item.second << "\n";
    }
    std::cout << "state doctor findings:\n";
    for (const auto& f : findings) {
      const auto& d = f.diagnostic;
      const int line = d.line > 0 ? d.line : 1;
      const int col = d.column > 0 ? d.column : 1;
      std::cout << "- " << d.file << ":" << line << ":" << col << ": " << d.code << ": " << d.message << "\n";
    }
    std::cout << "state doctor recommendations:\n";
    for (const auto& f : findings) {
      const std::string hint = support::diagnostic_fix_suggestion(f.diagnostic);
      if (!hint.empty()) {
        std::cout << "- " << f.diagnostic.code << ": " << hint << "\n";
      }
    }
  }
  print_non_state_errors(diag);
  cleanup_temp(temp_root);
  return build_ok && !diag.has_errors() ? 0 : 1;
}

}  // namespace thagc::driver
