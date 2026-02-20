#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path


HELP_INSERT = '    std::printf("  thagore update <check|apply|rollback>\\n");\n'
HELP_ANCHORS = (
    '    std::printf("  thagore install toolchain',
    '    std::printf("  thagore intent lock <entry.tg>',
    '    std::printf("  thagore --version\\n");',
)
UPDATE_MARK = 'if (arg1 == "update") {'
EMIT_ANCHOR = '  if (arg1 == "--emit-llvm-internal") {'
RUNTIME_FN_START = "static auto cliDetectRuntimeLib() -> std::string {"
RUNTIME_NEXT_ANCHOR = "static auto cliReadText(const std::string &path) -> std::string {"
BRIDGE_INSERT_ANCHOR = "\nnamespace {\n"
BRIDGE_MARK_BEGIN = "// THG_LLVM_CAPI_BRIDGE_BEGIN"
BRIDGE_MARK_END = "// THG_LLVM_CAPI_BRIDGE_END"

UPDATE_BLOCK = r'''
  if (arg1 == "update") {

    std::string mode {"check"};

    if (argc >= 3) {

      mode = cstrOrEmpty(__thg_arg_get(2));

    }

    if (mode != "check" && mode != "apply" && mode != "rollback") {

      std::fprintf(stderr, "Unknown update mode '%s' (expected: check|apply|rollback).\n", mode.c_str());

      return 2;

    }

#if defined(_WIN32)

    const auto selfPath = resolveSelfExecutablePath();

    std::filesystem::path scriptPath {};

    if (!selfPath.empty()) {

      scriptPath = selfPath.parent_path() / "installer" / "update-windows.ps1";

      if (!std::filesystem::exists(scriptPath)) {

        scriptPath = selfPath.parent_path().parent_path() / "installer" / "update-windows.ps1";

      }

    }

    if (scriptPath.empty() || !std::filesystem::exists(scriptPath)) {

      scriptPath = std::filesystem::path("installer") / "update-windows.ps1";

    }

    if (!std::filesystem::exists(scriptPath)) {

      scriptPath = std::filesystem::path("scripts") / "install" / "update-windows.ps1";

    }

    const std::string cmdArg = std::string("powershell -NoProfile -ExecutionPolicy Bypass -File \"")
      + scriptPath.string()
      + "\" "
      + mode;
    return std::system(cmdArg.c_str());

#else

    std::fprintf(stderr, "Error: update is only supported on Windows.\n");

    return 2;

#endif

  }

'''

RUNTIME_BLOCK = r'''
static auto cliDetectRuntimeLib() -> std::string {
  const char *envRuntime = std::getenv("THAGORE_RUNTIME_LIB");
  if (envRuntime == nullptr || envRuntime[0] == '\0') {
    envRuntime = std::getenv("THAG_RUNTIME_LIB");
  }
  if (envRuntime != nullptr && envRuntime[0] != '\0') {
    const std::filesystem::path configuredPath {envRuntime};
    std::error_code configuredEc {};
    if (std::filesystem::exists(configuredPath, configuredEc) && !configuredEc) {
      return configuredPath.string();
    }
  }

  std::vector<std::filesystem::path> roots {};
  const auto selfPath = resolveSelfExecutablePath();
  if (!selfPath.empty()) {
    const auto selfDir = selfPath.parent_path();
    if (!selfDir.empty()) {
      roots.push_back(selfDir);
      const auto installRoot = selfDir.parent_path();
      if (!installRoot.empty()) {
        roots.push_back(installRoot);
      }
    }
  }
  std::error_code cwdEc {};
  const auto cwd = std::filesystem::current_path(cwdEc);
  if (!cwdEc && !cwd.empty()) {
    roots.push_back(cwd);
  }

  const std::vector<std::filesystem::path> relCandidates {
    "runtime.lib",
    "runtime.a",
    std::filesystem::path("lib") / "runtime.lib",
    std::filesystem::path("lib") / "runtime.a",
    std::filesystem::path("runtime") / "runtime.lib",
    std::filesystem::path("runtime") / "runtime.a",
    std::filesystem::path("runtime") / "build" / "runtime.lib",
    std::filesystem::path("runtime") / "build" / "Release" / "runtime.lib",
    std::filesystem::path("runtime") / "build" / "runtime.a",
    std::filesystem::path("build") / "runtime.lib",
    std::filesystem::path("build") / "runtime.a",
    "thag_runtime.lib",
    "libthag_runtime.a",
    std::filesystem::path("lib") / "thag_runtime.lib",
    std::filesystem::path("lib") / "libthag_runtime.a",
    std::filesystem::path("runtime") / "thag_runtime.lib",
    std::filesystem::path("runtime") / "libthag_runtime.a",
    std::filesystem::path("runtime") / "build" / "thag_runtime.lib",
    std::filesystem::path("runtime") / "build" / "Release" / "thag_runtime.lib",
    std::filesystem::path("runtime") / "build" / "libthag_runtime.a",
    std::filesystem::path("build") / "thag_runtime.lib",
    std::filesystem::path("build") / "libthag_runtime.a",
  };

  for (const auto &root : roots) {
    for (const auto &rel : relCandidates) {
      const auto candidate = root.empty() ? rel : (root / rel);
      std::error_code ec {};
      if (std::filesystem::exists(candidate, ec) && !ec) {
        return candidate.string();
      }
    }
  }

  return "";
}

'''

LLVM_BRIDGE_BLOCK = r'''
#if !defined(_WIN32)
#include <dlfcn.h>
#endif

// THG_LLVM_CAPI_BRIDGE_BEGIN
namespace thg_llvm_bridge {
#if defined(_WIN32)
using dynlib_t = HMODULE;
#else
using dynlib_t = void *;
#endif

auto open_library(const char *name) -> dynlib_t {
#if defined(_WIN32)
  return LoadLibraryA(name);
#else
  return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

auto resolve_symbol(dynlib_t handle, const char *symbol) -> void * {
  if (handle == nullptr) {
    return nullptr;
  }
#if defined(_WIN32)
  return reinterpret_cast<void *>(GetProcAddress(handle, symbol));
#else
  return dlsym(handle, symbol);
#endif
}

auto detect_llvm_library() -> dynlib_t {
  static dynlib_t handle = nullptr;
  static std::once_flag once {};
  std::call_once(once, []() {
#if defined(_WIN32)
    const std::array<const char *, 6> candidates {
      "LLVM-C.dll",
      "libLLVM.dll",
      "LLVM.dll",
      "LLVM-18.dll",
      "LLVM-17.dll",
      "LLVM-16.dll",
    };
#else
    const std::array<const char *, 8> candidates {
      "libLLVM.so",
      "libLLVM-18.so",
      "libLLVM-17.so",
      "libLLVM-16.so",
      "libLLVM.dylib",
      "/opt/homebrew/opt/llvm/lib/libLLVM.dylib",
      "/usr/local/opt/llvm/lib/libLLVM.dylib",
      "/usr/lib/libLLVM.so",
    };
#endif
    for (const char *name : candidates) {
      auto loaded = open_library(name);
      if (loaded != nullptr) {
        handle = loaded;
        break;
      }
    }
  });
  return handle;
}

template <typename Fn>
auto resolve(const char *symbol) -> Fn {
  auto *raw = resolve_symbol(detect_llvm_library(), symbol);
#if !defined(_WIN32)
  if (raw == nullptr) {
    raw = dlsym(RTLD_DEFAULT, symbol);
  }
#endif
  return reinterpret_cast<Fn>(raw);
}
} // namespace thg_llvm_bridge

#define THG_LLVM_BRIDGE_PTR(name, params, args) \
extern "C" void *name params { \
  using fn_t = void * (*) params; \
  static fn_t fn = thg_llvm_bridge::resolve<fn_t>(#name); \
  if (fn == nullptr) { \
    return nullptr; \
  } \
  return fn args; \
}

#define THG_LLVM_BRIDGE_I32(name, params, args) \
extern "C" int name params { \
  using fn_t = int (*) params; \
  static fn_t fn = thg_llvm_bridge::resolve<fn_t>(#name); \
  if (fn == nullptr) { \
    return 1; \
  } \
  return fn args; \
}

#define THG_LLVM_BRIDGE_VOID(name, params, args) \
extern "C" void name params { \
  using fn_t = void (*) params; \
  static fn_t fn = thg_llvm_bridge::resolve<fn_t>(#name); \
  if (fn == nullptr) { \
    return; \
  } \
  fn args; \
}

THG_LLVM_BRIDGE_PTR(LLVMContextCreate, (), ())
THG_LLVM_BRIDGE_VOID(LLVMContextDispose, (void *ctx), (ctx))

THG_LLVM_BRIDGE_PTR(LLVMModuleCreateWithNameInContext, (const char *name, void *ctx), (name, ctx))
THG_LLVM_BRIDGE_VOID(LLVMDisposeModule, (void *module_ref), (module_ref))
THG_LLVM_BRIDGE_VOID(LLVMSetTarget, (void *module_ref, const char *triple), (module_ref, triple))
THG_LLVM_BRIDGE_VOID(LLVMSetSourceFileName, (void *module_ref, const char *name, int length), (module_ref, name, length))

THG_LLVM_BRIDGE_PTR(LLVMInt1TypeInContext, (void *ctx), (ctx))
THG_LLVM_BRIDGE_PTR(LLVMInt8TypeInContext, (void *ctx), (ctx))
THG_LLVM_BRIDGE_PTR(LLVMInt16TypeInContext, (void *ctx), (ctx))
THG_LLVM_BRIDGE_PTR(LLVMInt32TypeInContext, (void *ctx), (ctx))
THG_LLVM_BRIDGE_PTR(LLVMInt64TypeInContext, (void *ctx), (ctx))
THG_LLVM_BRIDGE_PTR(LLVMVoidTypeInContext, (void *ctx), (ctx))
THG_LLVM_BRIDGE_PTR(LLVMFloatTypeInContext, (void *ctx), (ctx))
THG_LLVM_BRIDGE_PTR(LLVMDoubleTypeInContext, (void *ctx), (ctx))
THG_LLVM_BRIDGE_PTR(LLVMPointerType, (void *ty, int address_space), (ty, address_space))
THG_LLVM_BRIDGE_PTR(LLVMArrayType2, (void *elem_ty, int count), (elem_ty, count))
THG_LLVM_BRIDGE_PTR(LLVMStructTypeInContext, (void *ctx, void *elem_types, int elem_count, int packed), (ctx, elem_types, elem_count, packed))
THG_LLVM_BRIDGE_PTR(LLVMFunctionType, (void *ret_ty, void *param_types, int param_count, int is_var_arg), (ret_ty, param_types, param_count, is_var_arg))

THG_LLVM_BRIDGE_PTR(LLVMAddFunction, (void *module_ref, const char *name, void *fn_ty), (module_ref, name, fn_ty))
THG_LLVM_BRIDGE_PTR(LLVMGetNamedFunction, (void *module_ref, const char *name), (module_ref, name))
THG_LLVM_BRIDGE_PTR(LLVMGetParam, (void *fn_value, int index), (fn_value, index))
THG_LLVM_BRIDGE_I32(LLVMCountParams, (void *fn_value), (fn_value))
THG_LLVM_BRIDGE_VOID(LLVMGetParamTypes, (void *fn_ty, void *dest), (fn_ty, dest))
THG_LLVM_BRIDGE_PTR(LLVMAppendBasicBlockInContext, (void *ctx, void *fn_value, const char *name), (ctx, fn_value, name))
THG_LLVM_BRIDGE_PTR(LLVMCreateBuilderInContext, (void *ctx), (ctx))
THG_LLVM_BRIDGE_VOID(LLVMDisposeBuilder, (void *builder), (builder))
THG_LLVM_BRIDGE_VOID(LLVMPositionBuilderAtEnd, (void *builder, void *block), (builder, block))
THG_LLVM_BRIDGE_PTR(LLVMGetInsertBlock, (void *builder), (builder))
THG_LLVM_BRIDGE_PTR(LLVMBuildRet, (void *builder, void *value), (builder, value))
THG_LLVM_BRIDGE_PTR(LLVMBuildRetVoid, (void *builder), (builder))
THG_LLVM_BRIDGE_PTR(LLVMBuildAlloca, (void *builder, void *ty, const char *name), (builder, ty, name))
THG_LLVM_BRIDGE_PTR(LLVMBuildStore, (void *builder, void *value, void *ptr_value), (builder, value, ptr_value))
THG_LLVM_BRIDGE_PTR(LLVMBuildLoad2, (void *builder, void *ty, void *ptr_value, const char *name), (builder, ty, ptr_value, name))
THG_LLVM_BRIDGE_PTR(LLVMBuildAdd, (void *builder, void *lhs, void *rhs, const char *name), (builder, lhs, rhs, name))
THG_LLVM_BRIDGE_PTR(LLVMBuildSub, (void *builder, void *lhs, void *rhs, const char *name), (builder, lhs, rhs, name))
THG_LLVM_BRIDGE_PTR(LLVMBuildMul, (void *builder, void *lhs, void *rhs, const char *name), (builder, lhs, rhs, name))
THG_LLVM_BRIDGE_PTR(LLVMBuildSDiv, (void *builder, void *lhs, void *rhs, const char *name), (builder, lhs, rhs, name))
THG_LLVM_BRIDGE_PTR(LLVMBuildICmp, (void *builder, int pred, void *lhs, void *rhs, const char *name), (builder, pred, lhs, rhs, name))
THG_LLVM_BRIDGE_PTR(LLVMBuildAnd, (void *builder, void *lhs, void *rhs, const char *name), (builder, lhs, rhs, name))
THG_LLVM_BRIDGE_PTR(LLVMBuildOr, (void *builder, void *lhs, void *rhs, const char *name), (builder, lhs, rhs, name))
THG_LLVM_BRIDGE_PTR(LLVMBuildXor, (void *builder, void *lhs, void *rhs, const char *name), (builder, lhs, rhs, name))
THG_LLVM_BRIDGE_PTR(LLVMBuildBr, (void *builder, void *dest), (builder, dest))
THG_LLVM_BRIDGE_PTR(LLVMBuildCondBr, (void *builder, void *cond, void *then_block, void *else_block), (builder, cond, then_block, else_block))
THG_LLVM_BRIDGE_PTR(LLVMBuildCall2, (void *builder, void *fn_ty, void *fn_value, void *args_data, int arg_count, const char *name), (builder, fn_ty, fn_value, args_data, arg_count, name))

THG_LLVM_BRIDGE_PTR(LLVMConstInt, (void *ty, int value, int sign_extend), (ty, value, sign_extend))
THG_LLVM_BRIDGE_PTR(LLVMConstNull, (void *ty), (ty))
THG_LLVM_BRIDGE_PTR(LLVMConstStringInContext2, (void *ctx, const char *value, int length, int dont_null_terminate), (ctx, value, length, dont_null_terminate))

THG_LLVM_BRIDGE_I32(LLVMVerifyModule, (void *module_ref, int action, void *out_message), (module_ref, action, out_message))
THG_LLVM_BRIDGE_VOID(LLVMDisposeMessage, (void *message), (message))
THG_LLVM_BRIDGE_PTR(LLVMPrintModuleToString, (void *module_ref), (module_ref))

THG_LLVM_BRIDGE_I32(LLVMInitializeNativeTarget, (), ())
THG_LLVM_BRIDGE_I32(LLVMInitializeNativeAsmPrinter, (), ())
THG_LLVM_BRIDGE_I32(LLVMInitializeNativeAsmParser, (), ())
THG_LLVM_BRIDGE_PTR(LLVMGetDefaultTargetTriple, (), ())
THG_LLVM_BRIDGE_I32(LLVMGetTargetFromTriple, (const char *triple, void *target_out, void *err_out), (triple, target_out, err_out))
THG_LLVM_BRIDGE_PTR(LLVMCreateTargetMachine, (void *target, const char *triple, const char *cpu, const char *features, int opt_level, int reloc_mode, int code_model), (target, triple, cpu, features, opt_level, reloc_mode, code_model))
THG_LLVM_BRIDGE_VOID(LLVMDisposeTargetMachine, (void *machine), (machine))
THG_LLVM_BRIDGE_I32(LLVMTargetMachineEmitToFile, (void *machine, void *module_ref, const char *filename, int codegen, void *out_message), (machine, module_ref, filename, codegen, out_message))
THG_LLVM_BRIDGE_I32(LLVMTargetMachineEmitToMemoryBuffer, (void *machine, void *module_ref, int codegen, void *out_message, void *out_mem_buf), (machine, module_ref, codegen, out_message, out_mem_buf))

THG_LLVM_BRIDGE_PTR(LLVMCreatePassManager, (), ())
THG_LLVM_BRIDGE_VOID(LLVMDisposePassManager, (void *pm), (pm))
THG_LLVM_BRIDGE_I32(LLVMRunPassManager, (void *pm, void *module_ref), (pm, module_ref))
THG_LLVM_BRIDGE_VOID(LLVMAddInstructionCombiningPass, (void *pm), (pm))
THG_LLVM_BRIDGE_VOID(LLVMAddReassociatePass, (void *pm), (pm))
THG_LLVM_BRIDGE_VOID(LLVMAddGVNPass, (void *pm), (pm))
THG_LLVM_BRIDGE_VOID(LLVMAddCFGSimplificationPass, (void *pm), (pm))

THG_LLVM_BRIDGE_I32(LLVMLinkModules2, (void *dest, void *src), (dest, src))
THG_LLVM_BRIDGE_PTR(LLVMCreateMemoryBufferWithMemoryRangeCopy, (const char *data, int data_len, const char *buffer_name), (data, data_len, buffer_name))
THG_LLVM_BRIDGE_VOID(LLVMDisposeMemoryBuffer, (void *mem_buf), (mem_buf))

extern "C" int __thg_llvm_emit_object_from_module(void *module_ref, const char *object_path) {
  if (module_ref == nullptr || object_path == nullptr) {
    return 1;
  }

  if (LLVMInitializeNativeTarget() != 0 || LLVMInitializeNativeAsmPrinter() != 0 || LLVMInitializeNativeAsmParser() != 0) {
    return 2;
  }

  auto *triple_ptr = reinterpret_cast<const char *>(LLVMGetDefaultTargetTriple());
  if (triple_ptr == nullptr) {
    return 3;
  }
  LLVMSetTarget(module_ref, triple_ptr);

  void *target = nullptr;
  void *target_err = nullptr;
  if (LLVMGetTargetFromTriple(triple_ptr, &target, &target_err) != 0 || target == nullptr) {
    if (target_err != nullptr) {
      LLVMDisposeMessage(target_err);
    }
    LLVMDisposeMessage(reinterpret_cast<void *>(const_cast<char *>(triple_ptr)));
    return 4;
  }

  void *machine = LLVMCreateTargetMachine(target, triple_ptr, "", "", 2, 0, 0);
  if (machine == nullptr) {
    LLVMDisposeMessage(reinterpret_cast<void *>(const_cast<char *>(triple_ptr)));
    return 5;
  }

  void *emit_err = nullptr;
  const int emit_code = LLVMTargetMachineEmitToFile(machine, module_ref, object_path, 1, &emit_err);
  if (emit_err != nullptr) {
    LLVMDisposeMessage(emit_err);
  }
  LLVMDisposeTargetMachine(machine);
  LLVMDisposeMessage(reinterpret_cast<void *>(const_cast<char *>(triple_ptr)));
  return emit_code == 0 ? 0 : 6;
}

extern "C" void *__thg_llvm_builder_context_create() {
  return LLVMContextCreate();
}

extern "C" void __thg_llvm_builder_context_dispose(void *ctx) {
  LLVMContextDispose(ctx);
}

extern "C" void *__thg_llvm_builder_module_create(void *ctx, const char *name) {
  if (ctx == nullptr) {
    return nullptr;
  }
  const char *module_name = (name != nullptr && name[0] != '\0') ? name : "thagore_module";
  return LLVMModuleCreateWithNameInContext(module_name, ctx);
}

extern "C" void __thg_llvm_builder_module_dispose(void *module_ref) {
  LLVMDisposeModule(module_ref);
}

extern "C" void *__thg_llvm_builder_type_i32(void *ctx) {
  return LLVMInt32TypeInContext(ctx);
}

extern "C" void *__thg_llvm_builder_type_void(void *ctx) {
  return LLVMVoidTypeInContext(ctx);
}

extern "C" void *__thg_llvm_builder_function_type(void *ret_ty, void *param_types, int param_count, int is_var_arg) {
  return LLVMFunctionType(ret_ty, param_types, param_count, is_var_arg);
}

extern "C" void *__thg_llvm_builder_function_add(void *module_ref, const char *name, void *fn_ty) {
  if (module_ref == nullptr || fn_ty == nullptr) {
    return nullptr;
  }
  const char *fn_name = (name != nullptr && name[0] != '\0') ? name : "main";
  return LLVMAddFunction(module_ref, fn_name, fn_ty);
}

extern "C" void *__thg_llvm_builder_function_main_i32(void *module_ref, void *ctx, const char *name) {
  if (module_ref == nullptr || ctx == nullptr) {
    return nullptr;
  }
  void *i32_ty = LLVMInt32TypeInContext(ctx);
  void *fn_ty = LLVMFunctionType(i32_ty, nullptr, 0, 0);
  const char *fn_name = (name != nullptr && name[0] != '\0') ? name : "main";
  return LLVMAddFunction(module_ref, fn_name, fn_ty);
}

extern "C" void *__thg_llvm_builder_function_i32_one_param(void *module_ref, void *ctx, const char *name) {
  if (module_ref == nullptr || ctx == nullptr) {
    return nullptr;
  }
  void *i32_ty = LLVMInt32TypeInContext(ctx);
  void *params[1] {i32_ty};
  void *fn_ty = LLVMFunctionType(i32_ty, params, 1, 0);
  const char *fn_name = (name != nullptr && name[0] != '\0') ? name : "f";
  return LLVMAddFunction(module_ref, fn_name, fn_ty);
}

extern "C" void *__thg_llvm_builder_function_get_param(void *fn_value, int index) {
  return LLVMGetParam(fn_value, index);
}

extern "C" void *__thg_llvm_builder_block_append(void *ctx, void *fn_value, const char *name) {
  if (ctx == nullptr || fn_value == nullptr) {
    return nullptr;
  }
  const char *block_name = (name != nullptr && name[0] != '\0') ? name : "entry";
  return LLVMAppendBasicBlockInContext(ctx, fn_value, block_name);
}

extern "C" void *__thg_llvm_builder_create(void *ctx) {
  return LLVMCreateBuilderInContext(ctx);
}

extern "C" void __thg_llvm_builder_dispose(void *builder) {
  LLVMDisposeBuilder(builder);
}

extern "C" void __thg_llvm_builder_position_at_end(void *builder, void *block) {
  LLVMPositionBuilderAtEnd(builder, block);
}

extern "C" void *__thg_llvm_builder_const_i32(void *ctx, int value) {
  return LLVMConstInt(LLVMInt32TypeInContext(ctx), value, 1);
}

extern "C" void *__thg_llvm_builder_build_alloca(void *builder, void *ty, const char *name) {
  const char *inst_name = (name != nullptr && name[0] != '\0') ? name : "tmp";
  return LLVMBuildAlloca(builder, ty, inst_name);
}

extern "C" void *__thg_llvm_builder_build_store(void *builder, void *value, void *ptr_value) {
  return LLVMBuildStore(builder, value, ptr_value);
}

extern "C" void *__thg_llvm_builder_build_load(void *builder, void *ty, void *ptr_value, const char *name) {
  const char *inst_name = (name != nullptr && name[0] != '\0') ? name : "loadtmp";
  return LLVMBuildLoad2(builder, ty, ptr_value, inst_name);
}

extern "C" void *__thg_llvm_builder_build_add(void *builder, void *lhs, void *rhs, const char *name) {
  const char *inst_name = (name != nullptr && name[0] != '\0') ? name : "addtmp";
  return LLVMBuildAdd(builder, lhs, rhs, inst_name);
}

extern "C" void *__thg_llvm_builder_build_sub(void *builder, void *lhs, void *rhs, const char *name) {
  const char *inst_name = (name != nullptr && name[0] != '\0') ? name : "subtmp";
  return LLVMBuildSub(builder, lhs, rhs, inst_name);
}

extern "C" void *__thg_llvm_builder_build_mul(void *builder, void *lhs, void *rhs, const char *name) {
  const char *inst_name = (name != nullptr && name[0] != '\0') ? name : "multmp";
  return LLVMBuildMul(builder, lhs, rhs, inst_name);
}

extern "C" void *__thg_llvm_builder_build_div(void *builder, void *lhs, void *rhs, const char *name) {
  const char *inst_name = (name != nullptr && name[0] != '\0') ? name : "divtmp";
  return LLVMBuildSDiv(builder, lhs, rhs, inst_name);
}

extern "C" void *__thg_llvm_builder_build_icmp(void *builder, int pred, void *lhs, void *rhs, const char *name) {
  const char *inst_name = (name != nullptr && name[0] != '\0') ? name : "cmptmp";
  return LLVMBuildICmp(builder, pred, lhs, rhs, inst_name);
}

extern "C" void *__thg_llvm_builder_build_br(void *builder, void *dest) {
  return LLVMBuildBr(builder, dest);
}

extern "C" void *__thg_llvm_builder_build_cond_br(void *builder, void *cond, void *then_block, void *else_block) {
  return LLVMBuildCondBr(builder, cond, then_block, else_block);
}

extern "C" void *__thg_llvm_builder_build_call(void *builder, void *fn_ty, void *fn_value, void *args_data, int arg_count, const char *name) {
  const char *inst_name = (name != nullptr) ? name : "";
  return LLVMBuildCall2(builder, fn_ty, fn_value, args_data, arg_count, inst_name);
}

extern "C" void *__thg_llvm_builder_build_ret(void *builder, void *value) {
  return LLVMBuildRet(builder, value);
}

extern "C" void *__thg_llvm_builder_build_ret_void(void *builder) {
  return LLVMBuildRetVoid(builder);
}

extern "C" int __thg_llvm_builder_verify_module(void *module_ref, int action, void *out_message) {
  return LLVMVerifyModule(module_ref, action, out_message);
}

extern "C" int __thg_llvm_builder_emit_object(void *module_ref, const char *object_path) {
  return __thg_llvm_emit_object_from_module(module_ref, object_path);
}

extern "C" int __thg_llvm_emit_object_from_ir(const char *ir_text, const char *module_name, const char *object_path) {
  if (ir_text == nullptr || object_path == nullptr) {
    return 1;
  }

  using parse_ir_fn_t = int (*)(void *, void *, void *, void *);
  static parse_ir_fn_t parse_ir = thg_llvm_bridge::resolve<parse_ir_fn_t>("LLVMParseIRInContext");
  if (parse_ir == nullptr) {
    return 2;
  }

  void *ctx = LLVMContextCreate();
  if (ctx == nullptr) {
    return 3;
  }
  const char *buf_name = module_name != nullptr ? module_name : "thagore_module";
  void *mem_buf = LLVMCreateMemoryBufferWithMemoryRangeCopy(ir_text, static_cast<int>(std::strlen(ir_text)), buf_name);
  if (mem_buf == nullptr) {
    LLVMContextDispose(ctx);
    return 4;
  }

  void *module_ref = nullptr;
  void *parse_err = nullptr;
  if (parse_ir(ctx, mem_buf, &module_ref, &parse_err) != 0 || module_ref == nullptr) {
    if (parse_err != nullptr) {
      LLVMDisposeMessage(parse_err);
    }
    LLVMDisposeMemoryBuffer(mem_buf);
    LLVMContextDispose(ctx);
    return 5;
  }

  const int emit_code = __thg_llvm_emit_object_from_module(module_ref, object_path);
  LLVMDisposeModule(module_ref);
  LLVMDisposeMemoryBuffer(mem_buf);
  LLVMContextDispose(ctx);
  return emit_code == 0 ? 0 : 6;
}

#undef THG_LLVM_BRIDGE_PTR
#undef THG_LLVM_BRIDGE_I32
#undef THG_LLVM_BRIDGE_VOID
// THG_LLVM_CAPI_BRIDGE_END
'''


def patch_runtime(path: Path) -> int:
    text = path.read_text(encoding="utf-8")

    if BRIDGE_MARK_BEGIN in text and BRIDGE_MARK_END in text:
        start = text.find(BRIDGE_MARK_BEGIN)
        end = text.find(BRIDGE_MARK_END)
        if end < start:
            raise RuntimeError("invalid LLVM bridge marker order")
        end += len(BRIDGE_MARK_END)
        while end < len(text) and text[end] in "\r\n":
            end += 1
        text = text[:start] + text[end:]

    out_lines: list[str] = []
    lines = text.splitlines(keepends=True)
    for idx, line in enumerate(lines):
        out_lines.append(line)
        if any(anchor in line for anchor in HELP_ANCHORS):
            nxt = lines[idx + 1] if (idx + 1) < len(lines) else ""
            if HELP_INSERT not in nxt:
                out_lines.append(HELP_INSERT)
    text = "".join(out_lines)

    idx = text.find(EMIT_ANCHOR)
    if idx < 0:
        raise RuntimeError(f"missing anchor: {EMIT_ANCHOR}")
    existing = text.find(UPDATE_MARK)
    if existing >= 0 and existing < idx:
        text = text[:existing] + text[idx:]
        idx = text.find(EMIT_ANCHOR)
    text = text[:idx] + UPDATE_BLOCK + text[idx:]

    runtime_start = text.find(RUNTIME_FN_START)
    runtime_next = text.find(RUNTIME_NEXT_ANCHOR)
    if runtime_start < 0 or runtime_next < 0 or runtime_next <= runtime_start:
        raise RuntimeError(f"missing runtime detect anchors: {RUNTIME_FN_START} / {RUNTIME_NEXT_ANCHOR}")
    text = text[:runtime_start] + RUNTIME_BLOCK + text[runtime_next:]

    namespace_idx = text.find(BRIDGE_INSERT_ANCHOR)
    if namespace_idx < 0:
        raise RuntimeError(f"missing LLVM bridge insert anchor: {BRIDGE_INSERT_ANCHOR!r}")
    text = text[:namespace_idx] + "\n" + LLVM_BRIDGE_BLOCK + text[namespace_idx:]

    path.write_text(text, encoding="utf-8")
    return 0


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_runtime_update_cmd.py <runtime.cc>", file=sys.stderr)
        return 2
    target = Path(sys.argv[1])
    if not target.exists():
        print(f"error: file not found: {target}", file=sys.stderr)
        return 2
    return patch_runtime(target)


if __name__ == "__main__":
    raise SystemExit(main())
