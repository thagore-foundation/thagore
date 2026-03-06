#include "thagc/driver/command_handlers.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "thagc/driver/common.hpp"
#include "thagc/driver/pipeline.hpp"
#include "thagc/shared/process.hpp"

namespace thagc::driver {

static std::string default_base_name(const std::string& input) {
  if (input.size() > 3 && input.compare(input.size() - 3, 3, ".tg") == 0) {
    return input.substr(0, input.size() - 3);
  }
  return input;
}

static std::string default_output(const std::string& input) {
  return default_base_name(input) + ".bin";
}

static bool valid_opt_level(int opt_level) {
  return opt_level >= 0 && opt_level <= 3;
}

static bool contains_ci(std::string haystack, std::string needle) {
  std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return haystack.find(needle) != std::string::npos;
}

static bool is_test_file(const std::filesystem::path& path) {
  if (!path.has_extension() || path.extension().string() != ".tg") {
    return false;
  }
  const std::string name = path.filename().string();
  return contains_ci(name, "test");
}

static std::vector<std::string> discover_tests(const ParsedCommand& cmd, const std::string& filter) {
  std::vector<std::string> tests;
  const std::filesystem::path root = std::filesystem::current_path();
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
    if (ec) {
      break;
    }
    if (!it->is_regular_file()) {
      continue;
    }
    const std::filesystem::path p = it->path();
    if (!is_test_file(p)) {
      continue;
    }
    const std::string full = p.string();
    if (!filter.empty() && !contains_ci(full, filter)) {
      continue;
    }
    if (!cmd.exclude_pattern.empty() && contains_ci(full, cmd.exclude_pattern)) {
      continue;
    }
    if (!cmd.package_name.empty() && !contains_ci(full, cmd.package_name)) {
      continue;
    }
    if (!cmd.exclude_package.empty() && contains_ci(full, cmd.exclude_package)) {
      continue;
    }
    tests.push_back(full);
  }
  std::sort(tests.begin(), tests.end());
  return tests;
}

struct TestResult {
  std::string path;
  bool build_ok = false;
  bool run_ok = false;
  int run_exit_code = 0;
  std::string message;
};

static void print_test_results_human(const std::vector<TestResult>& results, bool no_run) {
  for (const auto& r : results) {
    std::cout << (r.build_ok ? "[build-ok] " : "[build-fail] ") << r.path;
    if (!no_run) {
      std::cout << " " << (r.run_ok ? "[run-ok]" : "[run-fail:" + std::to_string(r.run_exit_code) + "]");
    }
    if (!r.message.empty()) {
      std::cout << " " << r.message;
    }
    std::cout << "\n";
  }
}

static void print_test_results_json(const std::vector<TestResult>& results, bool no_run) {
  std::cout << "{\n  \"tests\": [\n";
  for (std::size_t i = 0; i < results.size(); ++i) {
    const auto& r = results[i];
    std::cout << "    {\"path\":\"" << r.path << "\",\"build_ok\":" << (r.build_ok ? "true" : "false")
              << ",\"run_ok\":" << (no_run ? "null" : (r.run_ok ? "true" : "false"))
              << ",\"run_exit_code\":" << (no_run ? "null" : std::to_string(r.run_exit_code)) << "}";
    if (i + 1 < results.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }
  std::cout << "  ]\n}\n";
}

int handle_run(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  if (cmd.input_path.empty()) {
    std::cerr << "ERROR: missing input path for run\n";
    return 1;
  }
  if (!valid_opt_level(cmd.opt_level)) {
    std::cerr << "ERROR: invalid --opt-level value (expected 0..3)\n";
    return 1;
  }

  BuildOptions options;
  options.input_path = cmd.input_path;
  options.output_path = cmd.output_path.empty() ? default_output(cmd.input_path) : cmd.output_path;
  options.target_triple = cmd.target_triple;
  options.extra_link_args = compose_link_extra_args(cmd);
  options.opt_level = cmd.opt_level;
  options.emit_llvm = cmd.emit_llvm;
  if (!apply_target_config(options, cmd.target_triple, diag)) {
    for (const auto& d : diag.diagnostics()) {
      std::cerr << d.code << ": " << d.message << "\n";
    }
    return 1;
  }
  if (options.emit_llvm) {
    options.llvm_ir_path = cmd.output_path.empty() ? (default_base_name(cmd.input_path) + ".ll")
                                                    : (options.output_path + ".ll");
  }

  if (!pipeline.build(options, diag)) {
    for (const auto& d : diag.diagnostics()) {
      std::cerr << d.code << ": " << d.message << "\n";
    }
    return 1;
  }
  if (cmd.emit_llvm) {
    std::cout << "run: emitted LLVM IR only, skip execution\n";
    return 0;
  }

  const std::string exe = (std::filesystem::path(options.output_path).is_relative())
                              ? ("./" + options.output_path)
                              : options.output_path;
  const int rc = support::run_process({exe});
  if (rc != 0) {
    std::cerr << "ERROR: run failed with exit code " << rc << "\n";
    return 1;
  }
  return 0;
}

int handle_test(const ParsedCommand& cmd, const CompilerPipeline& pipeline, support::DiagnosticSink& diag) {
  if (!valid_opt_level(cmd.opt_level)) {
    std::cerr << "ERROR: invalid --opt-level value (expected 0..3)\n";
    return 1;
  }
  std::vector<std::string> test_paths;
  std::string filter;
  if (!cmd.input_path.empty()) {
    test_paths.push_back(cmd.input_path);
  } else if (cmd.workspace) {
    if (!cmd.args.empty() && !cmd.args[0].empty() && cmd.args[0][0] != '-') {
      filter = cmd.args[0];
    }
    test_paths = discover_tests(cmd, filter);
  } else if (!cmd.args.empty() && !cmd.args[0].empty() && cmd.args[0][0] != '-') {
    filter = cmd.args[0];
    test_paths = discover_tests(cmd, filter);
  }

  if (test_paths.empty()) {
    std::cerr << "ERROR: no tests found\n";
    return 1;
  }
  if (cmd.list_only) {
    for (const auto& path : test_paths) {
      std::cout << path << "\n";
    }
    return 0;
  }

  std::vector<TestResult> results;
  bool all_ok = true;
  for (std::size_t idx = 0; idx < test_paths.size(); ++idx) {
    const std::string& test_path = test_paths[idx];
    TestResult r;
    r.path = test_path;
    BuildOptions options;
    options.input_path = test_path;
    if (cmd.output_path.empty()) {
      options.output_path = default_output(test_path);
    } else if (test_paths.size() == 1) {
      options.output_path = cmd.output_path;
    } else {
      options.output_path = cmd.output_path + "." + std::to_string(idx);
    }
    options.target_triple = cmd.target_triple;
    options.extra_link_args = compose_link_extra_args(cmd);
    options.opt_level = cmd.opt_level;
    options.emit_llvm = cmd.emit_llvm;
    support::DiagnosticSink test_diag;
    if (!apply_target_config(options, cmd.target_triple, test_diag)) {
      r.build_ok = false;
      r.message = "target config failed";
      all_ok = false;
      results.push_back(std::move(r));
      if (cmd.fail_fast) {
        break;
      }
      continue;
    }
    if (options.emit_llvm) {
      options.llvm_ir_path = cmd.output_path.empty() ? (default_base_name(test_path) + ".ll")
                                                      : (options.output_path + ".ll");
    }

    if (!pipeline.build(options, test_diag)) {
      r.build_ok = false;
      r.message = "build failed";
      all_ok = false;
      results.push_back(std::move(r));
      if (cmd.fail_fast) {
        break;
      }
      continue;
    }
    r.build_ok = true;

    if (cmd.no_run || cmd.emit_llvm) {
      r.run_ok = true;
      r.run_exit_code = 0;
      if (cmd.emit_llvm) {
        r.message = "emit-llvm only";
      }
      results.push_back(std::move(r));
      continue;
    }

    const std::string test_exe = (std::filesystem::path(options.output_path).is_relative())
                                    ? ("./" + options.output_path)
                                    : options.output_path;
    const int rc = support::run_process({test_exe});
    r.run_exit_code = rc;
    r.run_ok = (rc == 0);
    if (!r.run_ok) {
      all_ok = false;
      r.message = "run failed";
    }
    results.push_back(std::move(r));
    if (!all_ok && cmd.fail_fast) {
      break;
    }
  }

  if (cmd.json_output || cmd.message_format == "json") {
    print_test_results_json(results, cmd.no_run || cmd.emit_llvm);
  } else {
    print_test_results_human(results, cmd.no_run || cmd.emit_llvm);
  }
  return all_ok ? 0 : 1;
}

}  // namespace thagc::driver
