#include "lib_c5t_dlib.h"

#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <sys/stat.h>  // NOTE(dkorolev): This may require fixes for macOS.
#include <tuple>
#include <utility>

#include "bricks/util/singleton.h"

#include "bricks/system/syscalls.h"

constexpr static char const* const kPlatformSpecificDotSO =
#ifdef CURRENT_APPLE
    ".dylib"
#else
    ".so"
#endif
    ;

class C5T_DLib_Impl final : public C5T_DLib {
 private:
  current::bricks::system::DynamicLibrary dl_;

  std::mutex mutex;
  std::map<std::string, std::pair<void*, bool>> symbols;  // name -> { ptr, bool not_present }.

 protected:
  void* GetRawPF(std::string const& name) override {
    std::lock_guard lock(mutex);
    auto& p = symbols[name];
    if (p.first) {
      return p.first;
    } else if (p.second) {
      return nullptr;
    } else {
      try {
        void* r = dl_.template Get<void*>(name);
        if (r) {
          p.first = r;
          return r;
        } else {
          p.second = true;
          return nullptr;
        }
      } catch (current::bricks::system::DLSymException const&) {
        p.second = true;
        return nullptr;
      }
    }
  }

 public:
  C5T_DLib_Impl(std::string const& base_dir, std::string const& basename)
      : dl_(current::bricks::system::DynamicLibrary::CrossPlatform(base_dir + "/libdlib_ext_" + basename)) {
    // The on-load sequence.
    CallOrDefault<void()>("OnLoad");
  }
  ~C5T_DLib_Impl() {
    // The on-unload sequence.
    CallOrDefault<void()>("OnUnload");
  }
};

inline decltype(auto) StructStatIntoKey(struct stat const& data) {
#ifndef CURRENT_APPLE
  return std::make_tuple(data.st_ino, data.st_mtim.tv_sec, data.st_mtim.tv_nsec);
#else  // CURRENT_APPLE
  return std::make_tuple(data.st_ino, data.st_mtimespec.tv_sec, data.st_mtimespec.tv_nsec);
#endif
}

class C5T_DLibs_Manager final : public C5T_DLibs_Manager_Interface {
 public:
  using libkey_t = decltype(StructStatIntoKey(std::declval<struct stat const&>()));

  std::string base_dir = ".";
  std::string const start_time_us_as_string;

  std::mutex mutex;
  std::map<std::string, std::pair<libkey_t, std::unique_ptr<C5T_DLib_Impl>>> loaded_libs;
  std::vector<std::string> symlinks_created;

  C5T_DLibs_Manager() : start_time_us_as_string(current::ToString(current::time::Now().count())) {}

  ~C5T_DLibs_Manager() {
    std::vector<std::string> symlinks_to_remove = [this]() {
      std::lock_guard lock(mutex);
      return std::move(symlinks_created);
    }();
    for (auto const& s : symlinks_to_remove) {
      ::remove(s.c_str());
    }
  }

  std::string LibkeyToString(libkey_t const& key) {
    std::ostringstream oss;
    oss << start_time_us_as_string << '_' << std::get<0>(key) << '_' << std::get<1>(key) << '_' << std::get<2>(key);
    return oss.str();
  }

  void SetBaseDir(std::string s) override { base_dir = std::move(s); }

  C5T_DLIB_RELOAD_RESULT LoadLibAndReloadAsNeededFromLockedSection(std::string const& name) override {
    // NOTE(dkorolev): May well do some work outside the locked section, such as `::stat`. Later.
    try {
      // NOTE(dkorolev): This should be part of `C5T/current/bricks/system/syscalls.h`, in one place.
      std::string const orig_base_name = "libdlib_ext_" + name + kPlatformSpecificDotSO;
      std::string const orig_full_name = base_dir + '/' + orig_base_name;
      struct stat data;
      if (::stat(orig_full_name.c_str(), &data)) {
        // NOTE(dkorolev): Hijacking exception type, but okay for now.
        throw current::bricks::system::DLOpenException();
      }
      libkey_t const key = StructStatIntoKey(data);
      auto& placeholder = loaded_libs[name];
      if (placeholder.first == key && placeholder.second) {
        return {C5T_DLIB_RELOAD_STATUS::UpToDate, placeholder.second.get()};
      } else {
        placeholder.first = key;
        bool const had_existing = (placeholder.second != nullptr);
        placeholder.second = nullptr;
        auto const key_s = LibkeyToString(key);
        std::string const link_base_name_wo_ext = name + "_" + key_s;
        std::string const link_base_name = link_base_name_wo_ext + kPlatformSpecificDotSO;
        std::string const link_full_name = base_dir + "/libdlib_ext_" + link_base_name;
        if (::symlink(orig_base_name.c_str(), link_full_name.c_str())) {
          // NOTE(dkorolev): Hijacking exception type, but okay for now.
          throw current::bricks::system::DLOpenException();
        }
        symlinks_created.push_back(link_full_name);
        placeholder.second = std::make_unique<C5T_DLib_Impl>(base_dir, link_base_name_wo_ext);
        return {had_existing ? C5T_DLIB_RELOAD_STATUS::Reloaded : C5T_DLIB_RELOAD_STATUS::Loaded,
                placeholder.second.get()};
      }
    } catch (current::bricks::system::DLOpenException const&) {
      return {C5T_DLIB_RELOAD_STATUS::Fail, nullptr};
    }
  }

  bool UseDLib(std::string const& name,
               std::function<void(C5T_DLib&)> cb_success,
               std::function<void()> cb_fail) override {
    std::lock_guard lock(mutex);
    auto const r = LoadLibAndReloadAsNeededFromLockedSection(name);
    // TODO(dkorolev): Change the lock to the per-lib one here, release the lock for all the libs!
    if (r.ptr) {
      cb_success(*r.ptr);
      return true;
    } else {
      cb_fail();
      return false;
    }
  }

  C5T_DLIB_RELOAD_RESULT DoLoadOrReloadDLib(std::string const& name) override {
    std::lock_guard lock(mutex);
    return LoadLibAndReloadAsNeededFromLockedSection(name);
  }

  void ListDLibs(std::function<void(std::string)> f) override {
    std::lock_guard lock(mutex);
    for (auto const& [k, _] : loaded_libs) {
      f(k);
    }
  }
};

struct C5T_DLibs_ManagerContainer final {
  C5T_DLibs_Manager_Interface* pimpl = nullptr;
};

inline C5T_DLibs_Manager_Interface& C5T_DLibs_Manager_Instance() {
  auto& singleton = current::Singleton<C5T_DLibs_ManagerContainer>();
  if (!singleton.pimpl) {
    std::cerr << "The `C5T_DLIB_*` subsystem was not initialized, use `C5T_DLIB_SET_BASE_DIR`." << std::endl;
    ::abort();
  }
  return *singleton.pimpl;
}

void C5T_DLIB_USE_PROVIDED_INSTANCE_AND_SET_BASE_DIR(C5T_DLibs_Manager_Interface& instance, std::string base_dir) {
  auto& singleton = current::Singleton<C5T_DLibs_ManagerContainer>();
  instance.SetBaseDir(std::move(base_dir));
  singleton.pimpl = &instance;
}

void C5T_DLIB_SET_BASE_DIR(std::string base_dir) {
  C5T_DLIB_USE_PROVIDED_INSTANCE_AND_SET_BASE_DIR(current::Singleton<C5T_DLibs_Manager>(), std::move(base_dir));
}

std::unique_ptr<C5T_DLibs_Manager_Interface> INTERNAL_C5T_DLIB_CREATE_SCOPED_INSTANCE() {
  return std::make_unique<C5T_DLibs_Manager>();
}

void C5T_DLIB_LIST(std::function<void(std::string)> f) { C5T_DLibs_Manager_Instance().ListDLibs(std::move(f)); }

bool C5T_DLIB_USE(std::string const& lib_name,
                  std::function<void(C5T_DLib&)> cb_success,
                  std::function<void()> cb_fail) {
  return C5T_DLibs_Manager_Instance().UseDLib(lib_name, std::move(cb_success), std::move(cb_fail));
}

C5T_DLIB_RELOAD_RESULT C5T_DLIB_RELOAD(std::string const& lib_name) {
  return C5T_DLibs_Manager_Instance().DoLoadOrReloadDLib(lib_name);
}
