#pragma once

// NOTE(dkorolev): This code is super ugly, but that's what we have today.
// TODO(dkorolev): Introduce a `lib_*.cc` file!

#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>

#include "lib_c5t_lifetime_manager.h"

// TODO: even more reasons for a `.cc` file!
#include "bricks/sync/owned_borrowed.h"

#include "typesystem/types.h"  // For `crnt::CurrentSuper`.

enum class TopicID : uint64_t {};
class TopicIDGenerator final {
 private:
  std::atomic_uint64_t next_topic_id = 0ull;

 public:
  TopicID GetNextUniqueTopicID() { return static_cast<TopicID>(next_topic_id++); }
};

template <class T>
struct TopicKeysOfType {
  std::unordered_set<TopicID> topic_ids_;
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
  TopicKey(ConstructTopicKey) : id_(current::Singleton<TopicIDGenerator>().GetNextUniqueTopicID()) {}
  TopicID GetTopicID() const { return id_; }
  operator TopicID() const { return GetTopicID(); }

  TopicKeys<T> operator+() const {
    TopicKeys<T> res;
    res.template Insert<T>(id_);
    return res;
  }

  template <class T2, class = std::enable_if_t<std::is_same_v<T2, T>>>
  TopicKeys<T> operator+(TopicKey<T2> t2) const {
    TopicKeys<T> res;
    res.template Insert<T>(id_);
    res.template Insert<T>(t2.GetTopicID());
    return res;
  }

  template <class T2, class = std::enable_if_t<!std::is_same_v<T2, T>>>
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

class TopicsSubcribersAllTypesSingleton final : public ICleanup {
 private:
  std::atomic_uint64_t ids_used_;

  std::mutex mutex_;
  std::unordered_map<std::type_index, ICleanup*> cleanups_per_type_;
  std::unordered_map<EventsSubscriberID, std::unordered_set<std::type_index>> types_per_ids_;

 public:
  TopicsSubcribersAllTypesSingleton() : ids_used_(0ull) {}

  EventsSubscriberID AllocateNextID() { return static_cast<EventsSubscriberID>(++ids_used_); }

  template <typename T>
  void InternalRegisterTypeForSubscriber(EventsSubscriberID sid, ICleanup& respective_singleton_instance) {
    std::lock_guard lock(mutex_);
    auto t = std::type_index(typeid(T));
    auto& p = cleanups_per_type_[t];
    if (!p) {
      p = &respective_singleton_instance;
    }
    types_per_ids_[sid].insert(t);
  }

  void CleanupSubscriberByID(EventsSubscriberID sid) override {
    std::lock_guard lock(mutex_);
    for (auto& e : types_per_ids_[sid]) {
      cleanups_per_type_[e]->CleanupSubscriberByID(sid);
    }
  }
};

template <typename T>
class TopicsSubcribersPerTypeSingleton final : public ICleanupAndLinkAndPublish {
 private:
  std::atomic_uint64_t ids_used_;
  std::mutex mutex_;

  std::unordered_map<EventsSubscriberID, std::unordered_set<TopicID>> s_;
  std::unordered_map<TopicID, std::unordered_map<EventsSubscriberID, std::function<void(std::shared_ptr<T>)>>> m1_;
  std::unordered_map<TopicID,
                     std::unordered_map<EventsSubscriberID, std::function<void(std::shared_ptr<crnt::CurrentSuper>)>>>
      m2_;

 public:
  TopicsSubcribersPerTypeSingleton() : ids_used_(0ull) {}

  EventsSubscriberID AllocateNextID() { return static_cast<EventsSubscriberID>(++ids_used_); }

  void AddLink(EventsSubscriberID sid, TopicID tid, std::function<void(std::shared_ptr<T>)> f) {
    std::lock_guard lock(mutex_);
    s_[sid].insert(tid);
    m1_[tid][sid] = std::move(f);
  }

  void AddGenericLink(EventsSubscriberID sid,
                      TopicID tid,
                      std::function<void(std::shared_ptr<crnt::CurrentSuper>)> f) override {
    current::Singleton<TopicsSubcribersAllTypesSingleton>().InternalRegisterTypeForSubscriber<T>(sid, *this);
    std::lock_guard lock(mutex_);
    s_[sid].insert(tid);
    m2_[tid][sid] = std::move(f);
  }

  void CleanupSubscriberByID(EventsSubscriberID sid) override {
    std::lock_guard lock(mutex_);
    for (TopicID tid : s_[sid]) {
      m1_[tid].erase(sid);
      m2_[tid].erase(sid);
    }
    s_.erase(sid);
  }

  void PublishEvent(TopicID tid, std::shared_ptr<T> event) {
    std::lock_guard lock(mutex_);
    for (auto const& e : m1_[tid]) {
      // NOTE(dkorolev): This `.second` should just quickly add a `shared_ptr` to the queue.
      // TODO(dkorolev): Maybe make it more explicit from the code, since a lambda is ambiguous.
      e.second(event);
    }
    auto e2 = std::dynamic_pointer_cast<crnt::CurrentSuper>(event);
    if (!e2) {
      std::cerr << "FATAL: Event type mismatch." << std::endl;
      ::abort();
    }
    for (auto const& e : m2_[tid]) {
      // NOTE(dkorolev): This `.second` should just quickly add a `shared_ptr` to the queue.
      // TODO(dkorolev): Maybe make it more explicit from the code, since a lambda is ambiguous.
      e.second(e2);
    }
  }

  void PublishGenericEvent(TopicID tid, std::shared_ptr<crnt::CurrentSuper> e2) override {
    auto event = std::dynamic_pointer_cast<T>(e2);
    if (!e2) {
      std::cerr << "FATAL: Event type mismatch." << std::endl;
      ::abort();
    }
    std::lock_guard lock(mutex_);
    for (auto const& e : m1_[tid]) {
      // NOTE(dkorolev): This `.second` should just quickly add a `shared_ptr` to the queue.
      // TODO(dkorolev): Maybe make it more explicit from the code, since a lambda is ambiguous.
      e.second(event);
    }
    for (auto const& e : m2_[tid]) {
      // NOTE(dkorolev): This `.second` should just quickly add a `shared_ptr` to the queue.
      // TODO(dkorolev): Maybe make it more explicit from the code, since a lambda is ambiguous.
      e.second(e2);
    }
  }
};

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
  std::vector<std::function<void()>> fifo;
};

struct ActorModelQueuesDebugWaitSingleton final {
  std::unordered_set<current::WaitableAtomic<ActorModelQueue>*> queues;
};

template <class W>
class ActorSubscriberScopeForImpl final : public ActorSubscriberScopeImpl {
 private:
  friend class ActorSubscriberScopeFor<W>;

  struct OfExtendedScope final {
    EventsSubscriberID const unique_id;
    current::WaitableAtomic<ActorModelQueue> wa;
    std::unique_ptr<W> worker;
    std::thread thread;

    OfExtendedScope(EventsSubscriberID id, std::unique_ptr<W> worker)
        : unique_id(id), worker(std::move(worker)), thread([this]() { Thread(); }) {
      current::Singleton<ActorModelQueuesDebugWaitSingleton>().queues.insert(&wa);
    }

    ~OfExtendedScope() {
      current::Singleton<ActorModelQueuesDebugWaitSingleton>().queues.erase(&wa);
      current::Singleton<TopicsSubcribersAllTypesSingleton>().CleanupSubscriberByID(unique_id);
      wa.MutableUse([](ActorModelQueue& q) { q.done = true; });
      thread.join();
    }

    void Thread() {
      auto const scope_term = C5T_LIFETIME_MANAGER_NOTIFY_OF_SHUTDOWN(
          [this]() { wa.MutableUse([](ActorModelQueue& q) { q.done = true; }); });
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
            ConstructTopicsSubscriberScopeImpl(),
            current::Singleton<TopicsSubcribersAllTypesSingleton>().AllocateNextID(),
            std::move(worker))) {}

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
    std::unordered_set<TopicID> const& ids = static_cast<TopicKeysOfType<T>&>(topics).topic_ids_;
    for (TopicID tid : ids) {
      ICleanupAndLinkAndPublish& s = current::Singleton<TopicsSubcribersPerTypeSingleton<T>>();
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

template <class... TS>
struct TopicKeys : TopicKeysOfType<TS>... {
 public:
  template <class T, class = std::enable_if_t<std::is_base_of_v<TopicKeysOfType<T>, TopicKeys>>>
  void Insert(TopicID tid) {
    TopicKeysOfType<T>::topic_ids_.insert(tid);
  }

  template <class T, class = std::enable_if_t<std::is_base_of_v<TopicKeysOfType<T>, TopicKeys>>>
  TopicKeys<TS...> operator+(TopicKey<T> t) const {
    TopicKeys<T> res = *this;
    res.template Insert<T>(t.id_);
    return res;
  }

  template <class T, class = std::enable_if_t<!std::is_base_of_v<TopicKeysOfType<T>, TopicKeys>>>
  TopicKeys<TS..., T> operator+(TopicKey<T> t) const {
    TopicKeys<TS..., T> res = *this;
    res.template Insert<T>(t.id_);
    return res;
  }

  template <class W>
  [[nodiscard]] ActorSubscriberScopeFor<W> NewSubscribeWorkerTo(std::unique_ptr<W> worker) {
    ActorSubscriberScopeFor<W> res(ConstructTopicsSubscriberScope(), std::move(worker));
    SubscribeAllImpl<TS...>::DoSubscribeAll(res.ExtractImpl(), *this);
    return res;
  }

  template <class W, typename... ARGS>
  [[nodiscard]] ActorSubscriberScopeFor<W> NewSubscribeTo(ARGS&&... args) {
    return NewSubscribeWorkerTo<W>(std::make_unique<W>(std::forward<ARGS>(args)...));
  }
};

// A type-erased `ActorSubscriberScopeFor<T>`.
class ActorSubscriberScope final {
 private:
  ActorSubscriberScope() = delete;
  ActorSubscriberScope(ActorSubscriberScope const&) = delete;
  ActorSubscriberScope& operator=(ActorSubscriberScope const&) = delete;

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
  NullableActorSubscriberScope(NullableActorSubscriberScope&&) = default;
  NullableActorSubscriberScope& operator=(NullableActorSubscriberScope&&) = default;

  template <class T>
  NullableActorSubscriberScope(ActorSubscriberScopeFor<T>&& rhs) : type_erased_impl_(std::move(rhs.impl_)) {}

  template <class T>
  NullableActorSubscriberScope& operator=(ActorSubscriberScopeFor<T>&& rhs) {
    type_erased_impl_ = std::move(rhs.impl_);
    return *this;
  }

  NullableActorSubscriberScope& operator=(std::nullptr_t) {
    type_erased_impl_ = nullptr;
    return *this;
  }
};

template <class T, class... ARGS>
void EmitEventTo(TopicID tid, std::shared_ptr<T> event) {
  ICleanupAndLinkAndPublish& s = current::Singleton<TopicsSubcribersPerTypeSingleton<T>>();
  s.PublishGenericEvent(tid, std::move(event));
}

template <class T, class... ARGS>
void EmitTo(TopicID tid, ARGS&&... args) {
  EmitEventTo(tid, std::make_shared<T>(std::forward<ARGS>(args)...));
}

#ifdef C5T_ACTOR_MODEL_ENABLE_TESTING
inline void C5T_ACTORS_DEBUG_WAIT_FOR_ALL_EVENTS_TO_PROPAGATE() {
  // TODO: This is ugly, slow, and not safe. Guard this by an `#ifdef`, to begin with, and lock with a mutex.
  std::vector<current::WaitableAtomic<ActorModelQueue>*> qs;
  for (auto p : current::Singleton<ActorModelQueuesDebugWaitSingleton>().queues) {
    qs.push_back(p);
  }
  for (auto p : qs) {
    p->Wait([](ActorModelQueue const& q) { return q.done || q.fifo.empty(); });
  }
}
#endif  // C5T_ACTOR_MODEL_ENABLE_TESTING
