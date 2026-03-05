#include "thagc/driver/command_handlers.hpp"

#include <filesystem>
#include <iostream>
#include <cstdint>
#include <exception>
#include <string>
#include <unordered_map>

#include "thagc/driver/common.hpp"
#include "thagc/frontend/source_map.hpp"
#include "thagc/driver/pipeline.hpp"
#include "thagc/shared/filesystem.hpp"

namespace thagc::driver {

namespace {

static std::string default_base_name(const std::string& input) {
  if (input.size() > 3 && input.compare(input.size() - 3, 3, ".tg") == 0) {
    return input.substr(0, input.size() - 3);
  }
  return input;
}

struct DiagnosticRenderContext {
  syntax::SourceMap source_map;
  std::unordered_map<std::string, std::uint32_t> file_ids;
};

static bool ensure_file_loaded(const std::string& file, DiagnosticRenderContext& ctx, std::uint32_t& out_file_id) {
  auto it = ctx.file_ids.find(file);
  if (it != ctx.file_ids.end()) {
    out_file_id = it->second;
    return true;
  }
  try {
    const std::string source = support::read_text_file(file);
    out_file_id = ctx.source_map.add_file(file, source);
    ctx.file_ids[file] = out_file_id;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

static void print_caret_line(const support::Diagnostic& d, DiagnosticRenderContext& ctx) {
  if (d.file.empty() || d.line <= 0) {
    return;
  }
  std::uint32_t file_id = 0;
  if (!ensure_file_loaded(d.file, ctx, file_id)) {
    return;
  }
  const std::string_view line_text = ctx.source_map.line_text(file_id, static_cast<std::uint32_t>(d.line));
  if (line_text.empty()) {
    return;
  }
  int start_col = d.column > 0 ? d.column : 1;
  int end_col = d.end_column > 0 ? d.end_column : start_col + 1;
  if (end_col <= start_col) {
    end_col = start_col + 1;
  }

  const int line_len = static_cast<int>(line_text.size());
  if (start_col > line_len + 1) {
    start_col = line_len + 1;
  }
  if (end_col > line_len + 1) {
    end_col = line_len + 1;
  }
  if (end_col <= start_col) {
    end_col = start_col + 1;
  }

  std::cerr << "  | " << line_text << "\n";
  std::cerr << "  | " << std::string(static_cast<std::size_t>(start_col - 1), ' ')
            << std::string(static_cast<std::size_t>(end_col - start_col), '^') << "\n";
}

static void print_diagnostics(const support::DiagnosticSink& diag) {
  DiagnosticRenderContext render_ctx;
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
    print_caret_line(d, render_ctx);
    const std::string hint = support::diagnostic_fix_suggestion(d);
    if (!hint.empty()) {
      std::cerr << "  help: " << hint << "\n";
    }
  }
}

}  // namespace

int handle_check(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  if (cmd.input_path.empty()) {
    std::cerr << "ERROR: missing input path for check\n";
    return 1;
  }

  BuildOptions options;
  options.input_path = cmd.input_path;
#if defined(_WIN32)
  options.output_path = cmd.output_path.empty() ? (default_base_name(cmd.input_path) + ".check.exe") : cmd.output_path;
#else
  options.output_path = cmd.output_path.empty() ? (default_base_name(cmd.input_path) + ".check.bin") : cmd.output_path;
#endif
  options.target_triple = cmd.target_triple;
  options.include_paths = cmd.include_paths;
  options.emit_llvm = true;
  options.llvm_ir_path = options.output_path + ".ll";
  if (!apply_target_config(options, cmd.target_triple, diag)) {
    print_diagnostics(diag);
    return 1;
  }
  if (!pipeline.build(options, diag)) {
    print_diagnostics(diag);
    return 1;
  }

  std::error_code ec;
  std::filesystem::remove(options.llvm_ir_path, ec);
  std::cout << "check: OK (" << cmd.input_path << ")\n";
  return 0;
}

}  // namespace thagc::driver
