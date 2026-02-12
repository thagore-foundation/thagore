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
using FnPyUnicodeFromStringAndSize = PyObject *(*)(const char *, Py_ssize_t);
using FnPyLongFromLong = PyObject *(*)(long);
using FnPyFloatFromDouble = PyObject *(*)(double);
using FnPyImportImport = PyObject *(*)(PyObject *);
using FnPyObjectGetAttrString = PyObject *(*)(PyObject *, const char *);
using FnPyObjectCallObject = PyObject *(*)(PyObject *, PyObject *);
using FnPyTuplePack = PyObject *(*)(Py_ssize_t, ...);
using FnPyObjectPrint = int (*)(PyObject *, FILE *, int);
using FnPyErrClear = void (*)();

struct PythonApi {
  HMODULE dll {nullptr};
  FnPyInitialize initialize {nullptr};
  FnPyFinalize finalize {nullptr};
  FnPyUnicodeFromString unicodeFromString {nullptr};
  FnPyUnicodeFromStringAndSize unicodeFromStringAndSize {nullptr};
  FnPyLongFromLong longFromLong {nullptr};
  FnPyFloatFromDouble floatFromDouble {nullptr};
  FnPyImportImport importModule {nullptr};
  FnPyObjectGetAttrString getAttrString {nullptr};
  FnPyObjectCallObject callObject {nullptr};
  FnPyTuplePack tuplePack {nullptr};
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
    api.unicodeFromStringAndSize =
      reinterpret_cast<FnPyUnicodeFromStringAndSize>(loadSymbol(api.dll, "PyUnicode_FromStringAndSize"));
    api.longFromLong = reinterpret_cast<FnPyLongFromLong>(loadSymbol(api.dll, "PyLong_FromLong"));
    api.floatFromDouble = reinterpret_cast<FnPyFloatFromDouble>(loadSymbol(api.dll, "PyFloat_FromDouble"));
    api.importModule = reinterpret_cast<FnPyImportImport>(loadSymbol(api.dll, "PyImport_Import"));
    api.getAttrString = reinterpret_cast<FnPyObjectGetAttrString>(loadSymbol(api.dll, "PyObject_GetAttrString"));
    api.callObject = reinterpret_cast<FnPyObjectCallObject>(loadSymbol(api.dll, "PyObject_CallObject"));
    api.tuplePack = reinterpret_cast<FnPyTuplePack>(loadSymbol(api.dll, "PyTuple_Pack"));
    api.objectPrint = reinterpret_cast<FnPyObjectPrint>(loadSymbol(api.dll, "PyObject_Print"));
    api.errClear = reinterpret_cast<FnPyErrClear>(loadSymbol(api.dll, "PyErr_Clear"));
    api.ready = api.initialize && api.finalize && api.unicodeFromString && api.unicodeFromStringAndSize &&
      api.longFromLong && api.floatFromDouble && api.importModule && api.getAttrString &&
      api.callObject && api.tuplePack && api.objectPrint && api.errClear;
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

void *__thg_py_call_0(void *func) {
  return __thg_py_call0(func);
}

void *__thg_py_from_i32(std::int32_t value) {
  if (!ensurePythonApi()) {
    return nullptr;
  }
  auto &api = pythonApi();
  PyObject *obj = api.longFromLong(static_cast<long>(value));
  if (obj == nullptr) {
    api.errClear();
  }
  return obj;
}

void *__thg_py_from_f32(float value) {
  if (!ensurePythonApi()) {
    return nullptr;
  }
  auto &api = pythonApi();
  PyObject *obj = api.floatFromDouble(static_cast<double>(value));
  if (obj == nullptr) {
    api.errClear();
  }
  return obj;
}

void *__thg_py_from_str(const char *ptr, std::int32_t len) {
  if (!ensurePythonApi()) {
    return nullptr;
  }
  auto &api = pythonApi();
  if (ptr == nullptr || len < 0) {
    return nullptr;
  }
  PyObject *obj = api.unicodeFromStringAndSize(ptr, static_cast<Py_ssize_t>(len));
  if (obj == nullptr) {
    api.errClear();
  }
  return obj;
}

void *__thg_py_call_1(void *func, void *arg1) {
  if (!ensurePythonApi()) {
    return nullptr;
  }
  auto &api = pythonApi();
  if (func == nullptr || arg1 == nullptr) {
    return nullptr;
  }
  PyObject *args = api.tuplePack(1, static_cast<PyObject *>(arg1));
  if (args == nullptr) {
    api.errClear();
    return nullptr;
  }
  PyObject *res = api.callObject(static_cast<PyObject *>(func), args);
  if (res == nullptr) {
    api.errClear();
  }
  return res;
}

void *__thg_py_call_2(void *func, void *arg1, void *arg2) {
  if (!ensurePythonApi()) {
    return nullptr;
  }
  auto &api = pythonApi();
  if (func == nullptr || arg1 == nullptr || arg2 == nullptr) {
    return nullptr;
  }
  PyObject *args = api.tuplePack(2, static_cast<PyObject *>(arg1), static_cast<PyObject *>(arg2));
  if (args == nullptr) {
    api.errClear();
    return nullptr;
  }
  PyObject *res = api.callObject(static_cast<PyObject *>(func), args);
  if (res == nullptr) {
    api.errClear();
  }
  return res;
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
