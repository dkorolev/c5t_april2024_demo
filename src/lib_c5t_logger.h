#pragma once

#include <iostream>  // IWYU pragma: keep
#include <memory>
#include <functional>

#include "bricks/util/singleton.h"  // IWYU pragma: keep

namespace current::logger {

struct C5T_LOGGER_LogLineWriterInterface {
  virtual ~C5T_LOGGER_LogLineWriterInterface() = default;
  virtual std::ostream& GetOStream() = 0;
};

class C5T_LOGGER_LogLineWriter final {
 private:
  std::unique_ptr<C5T_LOGGER_LogLineWriterInterface> pimpl_;

 public:
  C5T_LOGGER_LogLineWriter() = delete;
  C5T_LOGGER_LogLineWriter(std::unique_ptr<C5T_LOGGER_LogLineWriterInterface> pimpl) : pimpl_(std::move(pimpl)) {}

  template <typename T>
  C5T_LOGGER_LogLineWriter& operator<<(T&& e) {
    pimpl_->GetOStream() << std::forward<T>(e);
    return *this;
  }
};

struct C5T_LOGGER_Interface {
  virtual ~C5T_LOGGER_Interface() = default;

  virtual C5T_LOGGER_LogLineWriter NewLineWriter() = 0;
  virtual void WriteLine(std::string const&) = 0;

  template <typename T>
  C5T_LOGGER_LogLineWriter operator<<(T&& e) {
    auto writer = NewLineWriter();
    writer << std::forward<T>(e);
    return writer;
  }
};

struct C5T_LOGGER_SINGLETON_Interface {
  virtual ~C5T_LOGGER_SINGLETON_Interface() = default;

  virtual void SetLogsDir(std::string base_path) = 0;
  virtual void C5T_LOGGER_LIST_Impl(
      std::function<void(std::string const& name, std::string const& latest_file)> cb) const = 0;
  virtual void C5T_LOGGER_FIND_Impl(std::string const& key,
                                    std::function<void(std::string const& latest_file)> cb_found,
                                    std::function<void()> cb_notfound) const = 0;

  virtual C5T_LOGGER_Interface& operator[](std::string const& log_file_name) = 0;
};

#ifndef C5T_DLIB
// NOTE(dkorolev): This is deliberately not "pimpl", since it's not to be used from `dlib_*.cc` sources!
C5T_LOGGER_SINGLETON_Interface& C5T_LOGGER_CREATE_SINGLETON();
#endif

struct C5T_LOGGER_SINGLETON_Holder final {
  C5T_LOGGER_SINGLETON_Interface* ptr = nullptr;
  C5T_LOGGER_SINGLETON_Interface& Use(C5T_LOGGER_SINGLETON_Interface& impl) {
    ptr = &impl;
    return impl;
  }
  C5T_LOGGER_SINGLETON_Interface& Val() {
    if (ptr == nullptr) {
#ifndef C5T_DLIB
      return C5T_LOGGER_CREATE_SINGLETON();
#else
      std::cerr << "FATAL: No logger initialized.\n";
      __builtin_trap();
#endif
    }
    return *ptr;
  }
};

}  // namespace current::logger

inline current::logger::C5T_LOGGER_SINGLETON_Interface& C5T_LOGGER() {
  return current::Singleton<current::logger::C5T_LOGGER_SINGLETON_Holder>().Val();
}

inline void C5T_LOGGER_USE(current::logger::C5T_LOGGER_SINGLETON_Interface& impl) {
  current::Singleton<current::logger::C5T_LOGGER_SINGLETON_Holder>().Use(impl);
}

template <class T>
void C5T_LOGGER_USE(T&& wrapper) {
  C5T_LOGGER_USE(wrapper.Logger());
}

#ifndef C5T_DLIB
inline void C5T_LOGGER_SET_LOGS_DIR(std::string dir) { current::logger::C5T_LOGGER_CREATE_SINGLETON().SetLogsDir(dir); }
#endif

#define C5T_LOGGER_LIST(cb) C5T_LOGGER().C5T_LOGGER_LIST_Impl(cb)
#define C5T_LOGGER_FIND(key, cb_found, cb_notfound) C5T_LOGGER().C5T_LOGGER_FIND_Impl(key, cb_found, cb_notfound)

// NOTE(dkorolev): This is deliberately not "pimpl", since it's not to be used from `dlib_*.cc` sources!
inline current::logger::C5T_LOGGER_Interface& C5T_LOGGER(std::string const& name) { return C5T_LOGGER()[name]; }

#if 0

// NOTE(dkorolev): Non-persisted logs are an idea for the future when there are HTTP-based ways to listen to logs.

struct C5T_LOGGER_Impl_NoPersist final {
  C5T_LOGGER_Impl* impl_;
  C5T_LOGGER_Impl_NoPersist() = delete;
  explicit C5T_LOGGER_Impl_NoPersist(C5T_LOGGER_Impl& impl) : impl_(&impl) {}

  template <typename T>
  C5T_LOGGER_LogLineWriter operator<<(T&& e) {
    C5T_LOGGER_LogLineWriter w(impl_, false);
    w << std::forward<T>(e);
    return w;
  }
};

inline C5T_LOGGER_Impl_NoPersist C5T_LOGGER_NOPERSIST(std::string const& name) {
  return C5T_LOGGER_Impl_NoPersist(C5T_LOGGER(name));
}

#endif
