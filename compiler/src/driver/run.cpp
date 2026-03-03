#include "thagc/driver/command_handlers.hpp"

#include <filesystem>
#include <iostream>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cctype>

#include "thagc/driver/common.hpp"
#include "thagc/driver/pipeline.hpp"
#include "thagc/shared/process.hpp"

namespace thagc::driver {

namespace {

static std::string default_base_name(const std::string& input) {
  if (input.size() > 3 && input.compare(input.size() - 3, 3, ".tg") == 0) {
    return input.substr(0, input.size() - 3);
  }
  return input;
}

static std::string default_output(const std::string& input, const std::string& target_triple) {
  if (is_wasm_target(target_triple)) {
    return default_base_name(input) + ".wasm";
  }
#if defined(_WIN32)
  return default_base_name(input) + ".exe";
#else
  return default_base_name(input) + ".bin";
#endif
}

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

struct WatchConfig {
  bool enabled = false;
  int interval_ms = 350;
  int max_iterations = 0;
};

static bool parse_positive_int(const std::string& text, int& out) {
  if (text.empty()) {
    return false;
  }
  for (char ch : text) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  out = std::stoi(text);
  return out > 0;
}

static bool starts_with(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

static bool parse_watch_config(const ParsedCommand& cmd, WatchConfig& out, std::string& error) {
  out = WatchConfig{};
  for (std::size_t i = 0; i < cmd.args.size(); ++i) {
    const std::string& arg = cmd.args[i];
    if (arg == "--watch") {
      out.enabled = true;
      continue;
    }
    if (arg == "--watch-interval-ms" && i + 1 < cmd.args.size()) {
      int value = 0;
      if (!parse_positive_int(cmd.args[i + 1], value)) {
        error = "--watch-interval-ms expects positive integer milliseconds";
        return false;
      }
      out.interval_ms = value;
      ++i;
      continue;
    }
    if (arg == "--watch-iterations" && i + 1 < cmd.args.size()) {
      int value = 0;
      if (!parse_positive_int(cmd.args[i + 1], value)) {
        error = "--watch-iterations expects positive integer";
        return false;
      }
      out.max_iterations = value;
      ++i;
      continue;
    }
    if (starts_with(arg, "--watch-interval-ms=")) {
      int value = 0;
      if (!parse_positive_int(arg.substr(std::string("--watch-interval-ms=").size()), value)) {
        error = "--watch-interval-ms expects positive integer milliseconds";
        return false;
      }
      out.interval_ms = value;
      continue;
    }
    if (starts_with(arg, "--watch-iterations=")) {
      int value = 0;
      if (!parse_positive_int(arg.substr(std::string("--watch-iterations=").size()), value)) {
        error = "--watch-iterations expects positive integer";
        return false;
      }
      out.max_iterations = value;
      continue;
    }
  }
  return true;
}

static std::vector<std::filesystem::path> collect_watch_files(const std::string& input_path) {
  std::vector<std::filesystem::path> files;
  std::error_code ec;
  std::filesystem::path input = std::filesystem::absolute(input_path, ec);
  if (ec || input.empty()) {
    files.push_back(input_path);
    return files;
  }
  files.push_back(input);
  const std::filesystem::path root = input.parent_path();
  if (root.empty()) {
    return files;
  }
  for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    if (ec || !it->is_regular_file()) {
      continue;
    }
    if (it->path().extension() == ".tg") {
      files.push_back(it->path());
    }
  }
  return files;
}

static std::unordered_map<std::string, std::uint64_t> snapshot_watch_files(const std::vector<std::filesystem::path>& files) {
  std::unordered_map<std::string, std::uint64_t> snapshot;
  for (const auto& file : files) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec) || ec) {
      snapshot[file.string()] = 0;
      continue;
    }
    const auto ftime = std::filesystem::last_write_time(file, ec);
    if (ec) {
      snapshot[file.string()] = 0;
      continue;
    }
    snapshot[file.string()] = static_cast<std::uint64_t>(ftime.time_since_epoch().count());
  }
  return snapshot;
}

static bool wait_for_source_change(const std::vector<std::filesystem::path>& files, int interval_ms) {
  auto previous = snapshot_watch_files(files);
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    const auto current = snapshot_watch_files(files);
    if (current != previous) {
      return true;
    }
  }
}

static int build_and_run_once(const ParsedCommand& cmd, const CompilerPipeline& pipeline, BuildOptions& options) {
  support::DiagnosticSink diag;
  if (!pipeline.build(options, diag)) {
    print_diagnostics(diag);
    return 1;
  }
  if (is_wasm_target(options.target_triple)) {
    std::cout << "run: wasm target built at " << options.output_path << " (execution skipped)\n";
    return 0;
  }
  if (cmd.emit_llvm) {
    std::cout << "run: emitted LLVM IR only, skip execution\n";
    return 0;
  }

#if defined(_WIN32)
  const std::string exe = options.output_path;
#else
  const std::string exe = (std::filesystem::path(options.output_path).is_relative())
                              ? ("./" + options.output_path)
                              : options.output_path;
#endif
  const int rc = support::run_process({exe});
  if (rc != 0) {
    std::cerr << "ERROR: run failed with exit code " << rc << "\n";
    return 1;
  }
  return 0;
}

}  // namespace

int handle_run(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  if (cmd.input_path.empty()) {
    std::cerr << "ERROR: missing input path for run\n";
    return 1;
  }

  BuildOptions options;
  options.input_path = cmd.input_path;
  options.output_path = cmd.output_path.empty() ? default_output(cmd.input_path, cmd.target_triple) : cmd.output_path;
  options.target_triple = cmd.target_triple;
  options.include_paths = cmd.include_paths;
  options.extra_link_args = compose_link_extra_args(cmd);
  options.emit_llvm = cmd.emit_llvm;
  if (!apply_target_config(options, cmd.target_triple, diag)) {
    print_diagnostics(diag);
    return 1;
  }
  if (options.emit_llvm) {
    options.llvm_ir_path = cmd.output_path.empty() ? (default_base_name(cmd.input_path) + ".ll")
                                                    : (options.output_path + ".ll");
  }

  WatchConfig watch;
  std::string watch_error;
  if (!parse_watch_config(cmd, watch, watch_error)) {
    std::cerr << "ERROR: " << watch_error << "\n";
    return 1;
  }

  if (!watch.enabled) {
    return build_and_run_once(cmd, pipeline, options);
  }

  const std::vector<std::filesystem::path> watch_files = collect_watch_files(cmd.input_path);
  if (watch_files.empty()) {
    std::cerr << "ERROR: watch mode could not resolve source files\n";
    return 1;
  }

  std::cout << "watch: hot reload enabled (" << watch_files.size() << " files, interval " << watch.interval_ms << "ms)\n";
  int iteration = 0;
  while (true) {
    ++iteration;
    const int once_rc = build_and_run_once(cmd, pipeline, options);
    if (once_rc != 0) {
      return once_rc;
    }
    if (watch.max_iterations > 0 && iteration >= watch.max_iterations) {
      return 0;
    }
    std::cout << "watch: waiting for source changes...\n";
    if (!wait_for_source_change(watch_files, watch.interval_ms)) {
      return 0;
    }
    std::cout << "watch: source changed, rebuilding\n";
  }
}

}  // namespace thagc::driver
