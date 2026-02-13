#include "thagore/backend/ir_generator.hpp"
#include "thagore/driver/driver.hpp"
#include "thagore/frontend/lexer.hpp"
#include "thagore/frontend/parser.hpp"
#include "thagore/frontend/semantic.hpp"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace thagore;

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                  \
    if (!(expr)) {                                                                                                      \
      std::cerr << "CHECK failed: " << #expr << " at " << __FILE__ << ":" << __LINE__ << "\n";                       \
      std::abort();                                                                                                     \
    }                                                                                                                   \
  } while (false)

extern "C" {
const char *__env_get(const char *key);
int __env_set(const char *key, const char *value);
const char *__env_args();
const char *__env_cwd();

const char *__fs_read_text(const char *path);
int __fs_write_text(const char *path, const char *text);
int __fs_exists(const char *path);
int __fs_mkdir(const char *path);
const char *__fs_list_dir(const char *path);
void *__fs_open_binary(const char *path, const char *mode);
int __fs_write_bytes(void *handle, const char *buffer);
const char *__fs_read_bytes(void *handle, int size);
int __fs_seek(void *handle, int offset, int whence);
int __fs_close(void *handle);

int __time_now_ms();
int __time_sleep(int ms);

int __string_codepoint(const char *ch);
const char *__string_from_codepoint(int cp);
int __thg_str_contains(const char *text, const char *needle);
int __thg_str_starts_with(const char *text, const char *prefix);
int __thg_str_ends_with(const char *text, const char *suffix);
const char *__thg_str_trim(const char *text);
const char *__thg_str_replace(const char *text, const char *old_value, const char *new_value);
const char *__thg_str_lower(const char *text);
const char *__thg_str_upper(const char *text);
int __thg_str_compare(const char *left, const char *right);
const char *__thg_path_strip_trailing(const char *path);
const char *__thg_path_strip_leading(const char *path);
const char *__thg_path_basename(const char *path);
const char *__thg_path_dirname(const char *path);
const char *__thg_path_ext(const char *path);
const char *__thg_path_join2(const char *left, const char *right);
const char *__thg_fmt_trim_trailing(const char *text);

void __thg_str_free(char *s);
}

namespace {

#ifndef THAG_SOURCE_ROOT
#define THAG_SOURCE_ROOT "."
#endif

auto parseModuleSource(std::string_view source, std::string_view file) -> std::unique_ptr<ModuleDecl> {
  Lexer lexer {};
  auto tokens = lexer.tokenize(source, std::string(file));
  CHECK(tokens.has_value());

  Parser parser {};
  auto module = parser.parseModule(*tokens);
  CHECK(module.has_value());
  return std::move(module.value());
}

auto analyzeSource(std::string_view source, std::string_view file) -> TypedModule {
  auto module = parseModuleSource(source, file);
  SemanticAnalyzer semantic {};
  auto typed = semantic.analyze(std::move(module));
  CHECK(typed.has_value());
  return std::move(typed.value());
}

auto makeTempPath(std::string_view prefix) -> std::filesystem::path {
  const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()
  ).count();
  return std::filesystem::temp_directory_path() / (std::string(prefix) + "_" + std::to_string(ticks));
}

void testLexerKeywordsAndComments() {
  constexpr std::string_view source = R"(// comment
use prelude as prelude
func f(a, b = 1) = not a and b or true
)";
  Lexer lexer {};
  auto tokens = lexer.tokenize(source, "keywords.thg");
  CHECK(tokens.has_value());

  bool hasUse = false;
  bool hasNot = false;
  bool hasAnd = false;
  bool hasOr = false;
  for (const auto &tok : *tokens) {
    hasUse = hasUse || tok.kind == TokenKind::KwUse;
    hasNot = hasNot || tok.kind == TokenKind::KwNot;
    hasAnd = hasAnd || tok.kind == TokenKind::KwAnd;
    hasOr = hasOr || tok.kind == TokenKind::KwOr;
  }
  CHECK(hasUse);
  CHECK(hasNot);
  CHECK(hasAnd);
  CHECK(hasOr);
}

void testParserStdCoreSyntax() {
  constexpr std::string_view source = R"(use prelude as prelude
func f(a, b = 1) = a + b
func main(cond, a, b):
  if cond:
    throw "x"
  while cond:
    return f(1)
  let z = not a and b or true
  let n = null
  return 0
)";

  auto module = parseModuleSource(source, "parser_std_core.thg");
  CHECK(module->imports.size() == 1);
  CHECK(module->imports[0].path == "prelude");
  CHECK(module->imports[0].alias == "prelude");
  CHECK(module->functions.size() == 2);

  const auto &exprFn = *module->functions[0];
  CHECK(exprFn.params.size() == 2);
  CHECK(exprFn.returnType && exprFn.returnType->base == BaseType::Unknown);
  CHECK(exprFn.body != nullptr);
  CHECK(exprFn.body->statements.size() == 1);
  CHECK(exprFn.body->statements[0]->kind == NodeKind::ReturnStmt);

  const auto &mainFn = *module->functions[1];
  CHECK(mainFn.body != nullptr);
  CHECK(mainFn.body->statements.size() == 5);
  CHECK(mainFn.body->statements[0]->kind == NodeKind::IfStmt);
  CHECK(mainFn.body->statements[1]->kind == NodeKind::LoopStmt);

  const auto &ifStmt = static_cast<const IfStmt &>(*mainFn.body->statements[0]);
  CHECK(!ifStmt.thenBlock->statements.empty());
  CHECK(ifStmt.thenBlock->statements[0]->kind == NodeKind::ExprStmt);
  const auto &throwStmt = static_cast<const ExprStmt &>(*ifStmt.thenBlock->statements[0]);
  CHECK(throwStmt.expr->kind == NodeKind::CallExpr);
  const auto &throwCall = static_cast<const CallExpr &>(*throwStmt.expr);
  CHECK(throwCall.callee == "__thg_throw");

  const auto &letBool = static_cast<const LetStmt &>(*mainFn.body->statements[2]);
  CHECK(letBool.init->kind == NodeKind::BinaryExpr);
  const auto &boolExpr = static_cast<const BinaryExpr &>(*letBool.init);
  CHECK(boolExpr.op == BinaryOp::Or);

  const auto &letNull = static_cast<const LetStmt &>(*mainFn.body->statements[3]);
  CHECK(letNull.init->kind == NodeKind::LiteralExpr);
  const auto &nullLit = static_cast<const LiteralExpr &>(*letNull.init);
  CHECK(nullLit.literalKind == LiteralExpr::Kind::Null);
}

void testSemanticAndCodegenStdCorePaths() {
  constexpr std::string_view source = R"(func infer(flag: bool):
  if flag:
    return 7
  return 9

func gate(cond):
  if cond:
    return 1
  while cond:
    return 2
  return 0

func sum(a: i32, b: i32) -> i32:
  return a + b

func main() -> i32:
  let x = infer(true)
  let n = null
  if n == null and not false:
    return sum(x)
  __thg_throw("boom")
  return 0
)";

  auto typed = analyzeSource(source, "semantic_std_core.thg");

  const auto inferIt = typed.functionTypes.find("infer");
  CHECK(inferIt != typed.functionTypes.end());
  CHECK(inferIt->second.returnType != nullptr);
  CHECK(inferIt->second.returnType->base == BaseType::I32);

  const auto sumIt = typed.functionTypes.find("sum");
  CHECK(sumIt != typed.functionTypes.end());
  CHECK(sumIt->second.params.size() == 2);

  const auto gateIt = typed.functionTypes.find("gate");
  CHECK(gateIt != typed.functionTypes.end());
  CHECK(gateIt->second.returnType != nullptr);
  CHECK(gateIt->second.returnType->base == BaseType::I32);

  llvm::LLVMContext context;
  IRGenerator generator {context};
  auto lowered = generator.lower(typed, "semantic_std_core.thg");
  CHECK(lowered.has_value());
}

void testIfReturnKeepsOuterScope() {
  constexpr std::string_view source = R"(func process_exit(code: i32) -> void:
  if true:
    return
  print(code)
  return

func main() -> i32:
  process_exit(1)
  return 0
)";

  auto typed = analyzeSource(source, "if_return_scope.thg");
  llvm::LLVMContext context;
  IRGenerator generator {context};
  auto lowered = generator.lower(typed, "if_return_scope.thg");
  CHECK(lowered.has_value());
}

void testStdCoreModuleGraphWithDriver() {
  const auto sourceRoot = std::filesystem::path(THAG_SOURCE_ROOT);
  const auto stdEntry = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "lib.tg");
  CHECK(std::filesystem::exists(stdEntry));

  const auto tempRoot = makeTempPath("thagore_std_core_graph");
  std::filesystem::create_directories(tempRoot);

  const auto entry = tempRoot / "entry.tg";
  const auto outIr = tempRoot / "entry.ll";

  {
    std::ofstream out(entry, std::ios::binary | std::ios::trunc);
    CHECK(out.good());
    out << "use \"" << stdEntry.generic_string() << "\" as std\n";
    out << "func main() -> i32:\n";
    out << "  return 0\n";
  }

  Driver driver {};
  const int rc = driver.run({"thag", entry.string(), "--emit-ir", "-o", outIr.string()});
  CHECK(rc == 0);
  CHECK(std::filesystem::exists(outIr));

  std::filesystem::remove_all(tempRoot);
}

void testLibSystemModulesWithDriver() {
  const auto sourceRoot = std::filesystem::path(THAG_SOURCE_ROOT);
  const auto sysPlatformEntry =
    std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "sys_platform.tg");
  const auto ioEntry = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "io.tg");
  CHECK(std::filesystem::exists(sysPlatformEntry));
  CHECK(std::filesystem::exists(ioEntry));

  const auto tempRoot = makeTempPath("thagore_libsystem_graph");
  std::filesystem::create_directories(tempRoot);

  const auto entry = tempRoot / "entry.tg";
  const auto outIr = tempRoot / "entry.ll";

  {
    std::ofstream out(entry, std::ios::binary | std::ios::trunc);
    CHECK(out.good());
    out << "use \"" << sysPlatformEntry.generic_string() << "\" as sys_platform\n";
    out << "use \"" << ioEntry.generic_string() << "\" as io\n";
    out << "func main() -> i32:\n";
    out << "  let exists = sys_platform.file_exists(\"" << entry.generic_string() << "\")\n";
    out << "  if exists == 1:\n";
    out << "    let text = io.read_text(\"" << entry.generic_string() << "\")\n";
    out << "    if text == null:\n";
    out << "      return 2\n";
    out << "  let now = sys_platform.time_now_ms()\n";
    out << "  if now < 0:\n";
    out << "    return 3\n";
    out << "  return 0\n";
  }

  Driver driver {};
  const int rc = driver.run({"thag", entry.string(), "--emit-ir", "-o", outIr.string()});
  CHECK(rc == 0);
  CHECK(std::filesystem::exists(outIr));

  std::filesystem::remove_all(tempRoot);
}

void testRthagoreModuleSurfaceWithDriver() {
  const auto sourceRoot = std::filesystem::path(THAG_SOURCE_ROOT);
  const auto rthagoreRoot = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore");
  CHECK(std::filesystem::exists(rthagoreRoot));

  std::vector<std::filesystem::path> modules {};
  for (const auto &entry : std::filesystem::recursive_directory_iterator(rthagoreRoot)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    if (entry.path().extension() == ".tg") {
      modules.push_back(std::filesystem::weakly_canonical(entry.path()));
    }
  }
  CHECK(!modules.empty());

  const auto tempRoot = makeTempPath("thagore_rthagore_surface");
  std::filesystem::create_directories(tempRoot);
  const auto entry = tempRoot / "entry.tg";

  Driver driver {};
  for (std::size_t i = 0; i < modules.size(); ++i) {
    const auto outIr = tempRoot / ("mod_" + std::to_string(i) + ".ll");
    {
      std::ofstream out(entry, std::ios::binary | std::ios::trunc);
      CHECK(out.good());
      out << "use \"" << modules[i].generic_string() << "\" as m\n";
      out << "func main() -> i32:\n";
      out << "  return 0\n";
    }

    const int rc = driver.run({"thag", entry.string(), "--emit-ir", "-o", outIr.string()});
    if (rc != 0) {
      std::cerr << "Failed module: " << modules[i].string() << "\n";
    }
    CHECK(rc == 0);
    CHECK(std::filesystem::exists(outIr));
  }

  std::filesystem::remove_all(tempRoot);
}

void testDragoAndToolsCompatWithDriver() {
  const auto sourceRoot = std::filesystem::path(THAG_SOURCE_ROOT);
  const auto dragoCache = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "drago" / "cache.tg");
  const auto dragoBuild = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "drago" / "build.tg");
  const auto dragoResolve = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "drago" / "resolve.tg");
  const auto dragoRunner = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "drago" / "testrunner.tg");
  const auto formatter = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "tools" / "formatter.tg");
  const auto linter = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "tools" / "linter.tg");
  const auto lsp = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "tools" / "lsp.tg");
  CHECK(std::filesystem::exists(dragoCache));
  CHECK(std::filesystem::exists(dragoBuild));
  CHECK(std::filesystem::exists(dragoResolve));
  CHECK(std::filesystem::exists(dragoRunner));
  CHECK(std::filesystem::exists(formatter));
  CHECK(std::filesystem::exists(linter));
  CHECK(std::filesystem::exists(lsp));

  const auto tempRoot = makeTempPath("thagore_drago_tools_compat");
  std::filesystem::create_directories(tempRoot);
  const auto entry = tempRoot / "entry.tg";
  const auto outIr = tempRoot / "entry.ll";

  {
    std::ofstream out(entry, std::ios::binary | std::ios::trunc);
    CHECK(out.good());
    out << "use \"" << dragoCache.generic_string() << "\" as cache\n";
    out << "use \"" << dragoBuild.generic_string() << "\" as build\n";
    out << "use \"" << dragoResolve.generic_string() << "\" as resolve\n";
    out << "use \"" << dragoRunner.generic_string() << "\" as testrunner\n";
    out << "use \"" << formatter.generic_string() << "\" as formatter\n";
    out << "use \"" << linter.generic_string() << "\" as linter\n";
    out << "use \"" << lsp.generic_string() << "\" as lsp\n";
    out << "func main() -> i32:\n";
    out << "  let dir = cache.cache_dir(\".cache\", \"pkg\", \"1\")\n";
    out << "  let file = cache.cache_put_text(\".cache\", \"pkg\", \"1\", \"a.tgc\", \"x=1\")\n";
    out << "  let txt = cache.cache_get_text(\".cache\", \"pkg\", \"1\", \"a.tgc\")\n";
    out << "  let dep = resolve.resolve(\"a=1\", \"\")\n";
    out << "  let plan = build.plan_build(\"project\")\n";
    out << "  let rep = testrunner.report(\"ok\")\n";
    out << "  let fmt = formatter.format(\"x  \\n\")\n";
    out << "  let warn = linter.style_checks(\"x\\t\")\n";
    out << "  let diag = lsp.diagnostics(\"x\\t\")\n";
    out << "  if dep != \"\":\n";
    out << "    return 0\n";
    out << "  return 0\n";
  }

  Driver driver {};
  const int rc = driver.run({"thag", entry.string(), "--emit-ir", "-o", outIr.string()});
  CHECK(rc == 0);
  CHECK(std::filesystem::exists(outIr));

  std::filesystem::remove_all(tempRoot);
}

void testStdNonCoreCompatWithDriver() {
  const auto sourceRoot = std::filesystem::path(THAG_SOURCE_ROOT);
  const auto bytesMod = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "bytes.tg");
  const auto bufferMod = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "buffer.tg");
  const auto listMod = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "list.tg");
  const auto mapMod = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "map.tg");
  const auto setMod = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "set.tg");
  const auto collectionsMod =
    std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "collections.tg");
  const auto jsonMod = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "json.tg");
  const auto regexMod = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "regex.tg");
  const auto memMod = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "mem.tg");
  const auto mathMod = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "math.tg");
  const auto pathMod = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "path.tg");
  const auto timeMod = std::filesystem::weakly_canonical(sourceRoot / "lib" / "rthagore" / "std" / "time.tg");
  CHECK(std::filesystem::exists(bytesMod));
  CHECK(std::filesystem::exists(bufferMod));
  CHECK(std::filesystem::exists(listMod));
  CHECK(std::filesystem::exists(mapMod));
  CHECK(std::filesystem::exists(setMod));
  CHECK(std::filesystem::exists(collectionsMod));
  CHECK(std::filesystem::exists(jsonMod));
  CHECK(std::filesystem::exists(regexMod));
  CHECK(std::filesystem::exists(memMod));
  CHECK(std::filesystem::exists(mathMod));
  CHECK(std::filesystem::exists(pathMod));
  CHECK(std::filesystem::exists(timeMod));

  const auto tempRoot = makeTempPath("thagore_std_noncore_compat");
  std::filesystem::create_directories(tempRoot);
  const auto entry = tempRoot / "entry.tg";
  const auto outIr = tempRoot / "entry.ll";

  {
    std::ofstream out(entry, std::ios::binary | std::ios::trunc);
    CHECK(out.good());
    out << "use \"" << bytesMod.generic_string() << "\" as bytes\n";
    out << "use \"" << bufferMod.generic_string() << "\" as buffer\n";
    out << "use \"" << listMod.generic_string() << "\" as list\n";
    out << "use \"" << mapMod.generic_string() << "\" as map\n";
    out << "use \"" << setMod.generic_string() << "\" as set\n";
    out << "use \"" << collectionsMod.generic_string() << "\" as collections\n";
    out << "use \"" << jsonMod.generic_string() << "\" as json\n";
    out << "use \"" << regexMod.generic_string() << "\" as regex\n";
    out << "use \"" << memMod.generic_string() << "\" as mem\n";
    out << "use \"" << mathMod.generic_string() << "\" as math\n";
    out << "use \"" << pathMod.generic_string() << "\" as path\n";
    out << "use \"" << timeMod.generic_string() << "\" as time\n";
    out << "func main() -> i32:\n";
    out << "  let b = bytes.new(4)\n";
    out << "  let bl = bytes.len(b)\n";
    out << "  let bx = bytes.to_hex(b)\n";
    out << "  let s0 = buffer.new()\n";
    out << "  let s1 = buffer.push(s0, \"x\")\n";
    out << "  let s2 = list.map(s1, \"\")\n";
    out << "  let hm = collections.hashmap_new()\n";
    out << "  let hv = collections.hashmap_get(hm, \"k\", \"\")\n";
    out << "  let mk = map.keys(hm)\n";
    out << "  let su = set.union(\"a\", \"b\")\n";
    out << "  let js = json.stringify(hv)\n";
    out << "  let ok = regex.is_match(\"a\", \"a\")\n";
    out << "  let m0 = mem.alloc(8)\n";
    out << "  let p0 = path.join(\"a\", \"b\")\n";
    out << "  let t0 = time.set_backend(\"builtin\")\n";
    out << "  let n0 = math.pow(2, 3)\n";
    out << "  if ok and n0 > 0 and bl >= 0:\n";
    out << "    return 0\n";
    out << "  return 0\n";
  }

  Driver driver {};
  const int rc = driver.run({"thag", entry.string(), "--emit-ir", "-o", outIr.string()});
  CHECK(rc == 0);
  CHECK(std::filesystem::exists(outIr));

  std::filesystem::remove_all(tempRoot);
}

void testRuntimeBridgeContracts() {
  const auto tempRoot = makeTempPath("thagore_std_core_runtime");
  std::filesystem::create_directories(tempRoot);

  const auto textPath = tempRoot / "sample.txt";
  const auto binPath = tempRoot / "sample.bin";
  const auto nestedDir = tempRoot / "nested";

  const auto textFile = textPath.string();
  const auto binFile = binPath.string();
  const auto dirPath = tempRoot.string();

  CHECK(__fs_write_text(textFile.c_str(), "hello std core") == 1);
  const char *text = __fs_read_text(textFile.c_str());
  CHECK(text != nullptr);
  CHECK(std::string(text) == "hello std core");
  __thg_str_free(const_cast<char *>(text));
  __thg_str_free(const_cast<char *>(text));

  CHECK(__fs_exists(textFile.c_str()) == 1);
  CHECK(__fs_mkdir(nestedDir.string().c_str()) == 1);
  CHECK(__fs_exists(nestedDir.string().c_str()) == 1);
  const char *dirListing = __fs_list_dir(dirPath.c_str());
  CHECK(dirListing != nullptr);
  CHECK(std::string(dirListing).find("sample.txt") != std::string::npos);
  __thg_str_free(const_cast<char *>(dirListing));

  void *writer = __fs_open_binary(binFile.c_str(), "wb");
  CHECK(writer != nullptr);
  CHECK(__fs_write_bytes(writer, "abc123") == 6);
  CHECK(__fs_close(writer) == 0);

  void *reader = __fs_open_binary(binFile.c_str(), "rb");
  CHECK(reader != nullptr);
  CHECK(__fs_seek(reader, 1, 0) == 0);
  const char *chunk = __fs_read_bytes(reader, 3);
  CHECK(chunk != nullptr);
  CHECK(std::string(chunk) == "bc1");
  __thg_str_free(const_cast<char *>(chunk));
  CHECK(__fs_close(reader) == 0);

  CHECK(__env_set("THAGORE_STD_CORE_TEST", "ok") == 1);
  const char *envValue = __env_get("THAGORE_STD_CORE_TEST");
  CHECK(envValue != nullptr);
  CHECK(std::string(envValue) == "ok");
  __thg_str_free(const_cast<char *>(envValue));

  const char *cwd = __env_cwd();
  CHECK(cwd != nullptr);
  CHECK(std::strlen(cwd) > 0);
  __thg_str_free(const_cast<char *>(cwd));

  const char *args = __env_args();
  CHECK(args != nullptr);
  __thg_str_free(const_cast<char *>(args));

  CHECK(__env_get(nullptr) == nullptr);
  CHECK(__fs_read_text(nullptr) == nullptr);
  CHECK(__fs_write_text(nullptr, "x") == 0);
  CHECK(__fs_mkdir(nullptr) == 0);
  CHECK(__fs_exists(nullptr) == 0);

  const char *emptyRead = __fs_read_bytes(nullptr, 8);
  CHECK(emptyRead != nullptr);
  CHECK(std::string(emptyRead).empty());
  __thg_str_free(const_cast<char *>(emptyRead));

  const int before = __time_now_ms();
  CHECK(__time_sleep(1) == 0);
  const int after = __time_now_ms();
  CHECK(after >= before);

  CHECK(__string_codepoint("A") == 65);
  CHECK(__string_codepoint(nullptr) == 0);
  const char *cpA = __string_from_codepoint(65);
  CHECK(cpA != nullptr);
  CHECK(std::string(cpA) == "A");
  __thg_str_free(const_cast<char *>(cpA));
  const char *cpFallback = __string_from_codepoint(999);
  CHECK(cpFallback != nullptr);
  CHECK(std::string(cpFallback) == "?");
  __thg_str_free(const_cast<char *>(cpFallback));

  CHECK(__thg_str_contains("hello world", "world") == 1);
  CHECK(__thg_str_starts_with("hello", "he") == 1);
  CHECK(__thg_str_ends_with("hello", "lo") == 1);
  CHECK(__thg_str_compare("abc", "abd") < 0);
  CHECK(__thg_str_compare("abc", "abc") == 0);
  CHECK(__thg_str_compare("abd", "abc") > 0);

  const char *trimmed = __thg_str_trim(" \t hello \n");
  CHECK(trimmed != nullptr);
  CHECK(std::string(trimmed) == "hello");
  __thg_str_free(const_cast<char *>(trimmed));

  const char *replaced = __thg_str_replace("a-b-c", "-", ":");
  CHECK(replaced != nullptr);
  CHECK(std::string(replaced) == "a:b:c");
  __thg_str_free(const_cast<char *>(replaced));

  const char *lowered = __thg_str_lower("HeLLo");
  CHECK(lowered != nullptr);
  CHECK(std::string(lowered) == "hello");
  __thg_str_free(const_cast<char *>(lowered));

  const char *uppered = __thg_str_upper("HeLLo");
  CHECK(uppered != nullptr);
  CHECK(std::string(uppered) == "HELLO");
  __thg_str_free(const_cast<char *>(uppered));

  const char *pStripTrailing = __thg_path_strip_trailing("a/b///");
  CHECK(pStripTrailing != nullptr);
  CHECK(std::string(pStripTrailing) == "a/b");
  __thg_str_free(const_cast<char *>(pStripTrailing));

  const char *pStripLeading = __thg_path_strip_leading("///a/b");
  CHECK(pStripLeading != nullptr);
  CHECK(std::string(pStripLeading) == "a/b");
  __thg_str_free(const_cast<char *>(pStripLeading));

  const char *pBase = __thg_path_basename("a/b/c.txt");
  CHECK(pBase != nullptr);
  CHECK(std::string(pBase) == "c.txt");
  __thg_str_free(const_cast<char *>(pBase));

  const char *pDir = __thg_path_dirname("a/b/c.txt");
  CHECK(pDir != nullptr);
  CHECK(std::string(pDir) == "a/b");
  __thg_str_free(const_cast<char *>(pDir));

  const char *pExt = __thg_path_ext("a/b/c.txt");
  CHECK(pExt != nullptr);
  CHECK(std::string(pExt) == "txt");
  __thg_str_free(const_cast<char *>(pExt));

  const char *pJoin = __thg_path_join2("a/b", "c.txt");
  CHECK(pJoin != nullptr);
  CHECK(std::string(pJoin) == "a/b/c.txt");
  __thg_str_free(const_cast<char *>(pJoin));

  const char *fmt = __thg_fmt_trim_trailing("a  \n b\t\n");
  CHECK(fmt != nullptr);
  CHECK(std::string(fmt) == "a\n b\n");
  __thg_str_free(const_cast<char *>(fmt));

  for (int i = 0; i < 1024; ++i) {
    const char *loopEnv = __env_get("THAGORE_STD_CORE_TEST");
    CHECK(loopEnv != nullptr);
    __thg_str_free(const_cast<char *>(loopEnv));

    const char *loopText = __fs_read_text(textFile.c_str());
    CHECK(loopText != nullptr);
    __thg_str_free(const_cast<char *>(loopText));

    (void)__time_now_ms();
  }

  std::filesystem::remove_all(tempRoot);
}

} // namespace

int main() {
  testLexerKeywordsAndComments();
  testParserStdCoreSyntax();
  testSemanticAndCodegenStdCorePaths();
  testIfReturnKeepsOuterScope();
  testStdCoreModuleGraphWithDriver();
  testLibSystemModulesWithDriver();
  testRthagoreModuleSurfaceWithDriver();
  testDragoAndToolsCompatWithDriver();
  testStdNonCoreCompatWithDriver();
  testRuntimeBridgeContracts();

  std::cout << "All tests passed\n";
  return 0;
}

