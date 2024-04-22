#pragma once

// TODO(dkorolev): All these are next steps.
//
// The wrapper over native Current's `dlib` support. Extensions:
// 1) Handles startup and tear down, both stateful and stateless. Namely, will call, if present:
//    - `OnC5TLoad()`.
//    - `OnC5TUnload()`.
//    - ...
// 2) Renames and does the symlinks magic as the `.so` file changes on disk and is requested to be reloaded.
//    The issue is that by default the environment, at least on Linux, will not even attempt to re-load the same lib.
// 3) Offers the generic `IGeneric` and `IGeneric::Use<T>()` means to call other stuff.
//    The code defined in the `dlib_*` can receive implementations of many interfaces at once.
//    At the very least, the logger and the lifetime managers come in handy.
//
// Usage:
// 1) Create an interface provider as `class MyInterface : public virtual IGeneric`.
// 2) Have the `dlib`-exposed function be some `extern "C" void MyDLibExternalFunction(IGeneric& dlib);`
// 3) In that function, `dlib.Use<MyInterface>(...)`, see the `demo_*` in (some) C5T repo for details.
// 4) Call it with an instance of your `MyInterface`, see the `demo_*` in (some) C5T repo for details.

#include <string>
#include <functional>

// The thinnest possible wrapper over a loaded `dlib`.
struct C5T_DLib {
  virtual ~C5T_DLib() = default;
  virtual void* GetRawPF(std::string const& fn_name) = 0;
  template <typename F_PTR>
  F_PTR* Get(std::string const& fn_name) {
    return reinterpret_cast<F_PTR*>(GetRawPF(fn_name));
  }
};

// The "meta-interface" to pass interfaces (impl object instances by references) to and from dlib-s.
struct IGeneric {
 protected:
  virtual ~IGeneric() = default;

 public:
  template <class I, class F, class G = std::function<decltype(std::declval<F>()(*std::declval<I*>()))()>>
  decltype(std::declval<F>()(*std::declval<I*>())) Use(
      F&& f, G&& g = []() -> decltype(std::declval<F>()(*std::declval<I*>())) {
        return decltype(std::declval<F>()(std::declval<I&>()))();
      }) {
    if (I* i = dynamic_cast<I*>(this)) {
      return f(*i);
    } else {
      return g();
    }
  }
};

// Initialize, tell the `DLIB` framework which dir to load dynamic libraries from.
void C5T_DLIB_SET_BASE_DIR(std::string base_dir);

// List the currently loaded `DLIB`-s, for debugging purposes.
void C5T_DLIB_LIST(std::function<void(std::string)>);

// Use the `DLIB`, load it if needed, re-load it if needed.
// TODO(dkorolev): DLib lifetime management, this is the next step.
bool C5T_DLIB_USE(
    std::string const& lib_name, std::function<void(C5T_DLib&)>, std::function<void()> = [] {});

// Reloads the lib as needed, with the "symlink trick" so that the library is truly re-loaded.
enum class C5T_DLIB_RELOAD_STATUS : int { InternalError, Fail, Loaded, UpToDate, Reloaded };
struct C5T_DLIB_RELOAD_RESULT final {
  C5T_DLIB_RELOAD_STATUS res = C5T_DLIB_RELOAD_STATUS::InternalError;
  C5T_DLib* ptr = nullptr;
};

C5T_DLIB_RELOAD_RESULT C5T_DLIB_RELOAD(std::string const& lib_name);
