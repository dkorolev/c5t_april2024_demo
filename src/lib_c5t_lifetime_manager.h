#pragma once

#include <atomic>
#include <thread>

#include "bricks/sync/waitable_atomic.h"
#include "bricks/time/chrono.h"

struct LifetimeTrackedInstance final {
  std::string description;
  std::string file_fullname;
  std::string file_basename;
  uint32_t line_as_number;
  std::string line_as_string;
  std::chrono::microseconds t_added;

  LifetimeTrackedInstance() = default;
  LifetimeTrackedInstance(std::string desc,
                          std::string file,
                          uint32_t line,
                          std::chrono::microseconds t = current::time::Now());

  std::string ToShortString() const;
};

class LifetimeManagerSingletonInterface {
 protected:
  LifetimeManagerSingletonInterface() = default;

 public:
  virtual void SetLogger(std::function<void(std::string const&)> logger) const = 0;

  // NOTE(dkorolev): These are internal and could be refactored and/or wrapped into some `Scope` class, but later.
  virtual size_t TrackingAdd(std::string const& description, char const* file, size_t line) = 0;
  virtual void TrackingRemove(size_t id) = 0;

  // To run "global" threads instead of `.detach()`-ing them: these threads will be `.join()`-ed upon termination.
  // This function is internal, and it assumes that the provided thread itself respects the termination signal.
  // (There is a mechanism to guard against this too, with the second possible `::abort()` clause, but still.)
  virtual void EmplaceThreadImpl(std::thread t) = 0;

  // This ensures `f` will be called once and only once if and when it's time to terminate.
  [[nodiscard]] virtual current::WaitableAtomicSubscriberScope SubscribeToTerminationEvent(std::function<void()> f) = 0;

  // To list the currently active threads.
  virtual void DumpActive(std::function<void(LifetimeTrackedInstance const&)> f = nullptr) const = 0;

  // A quick O(1), atomic, way to tell if the code is shutting down.
  virtual bool IsShuttingDown() const = 0;

  // This function is only useful when called from a thread in the scope of important data was created.
  // Generally, this is the way to create lifetime-manager-friendly singleton instances:
  // 1) Spawn a thread.
  // 2) Create everything in it, preferably as `Owned<WaitableAtomic<...>>`.
  // 3) At the end of this thread wait until it is time to die.
  // 4) Once it is time to die, everything this thread has created will be destroyed, gracefully or forcefully.
  virtual void WaitUntilTimeToDie() const = 0;

  // Returns `false` if it's time to terminate.
  // TODO(dkorolev): Custom `enum class` return type?
  virtual bool WaitUntilTimeToDieFor(std::chrono::microseconds dt) const = 0;

  // This is the way to terminate the program.
  virtual void Exit(int exit_code = 0, std::chrono::milliseconds graceful_delay = std::chrono::seconds(2)) = 0;
};

LifetimeManagerSingletonInterface& LifetimeManagerSingletonInstance();

#define C5T_LIFETIME_MANAGER_SET_LOGGER(logger) LifetimeManagerSingletonInstance().SetLogger(logger)

// O(1), just `.load()`-s the atomic.
#define C5T_LIFETIME_MANAGER_SHUTTING_DOWN LifetimeManagerSingletonInstance().IsShuttingDown()

// Returns the `[[nodiscard]]`-ed scope for the lifetime of the passed-in lambda being registered.
template <class F>
[[nodiscard]] inline current::WaitableAtomicSubscriberScope C5T_LIFETIME_MANAGER_NOTIFY_OF_SHUTDOWN(F&& f) {
  return LifetimeManagerSingletonInstance().SubscribeToTerminationEvent(std::forward<F>(f));
}

// Waits forever. Useful for "singleton" threads and in `C5T_POPEN2()` runners for what should run forever.
inline void C5T_LIFETIME_MANAGER_SLEEP_UNTIL_SHUTDOWN() { LifetimeManagerSingletonInstance().WaitUntilTimeToDie(); }

// Use in place of `std::this_thread::sleep_for(...)`. Also returns `false` if it's time to die.
template <class DT>
inline bool C5T_LIFETIME_MANAGER_SLEEP_FOR(DT&& dt) {
  return LifetimeManagerSingletonInstance().WaitUntilTimeToDieFor(std::forward<DT>(dt));
}

#define C5T_LIFETIME_MANAGER_TRACKED_DEBUG_DUMP(...) LifetimeManagerSingletonInstance().DumpActive(__VA_ARGS__)

inline void C5T_LIFETIME_MANAGER_EXIT(int code = 0,
                                      std::chrono::milliseconds graceful_delay = std::chrono::seconds(2)) {
  LifetimeManagerSingletonInstance().Exit(code, graceful_delay);
}

// This is a bit of a "singleton instance" creator.
// Not recommended to use overall, as it would create one thread per instance,
// as opposed to "a single thread to own them all". But okay for the test and for quick experiments.
// TODO(dkorolev): One day the same semantics could be used to leverage that "single thread to own them all".
template <class T, class... ARGS>
T& CreateLifetimeTrackedInstance(char const* file, int line, std::string const& text, ARGS&&... args) {
  current::WaitableAtomic<T*> result(nullptr);
  // Construct in a dedicated thread, so that when it's time to destruct the destructors do not block one another!
  auto& mgr = LifetimeManagerSingletonInstance();
  std::thread t(
      [&](ARGS&&... args) {
        size_t const id = [&](ARGS&&... args) {
          T instance(std::forward<ARGS>(args)...);
          // Must ensure the thread registers its lifetime and respects the termination signal.
          size_t const id = mgr.TrackingAdd(text, file, line);
          result.SetValue(&instance);
          mgr.WaitUntilTimeToDie();
          return id;
        }(std::forward<ARGS>(args)...);
        mgr.TrackingRemove(id);
      },
      std::forward<ARGS>(args)...);
  mgr.EmplaceThreadImpl(std::move(t));
  result.Wait();
  return *result.GetValue();
}

#define C5T_LIFETIME_MANAGER_TRACKED_INSTANCE(type, ...) \
  CreateLifetimeTrackedInstance<type>(__FILE__, __LINE__, __VA_ARGS__)

// NOTE(dkorolev): Ensure that the thread body registers its lifetime to the singleton manager,
//                 to eliminate the risk of this thread being `.join()`-ed before it is fully done.
// NOTE(dkorolev): The `ready_to_go` part is essential because otherwise the lambda capture list may not intiailize yet!
// TODO(dkorolev): Why and how so though? I better investigate this deeper before using `std::move`-d lambda captures!

template <typename F, class... ARGS>
void C5T_LIFETIME_MANAGER_TRACKED_THREAD(std::string desc, F&& body, ARGS&&... args) {
  current::WaitableAtomic<bool> ready_to_go(false);
  LifetimeManagerSingletonInstance().EmplaceThreadImpl(std::thread(
      [moved_desc = std::move(desc), moved_body = std::forward<F>(body), &ready_to_go](ARGS&&... args) mutable {
        auto& mgr = LifetimeManagerSingletonInstance();
        size_t const id = mgr.TrackingAdd(moved_desc, __FILE__, __LINE__);
        ready_to_go.SetValue(true);
        moved_body(std::forward<ARGS>(args)...);
        mgr.TrackingRemove(id);
      },
      std::forward<ARGS>(args)...));
  ready_to_go.Wait();
}

// NOTE(dkorolev): `C5T_LIFETIME_MANAGER_TRACKED_POPEN2()` extends the "vanilla" `C5T_POPEN2()` in two ways.
//
// 1) The user provides the "display name" for the inner graceful "task manager" to report what is running, and
// 2) The lifetime managers takes the liberty to send SIGTERM to the child process once termination is initated.
//
// It is still up to the user to exit from the callback function that can `write()` into the child process.
// The user can, of course, send SIGTERM to the child process via the "native" `C5T_POPEN2()`-provided means.
// It is guaranteed that SIGTERM will only be sent to the child process once.
//
// NOTE(dkorolev): This `T_POPEN2_RUNTIME` is not a useful template type per se, it is only here to ensure
//                 that the function is not compiled until used. This way, if `C5T/popen` is neither included
//                 nor used, there are no build warnings/errors whatsoever.
template <class T_POPEN2_RUNTIME>
inline void C5T_LIFETIME_MANAGER_TRACKED_POPEN2_IMPL(
    std::string const& text,
    char const* file,
    size_t line,
    std::vector<std::string> const& cmdline,
    std::function<void(std::string)> cb_stdout_line,
    std::function<void(T_POPEN2_RUNTIME&)> cb_user_code = [](T_POPEN2_RUNTIME&) {},
    std::function<void(std::string)> cb_stderr_line = [](std::string) {},
    std::vector<std::string> const& env = {}) {
  auto& mgr = LifetimeManagerSingletonInstance();
  size_t const id = mgr.TrackingAdd(text, file, line);
  std::shared_ptr<std::atomic_bool> popen2_done = std::make_shared<std::atomic_bool>(false);
  C5T_POPEN2(
      cmdline,
      cb_stdout_line,
      [copy_popen_done = popen2_done, &mgr, moved_cb_user_code = std::move(cb_user_code)](T_POPEN2_RUNTIME& ctx) {
        // NOTE(dkorolev): On `C5T_POPEN2()` level it's OK to call `.Kill()` multiple times, only one will go through.
        // This code is still staying on the safe side =)
        auto const scope =
            mgr.SubscribeToTerminationEvent([&ctx, &mgr, captured_popen_done = std::move(copy_popen_done)]() {
              if (!captured_popen_done->load()) {
                ctx.Kill();
              }
            });
        moved_cb_user_code(ctx);
      },
      cb_stderr_line,
      env);
  popen2_done->store(true);
  mgr.TrackingRemove(id);
}

#define C5T_LIFETIME_MANAGER_TRACKED_POPEN2(text, ...) \
  C5T_LIFETIME_MANAGER_TRACKED_POPEN2_IMPL<Popen2Runtime>(text, __FILE__, __LINE__, __VA_ARGS__)

inline std::string ProvidedStringOrLifetimeManager(std::string s = "C5T_LIFETIME_MGR") { return s; }

#define C5T_LIFETIME_MANAGER_USE_C5T_LOGGER(...)                                    \
  do {                                                                              \
    std::string const title = ProvidedStringOrLifetimeManager(__VA_ARGS__);         \
    C5T_LIFETIME_MANAGER_SET_LOGGER([copy_of_title = title](std::string const& s) { \
      std::cerr << copy_of_title << ": " << s << std::endl;                         \
      C5T_LOGGER(copy_of_title) << s;                                               \
    });                                                                             \
  } while (false)
