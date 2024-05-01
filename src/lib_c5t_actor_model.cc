#include "lib_c5t_actor_model.h"

class TopicsSubcribersAllTypesSingleton final : public ICleanupWithBenefits {
 private:
  std::atomic_uint64_t ids_used_;

  std::mutex mutex_;
  std::unordered_map<std::type_index, ICleanup*> cleanups_per_type_;
  std::unordered_map<EventsSubscriberID, std::unordered_set<std::type_index>> types_per_ids_;

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
};

ICleanupWithBenefits& TMP_ActorModelSingleton() { return current::Singleton<TopicsSubcribersAllTypesSingleton>(); }
