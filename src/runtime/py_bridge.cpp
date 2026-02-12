#include <Python.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <windows.h>

namespace {

using FnPyInitialize = void (*)();
using FnPyFinalize = void (*)();
using FnPyUnicodeFromString = PyObject *(*)(const char *);
using FnPyImportImport = PyObject *(*)(PyObject *);
using FnPyObjectGetAttrString = PyObject *(*)(PyObject *, const char *);
using FnPyObjectCallObject = PyObject *(*)(PyObject *, PyObject *);
using FnPyObjectPrint = int (*)(PyObject *, FILE *, int);
using FnPyErrClear = void (*)();

struct PythonApi {
  HMODULE dll {nullptr};
  FnPyInitialize initialize {nullptr};
  FnPyFinalize finalize {nullptr};
  FnPyUnicodeFromString unicodeFromString {nullptr};
  FnPyImportImport importModule {nullptr};
  FnPyObjectGetAttrString getAttrString {nullptr};
  FnPyObjectCallObject callObject {nullptr};
  FnPyObjectPrint objectPrint {nullptr};
  FnPyErrClear errClear {nullptr};
  bool ready {false};
};

auto pythonApi() -> PythonApi & {
  static PythonApi api {};
  return api;
}

auto loadSymbol(HMODULE dll, const char *name) -> FARPROC {
  return GetProcAddress(dll, name);
}

auto ensurePythonApi() -> bool {
  static std::once_flag once {};
  std::call_once(once, []() {
    auto &api = pythonApi();
#ifdef THAG_PYTHON_DLL_PATH
    api.dll = LoadLibraryA(THAG_PYTHON_DLL_PATH);
#endif
    std::string dllName = std::format("python{}.dll", PY_MAJOR_VERSION * 100 + PY_MINOR_VERSION);
    if (api.dll == nullptr) {
      api.dll = LoadLibraryA(dllName.c_str());
    }
    if (api.dll == nullptr) {
      api.dll = LoadLibraryA("python3.dll");
    }
    if (api.dll == nullptr) {
      return;
    }

    api.initialize = reinterpret_cast<FnPyInitialize>(loadSymbol(api.dll, "Py_Initialize"));
    api.finalize = reinterpret_cast<FnPyFinalize>(loadSymbol(api.dll, "Py_Finalize"));
    api.unicodeFromString = reinterpret_cast<FnPyUnicodeFromString>(loadSymbol(api.dll, "PyUnicode_FromString"));
    api.importModule = reinterpret_cast<FnPyImportImport>(loadSymbol(api.dll, "PyImport_Import"));
    api.getAttrString = reinterpret_cast<FnPyObjectGetAttrString>(loadSymbol(api.dll, "PyObject_GetAttrString"));
    api.callObject = reinterpret_cast<FnPyObjectCallObject>(loadSymbol(api.dll, "PyObject_CallObject"));
    api.objectPrint = reinterpret_cast<FnPyObjectPrint>(loadSymbol(api.dll, "PyObject_Print"));
    api.errClear = reinterpret_cast<FnPyErrClear>(loadSymbol(api.dll, "PyErr_Clear"));
    api.ready = api.initialize && api.finalize && api.unicodeFromString && api.importModule && api.getAttrString &&
      api.callObject && api.objectPrint && api.errClear;
  });
  return pythonApi().ready;
}

} // namespace

extern "C" {

void __thg_py_initialize() {
  if (!ensurePythonApi()) {
    return;
  }
  pythonApi().initialize();
}

void __thg_py_finalize() {
  if (!ensurePythonApi()) {
    return;
  }
  pythonApi().finalize();
}

void *__thg_py_import(const char *name) {
  if (!ensurePythonApi()) {
    return nullptr;
  }
  auto &api = pythonApi();
  if (name == nullptr) {
    return nullptr;
  }
  PyObject *str = api.unicodeFromString(name);
  if (str == nullptr) {
    api.errClear();
    return nullptr;
  }
  PyObject *module = api.importModule(str);
  if (module == nullptr) {
    api.errClear();
  }
  return module;
}

void *__thg_py_getattr(void *obj, const char *attrName) {
  if (!ensurePythonApi()) {
    return nullptr;
  }
  auto &api = pythonApi();
  if (obj == nullptr || attrName == nullptr) {
    return nullptr;
  }
  PyObject *value = api.getAttrString(static_cast<PyObject *>(obj), attrName);
  if (value == nullptr) {
    api.errClear();
  }
  return value;
}

void *__thg_py_call0(void *func) {
  if (!ensurePythonApi()) {
    return nullptr;
  }
  auto &api = pythonApi();
  if (func == nullptr) {
    return nullptr;
  }
  PyObject *result = api.callObject(static_cast<PyObject *>(func), nullptr);
  if (result == nullptr) {
    api.errClear();
  }
  return result;
}

void __thg_py_print_obj(void *obj) {
  if (!ensurePythonApi()) {
    std::printf("None/Null\n");
    return;
  }
  auto &api = pythonApi();
  if (obj == nullptr) {
    std::printf("None/Null\n");
    return;
  }
  const int rc = api.objectPrint(static_cast<PyObject *>(obj), stdout, 0);
  if (rc != 0) {
    api.errClear();
    std::printf("None/Null\n");
    return;
  }
  std::printf("\n");
}

}
