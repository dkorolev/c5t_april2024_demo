#include "lib_c5t_lifetime_manager.h"

#include <iostream>

#include "bricks/strings/util.h"
#include "bricks/file/file.h"
#include "bricks/sync/owned_borrowed.h"

static std::string LifetimeTrackedInstanceBaseName(std::string const& s) {
  char const* r = s.c_str();
  for (char const* p = r; p[0] && p[1]; ++p) {
    if (*p == current::FileSystem::GetPathSeparator()) {
      r = p + 1;
    }
  }
  return r;
}

LifetimeTrackedInstance::LifetimeTrackedInstance(std::string desc,
                                                 std::string file,
                                                 uint32_t line,
                                                 std::chrono::microseconds t)
    : description(std::move(desc)),
      file_fullname(std::move(file)),
      file_basename(LifetimeTrackedInstanceBaseName(file_fullname)),
      line_as_number(line),
      line_as_string(current::ToString(line_as_number)),
      t_added(t) {}

std::string LifetimeTrackedInstance::ToShortString() const {
  return description + " @ " + file_basename + ':' + line_as_string;
}

class LifetimeManagerSingletonImpl : public LifetimeManagerSingletonInterface {
 private:
  // The `TrackedInstances` waitable atomic keeps track of everything that needs to be terminated before `::exit()`.
  // If at least one tracked instance remains unfinished within the grace period, `::abort()` is performed instead.
  // As a nice benefit, for tracked instances it is also journaled when and from what FILE:LINE did they start.
  struct TrackedInstances final {
    uint64_t next_id_desc = 0u;  // Descending so that in the naturally sorted order the more recent items come first.
    std::map<uint64_t, LifetimeTrackedInstance> still_alive;
  };

  struct OfExtendedLifetime final {
    mutable std::atomic_bool logger_initialized_ = std::atomic_bool(false);

    mutable std::mutex logger_mutex_;
    mutable std::function<void(std::string const&)> logger_ = nullptr;

    current::WaitableAtomic<std::atomic_bool> termination_initiated_;
    std::atomic_bool& termination_initiated_atomic_;

    current::WaitableAtomic<TrackedInstances> tracking_;

    std::vector<std::thread> threads_to_join_;
    std::mutex threads_to_join_mutex_;

    OfExtendedLifetime()
        : termination_initiated_(false),
          termination_initiated_atomic_(*termination_initiated_.MutableScopedAccessor()) {}
  };
  current::Owned<OfExtendedLifetime> extended_;

  // Set to `true` if the lifetime management singleton is being destructed organically,
  // in which case the tracked processes will be stopped, `:abort()` will be called as needed,
  // but `::exit(0)` will not be enforced. Important for `googletest` tests to actually fail when they do fail.
  bool organic_exit_ = false;

  void Log(std::string const& s) const {
    std::lock_guard lock(extended_->logger_mutex_);
    if (extended_->logger_) {
      extended_->logger_(s);
    } else {
      std::cerr << "LIFETIME_MANAGER_LOG: " << s << std::endl;
    }
  }

 public:
  void SetLogger(std::function<void(std::string const&)> logger) const override {
    extended_->logger_initialized_ = true;
    {
      std::lock_guard lock(extended_->logger_mutex_);
      extended_->logger_ = logger;
    }
  }

  void EnsureHasLogger() const {
    if (!extended_->logger_initialized_) {
      SetLogger([](std::string const& line) { std::cerr << "LIFETIME_MANAGER: " << line << std::endl; });
    }
  }

  size_t TrackingAdd(std::string const& description, char const* file, size_t line) override {
    EnsureHasLogger();
    return extended_->tracking_.MutableUse([=](TrackedInstances& trk) {
      uint64_t const id = trk.next_id_desc;
      --trk.next_id_desc;
      trk.still_alive[id] = LifetimeTrackedInstance(description, file, line);
      return id;
    });
  }

  void TrackingRemove(size_t id) override {
    extended_->tracking_.MutableUse([=](TrackedInstances& trk) { trk.still_alive.erase(id); });
  }

  // To run "global" threads instead of `.detach()`-ing them: these threads will be `.join()`-ed upon termination.
  // This function is internal, and it assumes that the provided thread itself respects the termination signal.
  // (There is a mechanism to guard against this too, with the second possible `::abort()` clause, but still.)
  void EmplaceThreadImpl(std::thread t) override {
    EnsureHasLogger();
    extended_->termination_initiated_.ImmutableUse([&](bool already_terminating) {
      // It's OK to just not start the thread if already in the "terminating" mode.
      if (!already_terminating) {
        std::lock_guard lock(extended_->threads_to_join_mutex_);
        extended_->threads_to_join_.emplace_back(std::move(t));
      }
    });
  }

#if 1
  [[nodiscard]] LifetimeTerminationSignalScope SubscribeToTerminationEvent(std::function<void()> f0) override {
    // Ensures that `f0()` will only be called once, possibly from the very call to `SubscribeToTerminationEvent()`.
    auto f = [borrowed_extended = current::Borrowed<OfExtendedLifetime>(extended_),
              called = std::make_shared<current::WaitableAtomic<bool>>(false),
              f1 = std::move(f0)]() mutable {
      // Guard against spurious wakeups.
      if (borrowed_extended->termination_initiated_atomic_ ||
          borrowed_extended->termination_initiated_.ImmutableUse([](std::atomic_bool const& b) { return b.load(); })) {
        // Guard against calling the user-provided `f0()` more than once.
        if (called->MutableUse([](bool& called_flag) {
              if (called_flag) {
                return false;
              } else {
                called_flag = true;
                return true;
              }
            })) {
          borrowed_extended = nullptr;  // NOTE(dkorolev): <-- this statement is critical for graceful shutdown!
          f1();
        }
      }
    };
    auto result = extended_->termination_initiated_.Subscribe(f);
    // Here it is safe to use `termination_initiated_atomic_`, since the guarantee provided is "at least once",
    // and, coupled with the `still_active` guard, it becomes "exactly once" for `f` to be called.
    if (extended_->termination_initiated_atomic_) {
      f();
    }
    return result;
  }
#else
  // NOTE(dkorolev): With the "<--" statement above this implementation is not neceessary.
  [[nodiscard]] LifetimeTerminationSignalScope SubscribeToTerminationEvent(std::function<void()> f0) override {
    EnsureHasLogger();
    struct LifetimeTerminationSignalScopeRealImpl final : LifetimeTerminationSignalScopeImpl {
      current::Borrowed<OfExtendedLifetime> borrowed_;
      std::function<void()> f_;
      current::WaitableAtomicSubscriberScope wa_scope_;
      std::atomic_uint64_t call_count_ = std::atomic_uint64_t(0ull);
      LifetimeTerminationSignalScopeRealImpl(current::Borrowed<OfExtendedLifetime> borrowed, std::function<void()> f)
          : borrowed_(std::move(borrowed)),
            f_(std::move(f)),
            wa_scope_(borrowed_->termination_initiated_.Subscribe([this]() { Signal(); })) {
        if (borrowed_->termination_initiated_atomic_) {
          Signal();
        }
      }
      void Signal() {
        uint64_t const v = call_count_++;
        if (!v) {
          f_();
        }
        borrowed_ = nullptr;
      }
    };
    return std::make_unique<LifetimeTerminationSignalScopeRealImpl>(current::Borrowed<OfExtendedLifetime>(extended_),
                                                                    std::move(f0));
  }
#endif

  void DumpActive(std::function<void(LifetimeTrackedInstance const&)> f0 = nullptr) const override {
    EnsureHasLogger();
    std::function<void(LifetimeTrackedInstance const&)> f =
        f0 != nullptr ? f0 : [this](LifetimeTrackedInstance const& s) { Log(s.ToShortString()); };
    extended_->tracking_.ImmutableUse([&f](TrackedInstances const& trk) {
      for (auto const& [_, s] : trk.still_alive) {
        f(s);
      }
    });
  }

  bool IsShuttingDown() const override { return extended_->termination_initiated_atomic_; }

  void WaitUntilTimeToDie() const override {
    EnsureHasLogger();
    extended_->termination_initiated_.Wait();
  }

  bool WaitUntilTimeToDieFor(std::chrono::microseconds dt) const override {
    EnsureHasLogger();
    extended_->termination_initiated_.WaitFor(dt);
    return !extended_->termination_initiated_atomic_;
  }

  void DoExit(int exit_code = 0, std::chrono::milliseconds graceful_delay = std::chrono::seconds(2)) {
    auto const t0 = current::time::Now();
    std::map<uint64_t, LifetimeTrackedInstance> original_still_alive =
        extended_->tracking_.ImmutableScopedAccessor()->still_alive;
    std::vector<uint64_t> still_alive_ids;
    for (auto const& e : original_still_alive) {
      still_alive_ids.push_back(e.first);
    }
    bool ok = false;
    extended_->tracking_.WaitFor(
        [this, &ok, &original_still_alive, &still_alive_ids, t0](TrackedInstances const& trk) {
          std::vector<uint64_t> next_still_alive_ids;
          auto const t1 = current::time::Now();
          for (uint64_t id : still_alive_ids) {
            auto const cit = trk.still_alive.find(id);
            if (cit == std::end(trk.still_alive)) {
              auto const& e = original_still_alive[id];
              // NOTE(dkorolev): The order of `Gone after`-s may not be exactly the order of stuff terminating.
              // TODO(dkorolev): May well tweak this one day.
              Log(current::strings::Printf("Gone after %.3lfs: %s @ %s:%d",
                                           1e-6 * (t1 - t0).count(),
                                           e.description.c_str(),
                                           e.file_basename.c_str(),
                                           e.line_as_number));
            } else {
              next_still_alive_ids.push_back(id);
            }
          }
          still_alive_ids = std::move(next_still_alive_ids);
          if (trk.still_alive.empty()) {
            ok = true;
            return true;
          } else {
            return false;
          }
        },
        graceful_delay);
    if (ok) {
      Log("Main termination sequence successful, joining the presumably-done threads.");
      std::vector<std::thread> threads_to_join = [this]() {
        std::lock_guard lock(extended_->threads_to_join_mutex_);
        return std::move(extended_->threads_to_join_);
      }();
      current::WaitableAtomic<bool> threads_joined_successfully(false);
      std::thread threads_joiner([&threads_to_join, &threads_joined_successfully]() {
        for (auto& t : threads_to_join) {
          t.join();
        }
        threads_joined_successfully.SetValue(true);
      });
      bool need_to_abort_because_threads_are_not_all_joined = true;
      threads_joined_successfully.WaitFor(
          [&need_to_abort_because_threads_are_not_all_joined](bool b) {
            if (b) {
              need_to_abort_because_threads_are_not_all_joined = false;
              return true;
            } else {
              return false;
            }
          },
          graceful_delay);
      if (!need_to_abort_because_threads_are_not_all_joined) {
        Log("Termination sequence successful, all threads joined.");
        threads_joiner.join();
        Log("Termination sequence successful, all done.");
        if (!organic_exit_) {
          ::exit(exit_code);
        }
      } else {
        Log("");
        Log("Uncooperative threads remain, time to `abort()`.");
        ::abort();
      }
    } else {
      Log("");
      Log("Termination sequence unsuccessful, still has offenders.");
      extended_->tracking_.ImmutableUse([this](TrackedInstances const& trk) {
        for (auto const& [_, s] : trk.still_alive) {
          Log("Offender: " + s.ToShortString());
        }
      });
      Log("");
      Log("Time to `abort()`.");
      ::abort();
    }
  }

  void Exit(int exit_code = 0, std::chrono::milliseconds graceful_delay = std::chrono::seconds(2)) override {
    bool const previous_value = extended_->termination_initiated_.MutableUse([](std::atomic_bool& already_terminating) {
      bool const retval = already_terminating.load();
      already_terminating = true;
      return retval;
    });

    if (previous_value) {
      Log("Ignoring a consecutive call to `LIFETIME_MANAGER_EXIT()`.");
    } else {
      Log("Initating termination sequence per `LIFETIME_MANAGER_EXIT(" + current::ToString(exit_code) + ")`.");
      DoExit(exit_code, graceful_delay);
    }
  }

  ~LifetimeManagerSingletonImpl() {
    bool const previous_value = extended_->termination_initiated_.MutableUse([](std::atomic_bool& already_terminating) {
      bool const retval = already_terminating.load();
      already_terminating = true;
      return retval;
    });
    if (!previous_value) {
      Log("");
      Log("The program is terminating organically.");

      organic_exit_ = true;
      DoExit();
    }
  }
};

LifetimeManagerSingletonInterface& LifetimeManagerSingletonInstance() {
  return current::Singleton<LifetimeManagerSingletonImpl>();
}
