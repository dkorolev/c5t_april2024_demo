#include "lib_c5t_actor_model.h"
#include "bricks/util/singleton.h"

class TopicIDGenerator final {
 private:
  std::atomic_uint64_t next_topic_id = 0ull;

 public:
  TopicID GetNextUniqueTopicID() { return static_cast<TopicID>(next_topic_id++); }
};

TopicID GetNextUniqueTopicID() { return current::Singleton<TopicIDGenerator>().GetNextUniqueTopicID(); }

class TopicsSubcribersPerTypeSingleton final : public ICleanupAndLinkAndPublish {
 private:
  std::type_index const type_index_;
  std::atomic_uint64_t ids_used_;
  std::mutex mutex_;

  std::unordered_map<EventsSubscriberID, std::unordered_set<TopicID>> s_;
  std::unordered_map<TopicID,
                     std::unordered_map<EventsSubscriberID, std::function<void(std::shared_ptr<crnt::CurrentSuper>)>>>
      m2_;

 public:
  TopicsSubcribersPerTypeSingleton(std::type_index t) : type_index_(t), ids_used_(0ull) {}

  EventsSubscriberID AllocateNextID() { return static_cast<EventsSubscriberID>(++ids_used_); }

  void AddGenericLink(EventsSubscriberID sid,
                      TopicID tid,
                      std::function<void(std::shared_ptr<crnt::CurrentSuper>)> f) override {
    TMP_ActorModelSingleton().InternalRegisterTypeForSubscriber(type_index_, sid, *this);
    std::lock_guard lock(mutex_);
    s_[sid].insert(tid);
    m2_[tid][sid] = std::move(f);
  }

  void CleanupSubscriberByID(EventsSubscriberID sid) override {
    std::lock_guard lock(mutex_);
    for (TopicID tid : s_[sid]) {
      m2_[tid].erase(sid);
    }
    s_.erase(sid);
  }

  void PublishGenericEvent(TopicID tid, std::shared_ptr<crnt::CurrentSuper> e2) override {
    std::lock_guard lock(mutex_);
    for (auto const& e : m2_[tid]) {
      // NOTE(dkorolev): This `.second` should just quickly add a `shared_ptr` to the queue.
      // TODO(dkorolev): Maybe make it more explicit from the code, since a lambda is ambiguous.
      e.second(e2);
    }
  }
};

class TopicsSubcribersAllTypesSingleton final : public ICleanupWithBenefits {
 private:
  std::atomic_uint64_t ids_used_;

  std::mutex mutex_;
  std::unordered_map<std::type_index, ICleanup*> cleanups_per_type_;
  std::unordered_map<EventsSubscriberID, std::unordered_set<std::type_index>> types_per_ids_;

  std::unordered_map<std::type_index, std::unique_ptr<ICleanupAndLinkAndPublish>> impls_;

 public:
  TopicsSubcribersAllTypesSingleton() : ids_used_(0ull) {}

  EventsSubscriberID AllocateNextID() override { return static_cast<EventsSubscriberID>(++ids_used_); }

  void InternalRegisterTypeForSubscriber(std::type_index t,
                                         EventsSubscriberID sid,
                                         ICleanup& respective_singleton_instance) override {
    std::lock_guard lock(mutex_);
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

  ICleanupAndLinkAndPublish& HandlerPerType(std::type_index t) override {
    auto& p = impls_[t];
    if (!p) {
      p = std::make_unique<TopicsSubcribersPerTypeSingleton>(t);
    }
    return *p;
  }
};

ICleanupWithBenefits& TMP_ActorModelSingleton() { return current::Singleton<TopicsSubcribersAllTypesSingleton>(); }
