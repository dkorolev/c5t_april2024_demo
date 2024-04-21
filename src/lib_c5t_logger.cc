#include "lib_c5t_logger.h"

#include <functional>

#include "bricks/exception.h"
#include "bricks/time/chrono.h"
#include "bricks/file/file.h"

#include "bricks/sync/waitable_atomic.h"

#include "typesystem/optional.h"
#include "typesystem/helpers.h"

namespace current::logger::impl {

#if 1  // NOTE(dkorolev): I've tested this with Google Sheets by adding a chart. This date & time format works natively!
constexpr static char const* const kLogFmt = "%Y/%m/%d %H:%M:%S";  // The format that makes sense.
#else
constexpr static char const* const kLogFmt = "%m/%d/%Y %H:%M:%S";  // The US format.
#endif

struct C5T_LOGGER_LogLineWriterImpl final : C5T_LOGGER_LogLineWriterInterface {
  C5T_LOGGER_Interface* logger_;
  bool to_file_;
  std::ostringstream oss_;

  C5T_LOGGER_LogLineWriterImpl() = delete;
  C5T_LOGGER_LogLineWriterImpl(C5T_LOGGER_LogLineWriterImpl&&) = default;

  std::ostream& GetOStream() override { return oss_; }

  explicit C5T_LOGGER_LogLineWriterImpl(C5T_LOGGER_Interface* self, bool to_file = true)
      : logger_(self), to_file_(to_file) {}

  ~C5T_LOGGER_LogLineWriterImpl() override {
    // TODO(dkorolev): Mark idle-ness?
    if (to_file_) {
      logger_->WriteLine(oss_.str());
    }
  }
};

// This macro is used in more than one place.
#ifdef LOG_IMPL
#error "`LOG_IMPL` should not be `#define`-d by this point."
#endif
#define LOG_IMPL(s) \
  inner.active->second << TS.count() << '\t' << current::FormatDateTime(TS, kLogFmt) << '\t' << s << std::endl;

struct C5T_LOGGER_Impl_Stdout final {};

struct C5T_LOGGER_Impl final : C5T_LOGGER_Interface {
  Optional<std::string> const log_file_name_;

  struct InnerLoggerImpl final {
    // Keeps { log file name, fstream }, to re-create with one-liners.
    using active_t = std::pair<std::string, std::ofstream>;
    std::unique_ptr<active_t> active;
    ~InnerLoggerImpl() {
      if (active && active->second) {
        auto const TS = current::time::Now();
        auto const& inner = *this;
        LOG_IMPL("gracefully closing this log file");
      }
    }

    struct Construct final {};
    explicit InnerLoggerImpl(Construct, std::string) {}

    struct ConstructForStdout final {};
    explicit InnerLoggerImpl(ConstructForStdout) {}

    InnerLoggerImpl(InnerLoggerImpl const&) = delete;
    InnerLoggerImpl& operator=(InnerLoggerImpl const&) = delete;
    InnerLoggerImpl(InnerLoggerImpl&&) = delete;
    InnerLoggerImpl& operator=(InnerLoggerImpl&&) = delete;
  };

  current::WaitableAtomic<InnerLoggerImpl> inner_logger_;

  C5T_LOGGER_Impl() = delete;
  C5T_LOGGER_Impl(std::string log_file_name)
      : log_file_name_(std::move(log_file_name)), inner_logger_(InnerLoggerImpl::Construct(), Value(log_file_name_)) {}
  C5T_LOGGER_Impl(C5T_LOGGER_Impl_Stdout) : inner_logger_(InnerLoggerImpl::ConstructForStdout()) {}

  C5T_LOGGER_LogLineWriter NewLineWriter() override {
    return C5T_LOGGER_LogLineWriter(std::make_unique<C5T_LOGGER_LogLineWriterImpl>(this));
  }

  void WriteLineToFile(std::string const& s, std::string const& log_file_name) {
    auto const TS = current::time::Now();
    auto const ts = current::FormatDateTime(TS, "-%Y%m%d-%H");
    auto const fn = log_file_name + ts + ".txt";
    inner_logger_.MutableUse([&](InnerLoggerImpl& inner) {
      bool log_starting_new = false;
      bool create_or_recreate = false;
      if (!inner.active) {
        log_starting_new = true;
        create_or_recreate = true;
      } else if (inner.active->first != fn) {
        LOG_IMPL("moving on to " << fn);
        create_or_recreate = true;
      }
      if (create_or_recreate) {
        std::string renamed_file_name;
        try {
          if (current::FileSystem::GetFileSize(fn)) {
            renamed_file_name = log_file_name + ts + '.' + current::ToString(TS.count()) + ".txt";
            current::FileSystem::RenameFile(fn, renamed_file_name);
          }
        } catch (current::Exception const&) {
          // Consciously ignore exceptions here.
        }
        inner.active =
            std::make_unique<InnerLoggerImpl::active_t>(fn, std::ofstream(fn, std::fstream::app | std::fstream::ate));
        if (!renamed_file_name.empty()) {
          LOG_IMPL("the old log file was renamed to " << renamed_file_name);
        }
      }
      if (log_starting_new) {
        LOG_IMPL("started a new log file")
      }
      LOG_IMPL(s);
    });
  }
  virtual void WriteLine(std::string const& s) override {
    if (Exists(log_file_name_)) {
      WriteLineToFile(s, Value(log_file_name_));
    } else {
      struct InjectedInner {
        struct InjectedActive {
          std::ostream& second = std::cout;
          InjectedActive* operator->() { return this; }
        } active;
      } inner;
      auto const TS = current::time::Now();
      // auto const ts = current::FormatDateTime(TS, "-%Y%m%d-%H");
      LOG_IMPL("LOGGER WITH NO BASE DIR SET:\t" + s);
    }
  }
#undef LOG_IMPL
};

struct C5T_LOGGER_SINGLETON_Impl final : C5T_LOGGER_SINGLETON_Interface {
  struct InnerSingletonImpl final {
    std::atomic_bool initialized;
    Optional<std::string> base_path;
    std::unordered_map<std::string, std::unique_ptr<C5T_LOGGER_Impl>> per_file_loggers;
    InnerSingletonImpl() : initialized(false) {}
  };
  current::WaitableAtomic<InnerSingletonImpl> inner_singleton_;
  std::atomic_bool const& initialized_;

  C5T_LOGGER_SINGLETON_Impl()
      : inner_singleton_(), initialized_(inner_singleton_.ImmutableScopedAccessor()->initialized) {}

  ~C5T_LOGGER_SINGLETON_Impl() override {}

  void SetLogsDir(std::string base_path) override {
    if (!inner_singleton_.MutableUse([&](InnerSingletonImpl& inner) {
          if (inner.initialized) {
            return false;
          }
          inner.initialized = true;
          inner.base_path = std::move(base_path);
          return true;
        })) {
      // NOTE(dkorolev): Re-setting the logs path will not re-create already existing per-file logger instances.
      std::cerr << "WARNING: Logs dir is set more than once." << std::endl;
    }
  }

  void C5T_LOGGER_LIST_Impl(
      std::function<void(std::string const& name, std::string const& latest_file)> cb) const override {
    inner_singleton_.ImmutableUse([&](InnerSingletonImpl const& inner) {
      for (auto const& e : inner.per_file_loggers) {
        e.second->inner_logger_.ImmutableUse([&](C5T_LOGGER_Impl::InnerLoggerImpl const& inner2) {
          if (inner2.active) {
            cb(e.first, inner2.active->first);
          }
        });
      }
    });
  }
  void C5T_LOGGER_FIND_Impl(std::string const& key,
                            std::function<void(std::string const& latest_file)> cb_found,
                            std::function<void()> cb_notfound) const override {
    inner_singleton_.ImmutableUse([&](InnerSingletonImpl const& inner) {
      auto const& e = inner.per_file_loggers.find(key);
      if (e != std::end(inner.per_file_loggers)) {
        e->second->inner_logger_.ImmutableUse([&](C5T_LOGGER_Impl::InnerLoggerImpl const& inner2) {
          if (inner2.active) {
            cb_found(inner2.active->first);
          } else {
            cb_notfound();
          }
        });
      } else {
        cb_notfound();
      }
    });
  }

  C5T_LOGGER_Impl& operator[](std::string const& log_file_name) override {
    return inner_singleton_.MutableUse([&](InnerSingletonImpl& inner) -> C5T_LOGGER_Impl& {
      auto& placeholder = inner.per_file_loggers[log_file_name];
      if (!placeholder) {
        if (Exists(inner.base_path)) {
          placeholder =
              std::make_unique<C5T_LOGGER_Impl>(current::FileSystem::JoinPath(Value(inner.base_path), log_file_name));
        } else {
          // TODO(dkorolev): Two IMPLs, one for file one for no file.
          placeholder = std::make_unique<C5T_LOGGER_Impl>(C5T_LOGGER_Impl_Stdout());
        }
      }
      return *placeholder;
    });
  }
};

}  // namespace current::logger::impl

// NOTE(dkorolev): This method is deliberately not "pimpl", since it's not to be used from `dlib_*.cc` sources!
current::logger::C5T_LOGGER_SINGLETON_Interface& current::logger::C5T_LOGGER_CREATE_SINGLETON() {
  return current::Singleton<current::logger::C5T_LOGGER_SINGLETON_Holder>().Use(
      current::Singleton<current::logger::impl::C5T_LOGGER_SINGLETON_Impl>());
}
