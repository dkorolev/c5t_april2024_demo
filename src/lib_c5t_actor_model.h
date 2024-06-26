#pragma once

#include <memory>
#include <thread>
#include <vector>
#include <typeindex>
#include <unordered_set>

// TODO: even more reasons for a `.cc` file!
#include "bricks/sync/waitable_atomic.h"
#include "bricks/sync/owned_borrowed.h"
#include "bricks/util/singleton.h"

#include "typesystem/types.h"  // For `crnt::CurrentSuper`.

enum class TopicID : uint64_t {};
TopicID GetNextUniqueTopicID();

template <class T>
struct TopicKeysOfType {
  std::unordered_set<TopicID> topic_ids_;
  // NOTE(dkorolev): likely unnecessary!
  TopicKeysOfType() = default;
  TopicKeysOfType(TopicKeysOfType const&) = default;
  TopicKeysOfType(TopicKeysOfType&&) = default;
  TopicKeysOfType& operator=(TopicKeysOfType const&) = default;
  TopicKeysOfType& operator=(TopicKeysOfType&&) = default;
};

template <class... TS>
struct TopicKeys;

// When `<T>` is important to pass along, `TopicKey<T>` is easy to use instead of `TopicID`.
// It's always sufficient to use `TopicID`, but then the templated type `<T>` must be provided explicitly.
struct ConstructTopicKey final {};
template <class T>
class TopicKey final {
 private:
  TopicID id_;
  TopicKey() = delete;

 public:
  TopicKey(ConstructTopicKey) : id_(GetNextUniqueTopicID()) {}
  TopicKey(ConstructTopicKey, TopicID id) : id_(id) {}

  static TopicKey FromID(TopicID id) { return TopicKey(ConstructTopicKey(), id); }
  static TopicKey Uninitialized() { return TopicKey(ConstructTopicKey(), static_cast<TopicID>(-1)); }

  void AssignOnce(TopicKey rhs) {
    if (id_ == static_cast<TopicID>(-1) && rhs.id_ != static_cast<TopicID>(-1)) {
      id_ = rhs.id_;
    } else {
      std::cerr << "FATAL: `AssignOnce` is not once." << std::endl;
      ::abort();
    }
  }

  TopicID GetTopicID() const { return id_; }
  operator TopicID() const { return GetTopicID(); }

  TopicKeys<T> operator+() const {
    TopicKeys<T> res;
    res.template Insert<T>(id_);
    return res;
  }

  template <class T2, class ENABLE = std::enable_if_t<std::is_same_v<T2, T>>>
  TopicKeys<T> operator+(TopicKey<T2> t2) const {
    TopicKeys<T> res;
    res.template Insert<T>(id_);
    res.template Insert<T>(t2.GetTopicID());
    return res;
  }

  template <class T2, class ENABLE = std::enable_if_t<!std::is_same_v<T2, T>>>
  TopicKeys<T, T2> operator+(TopicKey<T2> t2) const {
    TopicKeys<T, T2> res;
    res.template Insert<T>(id_);
    res.template Insert<T2>(t2.GetTopicID());
    return res;
  }
};

template <class T>
TopicKey<T> Topic(std::string name = "") {
  // TODO(dkorolev): Tons of things incl. registry, counters, telemetry, etc.
  static_cast<void>(name);
  return TopicKey<T>((ConstructTopicKey()));
}

enum class EventsSubscriberID : uint64_t {};

class ICleanup {
 public:
  virtual ~ICleanup() = default;
  virtual void CleanupSubscriberByID(EventsSubscriberID) = 0;
};

class ICleanupAndLinkAndPublish : public ICleanup {
 public:
  virtual ~ICleanupAndLinkAndPublish() = default;
  virtual void AddGenericLink(EventsSubscriberID sid,
                              TopicID tid,
                              std::function<void(std::shared_ptr<crnt::CurrentSuper>)> f) = 0;
  virtual void PublishGenericEvent(TopicID tid, std::shared_ptr<crnt::CurrentSuper> e2) = 0;
};

class ICanWait {
 public:
  virtual ~ICanWait() = default;
  virtual size_t GetNumQueued() = 0;
  virtual void WaitUntilNumProcessedIsAtLeast(size_t) = 0;
};

class C5T_ACTOR_MODEL_Interface : public ICleanup {
 public:
  virtual void InternalRegisterTypeForSubscriber(std::type_index t,
                                                 EventsSubscriberID sid,
                                                 ICleanup& respective_singleton_instance) = 0;
  virtual EventsSubscriberID AllocateNextID() = 0;
  virtual ICleanupAndLinkAndPublish& HandlerPerType(std::type_index) = 0;
  virtual void DebugWaitForAllTrackedWorkersToComplete() = 0;
  virtual void AddTracker(ICanWait*) = 0;
  virtual void RemoveTracker(ICanWait*) = 0;
};

inline C5T_ACTOR_MODEL_Interface& C5T_ACTOR_MODEL_INSTANCE();

struct ConstructTopicsSubscriberScope final {};
struct ConstructTopicsSubscriberScopeImpl final {};

class ActorSubscriberScopeImpl {
 public:
  virtual ~ActorSubscriberScopeImpl() = default;
};

template <class W>
class ActorSubscriberScopeFor;

struct ActorModelQueue final {
  bool done = false;
  size_t num_queued = 0u;
  size_t num_processed = 0u;
  std::vector<std::function<void()>> fifo;
};

template <class W>
class ActorSubscriberScopeForImpl final : public ActorSubscriberScopeImpl {
 private:
  friend class ActorSubscriberScopeFor<W>;

  struct OfExtendedScope final : ICanWait {
    EventsSubscriberID const unique_id;
    current::WaitableAtomic<ActorModelQueue> wa;
    std::unique_ptr<W> worker;
    std::thread thread;

    OfExtendedScope(EventsSubscriberID id, std::unique_ptr<W> worker)
        : unique_id(id), worker(std::move(worker)), thread([this]() { Thread(); }) {
      C5T_ACTOR_MODEL_INSTANCE().AddTracker(this);
    }

    ~OfExtendedScope() {
      C5T_ACTOR_MODEL_INSTANCE().RemoveTracker(this);
      C5T_ACTOR_MODEL_INSTANCE().CleanupSubscriberByID(unique_id);
      wa.MutableUse([](ActorModelQueue& q) { q.done = true; });
      thread.join();
    }

    void Thread() {
      // NOTE: it's on the user to stop subscriptions if the application is terminating
      while (true) {
        using r_t = std::pair<std::vector<std::function<void()>>, bool>;
        r_t const w = wa.Wait([](ActorModelQueue const& q) { return q.done || !q.fifo.empty(); },
                              [](ActorModelQueue& q) -> r_t {
                                if (q.done) {
                                  return {{}, true};
                                } else {
                                  std::vector<std::function<void()>> foo;
                                  std::swap(q.fifo, foo);
                                  return {foo, false};
                                }
                              });
        if (w.second) {
          worker->OnShutdown();
          break;
        } else {
          for (auto& f : w.first) {
            try {
              f();
              ++wa.MutableScopedAccessor()->num_processed;
            } catch (current::Exception const&) {
              // TODO
            } catch (std::exception const&) {
              // TODO
            }
          }
          worker->OnBatchDone();
        }
      }
    }

    size_t GetNumQueued() override { return wa.ImmutableScopedAccessor()->num_queued; }

    void WaitUntilNumProcessedIsAtLeast(size_t c) override {
      wa.Wait([c](ActorModelQueue const& q) { return q.done || q.num_processed >= c; });
    }
  };

  current::Owned<OfExtendedScope> extended_;

  // Always hidden inside a `unique_ptr<>`, so no copies and no moves.
  ActorSubscriberScopeForImpl() = delete;
  ActorSubscriberScopeForImpl(ActorSubscriberScopeForImpl const&) = delete;
  ActorSubscriberScopeForImpl& operator=(ActorSubscriberScopeForImpl const&) = delete;
  ActorSubscriberScopeForImpl(ActorSubscriberScopeForImpl&&) = delete;
  ActorSubscriberScopeForImpl& operator=(ActorSubscriberScopeForImpl&&) = delete;

 public:
  // TODO: make private, much like `ExtractImpl()` and `GetUniqueID()`.
  template <typename E>
  void EnqueueEvent(std::shared_ptr<E> e) {
    current::Borrowed<OfExtendedScope> borrowed(extended_);
    extended_->wa.MutableUse([b = std::move(borrowed), &e](ActorModelQueue& q) {
      q.fifo.push_back([b2 = std::move(b), e2 = std::move(e)]() { b2->worker->OnEvent(*e2); });
      ++q.num_queued;
    });
  }

  using worker_t = W;

  ActorSubscriberScopeForImpl(ConstructTopicsSubscriberScopeImpl, EventsSubscriberID id, std::unique_ptr<W> worker)
      : extended_(current::MakeOwned<OfExtendedScope>(id, std::move(worker))) {}

  EventsSubscriberID GetUniqueID() const { return extended_->unique_id; }
};

template <class W>
class ActorSubscriberScopeFor final {
 private:
  friend class ActorSubscriberScope;
  friend class NullableActorSubscriberScope;

  ActorSubscriberScopeFor() = delete;
  ActorSubscriberScopeFor(ActorSubscriberScopeFor const&) = delete;
  ActorSubscriberScopeFor& operator=(ActorSubscriberScopeFor const&) = delete;

  std::unique_ptr<ActorSubscriberScopeForImpl<W>> impl_;

 public:
  using worker_t = W;

  ActorSubscriberScopeFor(ConstructTopicsSubscriberScope, std::unique_ptr<W> worker)
      : impl_(std::make_unique<ActorSubscriberScopeForImpl<W>>(
            ConstructTopicsSubscriberScopeImpl(), C5T_ACTOR_MODEL_INSTANCE().AllocateNextID(), std::move(worker))) {}

  ActorSubscriberScopeFor(ActorSubscriberScopeFor&& rhs) = default;

  // TODO: move away, make private & friends again
  ActorSubscriberScopeForImpl<W>& ExtractImpl() { return *impl_; }
};

template <class... TS>
struct SubscribeAllImpl;

template <class T, class... TS>
struct SubscribeAllImpl<T, TS...> {
  template <class SCOPE, class TOPICS>
  static void DoSubscribeAll(SCOPE& scope, TOPICS& topics) {
    std::unordered_set<TopicID> const& ids = static_cast<TopicKeysOfType<T> const&>(topics).topic_ids_;
    for (TopicID tid : ids) {
      ICleanupAndLinkAndPublish& s = C5T_ACTOR_MODEL_INSTANCE().HandlerPerType(std::type_index(typeid(T)));
      s.AddGenericLink(scope.GetUniqueID(), tid, [&scope](std::shared_ptr<crnt::CurrentSuper> e) {
        std::shared_ptr<T> e2(std::dynamic_pointer_cast<T>(std::move(e)));
        if (e2) {
          scope.template EnqueueEvent<T>(std::move(e2));
        } else {
          std::cerr << "FATAL: Event type mismatch." << std::endl;
          ::abort();
        }
      });
    }
    SubscribeAllImpl<TS...>::DoSubscribeAll(scope, topics);
  }
};

template <>
struct SubscribeAllImpl<> final {
  template <class SCOPE, class TOPICS>
  static void DoSubscribeAll(SCOPE&, TOPICS&) {}
};

template <class...>
struct CopyForAllTypesImpl;

template <class... TS>
struct TopicKeys : TopicKeysOfType<std::decay_t<TS>>... {
 public:
  // TODO(dkorolev): re-add this constraint
  template <class T>  //, class ENABLE = std::enable_if_t<std::is_base_of_v<TopicKeysOfType<std::decay<T>>, TopicKeys>>>
  void Insert(TopicID tid) {
    TopicKeysOfType<std::decay_t<T>>::topic_ids_.insert(tid);
  }

  // TODO(dkorolev): TESTS for this!
  template <class T, class ENABLE = std::enable_if_t<std::is_base_of_v<TopicKeysOfType<std::decay_t<T>>, TopicKeys>>>
  TopicKeys<TS...> operator+(TopicKey<T> t) const {
    TopicKeys<TS...> res;
    CopyForAllTypesImpl<std::decay_t<TS>...>::DoCopyForAllTypes(res, *this);
    res.template Insert<std::decay_t<T>>(t.GetTopicID());
    return res;
  }

  template <class T, class ENABLE = std::enable_if_t<!std::is_base_of_v<TopicKeysOfType<std::decay_t<T>>, TopicKeys>>>
  TopicKeys<TS..., std::decay_t<T>> operator+(TopicKey<T> t) const {
    TopicKeys<TS..., std::decay_t<T>> res;
    CopyForAllTypesImpl<std::decay_t<TS>...>::DoCopyForAllTypes(res, *this);
    res.template Insert<std::decay_t<T>>(t.GetTopicID());
    return res;
  }

  template <class W>
  [[nodiscard]] ActorSubscriberScopeFor<W> InternalSubscribeWorkerTo(std::unique_ptr<W> worker) const {
    ActorSubscriberScopeFor<W> res(ConstructTopicsSubscriberScope(), std::move(worker));
    SubscribeAllImpl<TS...>::DoSubscribeAll(res.ExtractImpl(), *this);
    return res;
  }

  template <class W, typename... ARGS>
  [[nodiscard]] ActorSubscriberScopeFor<W> InternalSubscribeTo(ARGS&&... args) const {
    return InternalSubscribeWorkerTo<W>(std::make_unique<W>(std::forward<ARGS>(args)...));
  }
};

// TODO(dkorolev): TESTS for this!
template <>
struct CopyForAllTypesImpl<> {
  template <typename LHS, typename RHS>
  static void DoCopyForAllTypes(LHS&&, RHS&&) {}
};

template <typename X, typename... XS>
struct CopyForAllTypesImpl<X, XS...> {
  template <typename LHS, typename RHS>
  static void DoCopyForAllTypes(LHS&& lhs, RHS&& rhs) {
    static_cast<TopicKeysOfType<X>&>(lhs) = static_cast<TopicKeysOfType<X> const&>(rhs);
    CopyForAllTypesImpl<XS...>::DoCopyForAllTypes(std::forward<LHS>(lhs), std::forward<RHS>(rhs));
  }
};

// A type-erased `ActorSubscriberScopeFor<T>`.
class ActorSubscriberScope final {
 private:
  ActorSubscriberScope() = delete;
  ActorSubscriberScope(ActorSubscriberScope const&) = delete;
  ActorSubscriberScope& operator=(ActorSubscriberScope const&) = delete;

  friend class NullableActorSubscriberScope;
  std::unique_ptr<ActorSubscriberScopeImpl> type_erased_impl_;

 public:
  ActorSubscriberScope(ActorSubscriberScope&&) = default;
  ActorSubscriberScope& operator=(ActorSubscriberScope&&) = default;

  template <class T>
  ActorSubscriberScope(ActorSubscriberScopeFor<T>&& rhs) : type_erased_impl_(std::move(rhs.impl_)) {}

  template <class T>
  ActorSubscriberScope& operator=(ActorSubscriberScopeFor<T>&& rhs) {
    type_erased_impl_ = std::move(rhs.impl);
    return *this;
  }
};

class NullableActorSubscriberScope final {
 private:
  NullableActorSubscriberScope(NullableActorSubscriberScope const&) = delete;
  NullableActorSubscriberScope& operator=(NullableActorSubscriberScope const&) = delete;

  std::unique_ptr<ActorSubscriberScopeImpl> type_erased_impl_;

 public:
  NullableActorSubscriberScope() = default;
  NullableActorSubscriberScope(std::nullptr_t) {}
  NullableActorSubscriberScope(NullableActorSubscriberScope&&) = default;
  NullableActorSubscriberScope& operator=(NullableActorSubscriberScope&&) = default;

  template <class T>
  NullableActorSubscriberScope(ActorSubscriberScopeFor<T>&& rhs) : type_erased_impl_(std::move(rhs.impl_)) {}

  template <class T>
  NullableActorSubscriberScope& operator=(ActorSubscriberScopeFor<T>&& rhs) {
    type_erased_impl_ = std::move(rhs.impl_);
    return *this;
  }

  NullableActorSubscriberScope& operator=(ActorSubscriberScope&& rhs) {
    type_erased_impl_ = std::move(rhs.type_erased_impl_);
    return *this;
  }

  NullableActorSubscriberScope& operator=(std::nullptr_t) {
    type_erased_impl_ = nullptr;
    return *this;
  }

  operator bool() const { return type_erased_impl_ != nullptr; }
};

template <class T, class... ARGS>
void InternalEmitEventTo(TopicID tid, std::shared_ptr<T> event) {
  C5T_ACTOR_MODEL_INSTANCE().HandlerPerType(std::type_index(typeid(T))).PublishGenericEvent(tid, std::move(event));
}

template <class T, class... ARGS>
void C5T_EMIT(TopicID tid, ARGS&&... args) {
  InternalEmitEventTo(tid, std::make_shared<T>(std::forward<ARGS>(args)...));
}

inline void C5T_ACTORS_DEBUG_WAIT_FOR_ALL_EVENTS_TO_PROPAGATE() {
  C5T_ACTOR_MODEL_INSTANCE().DebugWaitForAllTrackedWorkersToComplete();
}

struct ActorModelInjectableInstance final {
  std::atomic<C5T_ACTOR_MODEL_Interface*> p = std::atomic<C5T_ACTOR_MODEL_Interface*>(nullptr);
  C5T_ACTOR_MODEL_Interface& Get() {
    if (!p) {
#ifndef C5T_DLIB
      p = &GetSingleton();
#else
      std::cerr << "FATAL: No actor model.\n";
      __builtin_trap();
#endif
    }
    return *p;
  }
#ifndef C5T_DLIB
  C5T_ACTOR_MODEL_Interface& GetSingleton();
#endif
};

inline C5T_ACTOR_MODEL_Interface& C5T_ACTOR_MODEL_INSTANCE() {
  return current::Singleton<ActorModelInjectableInstance>().Get();
}

inline void C5T_ACTOR_MODEL_INJECT(C5T_ACTOR_MODEL_Interface& injected) {
  current::Singleton<ActorModelInjectableInstance>().p = &injected;
}

template <class W, class TOPICS>
struct C5T_SUBSCRIBE_IMPL;

template <class W, class... TOPICS_TS>
struct C5T_SUBSCRIBE_IMPL<W, TopicKeys<TOPICS_TS...>> final {
  template <typename... ARGS>
  [[nodiscard]] static ActorSubscriberScopeFor<W> DO_C5T_SUBSCRIBE(TopicKeys<TOPICS_TS...> const& topics,
                                                                   ARGS&&... args) {
    return topics.template InternalSubscribeTo<W>(std::forward<ARGS>(args)...);
  }
};

template <class W, class TOPIC_T>
struct C5T_SUBSCRIBE_IMPL<W, TopicKey<TOPIC_T>> final {
  template <typename... ARGS>
  [[nodiscard]] static ActorSubscriberScopeFor<W> DO_C5T_SUBSCRIBE(TopicKey<TOPIC_T> const& topics, ARGS&&... args) {
    return (+topics).template InternalSubscribeTo<W>(std::forward<ARGS>(args)...);
  }
};

template <class W, class TOPICS, typename... ARGS>
[[nodiscard]] ActorSubscriberScopeFor<W> C5T_SUBSCRIBE(TOPICS&& topics, ARGS&&... args) {
  return C5T_SUBSCRIBE_IMPL<W, std::decay_t<TOPICS>>::template DO_C5T_SUBSCRIBE(std::forward<TOPICS>(topics),
                                                                                std::forward<ARGS>(args)...);
}
