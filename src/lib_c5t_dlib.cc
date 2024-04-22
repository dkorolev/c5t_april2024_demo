#include "lib_c5t_dlib.h"

#include <map>
#include <mutex>

#include "bricks/util/singleton.h"

#include "bricks/system/syscalls.h"

struct C5T_DLib_Impl final : C5T_DLib {
  current::bricks::system::DynamicLibrary dl;

  std::mutex mutex;
  std::map<std::string, std::pair<void*, bool>> symbols;  // name -> { ptr, bool not_present }.

  C5T_DLib_Impl(std::string const& base_dir, std::string const& basename)
      : dl(current::bricks::system::DynamicLibrary::CrossPlatform(base_dir + "/libdlib_ext_" + basename)) {}

  void* GetRawPF(std::string const& name) override {
    std::lock_guard lock(mutex);
    auto& p = symbols[name];
    if (p.first) {
      return p.first;
    } else if (p.second) {
      return nullptr;
    } else {
      try {
        void* r = dl.template Get<void*>(name);
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
};

struct C5T_DLibs_Manager final {
  std::string base_dir = ".";

  std::mutex mutex;
  std::map<std::string, std::unique_ptr<C5T_DLib_Impl>> libs;
  // uint32_t idx = 0u; <-- for dynamic reloads, coming soon

  void SetBaseDir(std::string s) { base_dir = std::move(s); }

  void UseDLib(std::string const& name, std::function<void(C5T_DLib&)> cb_success, std::function<void()> cb_fail) {
    std::lock_guard lock(mutex);
    try {
      auto& p = libs[name];
      if (!p) {
        p = std::make_unique<C5T_DLib_Impl>(base_dir, name);
      }
      cb_success(*p);
    } catch (current::bricks::system::DLOpenException const&) {
      libs.erase(name);
      cb_fail();
    }
  }

  void ListDLibs(std::function<void(std::string)> f) {
    std::lock_guard lock(mutex);
    for (auto const& [k, _] : libs) {
      f(k);
    }
  }
};

void C5T_DLIB_SET_BASE_DIR(std::string base_dir) {
  current::Singleton<C5T_DLibs_Manager>().SetBaseDir(std::move(base_dir));
}

void C5T_DLIB_LIST(std::function<void(std::string)> f) {
  current::Singleton<C5T_DLibs_Manager>().ListDLibs(std::move(f));
}

void C5T_DLIB_USE(std::string const& name, std::function<void(C5T_DLib&)> cb_success, std::function<void()> cb_fail) {
  current::Singleton<C5T_DLibs_Manager>().UseDLib(name, std::move(cb_success), std::move(cb_fail));
}
