#include "thagc/driver/command_handlers.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "thagc/driver/common.hpp"
#include "thagc/driver/pipeline.hpp"
#include "thagc/shared/filesystem.hpp"
#include "thagc/shared/process.hpp"

namespace thagc::driver {

namespace {

static void print_diagnostics(const support::DiagnosticSink& diag) {
  for (const auto& d : diag.diagnostics()) {
    const char* level = d.level == support::DiagnosticLevel::Warning ? "warning" : "error";
    if (!d.file.empty()) {
      const int line = d.line > 0 ? d.line : 1;
      const int col = d.column > 0 ? d.column : 1;
      std::cerr << d.file << ":" << line << ":" << col << ": " << level << " " << d.code << ": " << d.message
                << "\n";
    } else {
      std::cerr << level << " " << d.code << ": " << d.message << "\n";
    }
    const std::string hint = support::diagnostic_fix_suggestion(d);
    if (!hint.empty()) {
      std::cerr << "  help: " << hint << "\n";
    }
  }
}

static std::string join_lines(const std::vector<std::string>& lines) {
  std::string out;
  for (const std::string& line : lines) {
    out += line;
    out.push_back('\n');
  }
  return out;
}

static std::string make_unique_suffix() {
  const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  return std::to_string(static_cast<long long>(now));
}

}  // namespace

int handle_repl(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  (void)cmd;
  std::cout << "Thagore REPL\n";
  std::cout << "Commands: :run, :show, :reset, :quit\n";

  std::vector<std::string> lines;
  std::string input;
  while (true) {
    std::cout << "thag> ";
    if (!std::getline(std::cin, input)) {
      std::cout << "\n";
      break;
    }
    if (input == ":quit" || input == ":exit") {
      break;
    }
    if (input == ":reset") {
      lines.clear();
      std::cout << "buffer cleared\n";
      continue;
    }
    if (input == ":show") {
      for (std::size_t i = 0; i < lines.size(); ++i) {
        std::cout << (i + 1) << ": " << lines[i] << "\n";
      }
      continue;
    }
    if (input == ":run") {
      if (lines.empty()) {
        std::cout << "buffer is empty\n";
        continue;
      }
      const std::filesystem::path temp_root =
          std::filesystem::temp_directory_path() / ("thagc-repl-" + make_unique_suffix());
      std::error_code ec;
      std::filesystem::create_directories(temp_root, ec);
      if (ec) {
        std::cerr << "ERROR: cannot create repl temp directory: " << ec.message() << "\n";
        return 1;
      }
      const std::filesystem::path src = temp_root / "main.tg";
#if defined(_WIN32)
      const std::filesystem::path bin = temp_root / "main.exe";
#else
      const std::filesystem::path bin = temp_root / "main.bin";
#endif
      support::write_text_file(src.string(), join_lines(lines));

      BuildOptions options;
      options.input_path = src.string();
      options.output_path = bin.string();
      options.include_paths = {};
      options.extra_link_args = {};
      options.emit_llvm = false;
      diag = support::DiagnosticSink{};
      if (!apply_target_config(options, "", diag)) {
        print_diagnostics(diag);
        std::filesystem::remove_all(temp_root, ec);
        continue;
      }
      if (!pipeline.build(options, diag)) {
        print_diagnostics(diag);
        std::filesystem::remove_all(temp_root, ec);
        continue;
      }
      const int rc = support::run_process({bin.string()});
      std::cout << "exit code: " << rc << "\n";
      std::filesystem::remove_all(temp_root, ec);
      continue;
    }
    lines.push_back(input);
  }
  return 0;
}

}  // namespace thagc::driver
