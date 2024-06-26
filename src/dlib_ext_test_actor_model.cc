#include "lib_c5t_dlib.h"
#include "lib_c5t_actor_model.h"
#include "lib_test_actor_model.h"

#include <sstream>
#include <string>

inline int magic = 0;

struct DLibTestWorker final {
  bool first = true;
  std::ostringstream& oss;
  DLibTestWorker(std::ostringstream& oss) : oss(oss) { oss.clear(); }
  void OnEvent(Event_DL2TEST const& e) {
    if (first) {
      first = false;
    } else {
      oss << ',';
    }
    oss << e.v;
  }
  void OnBatchDone() {}
  void OnShutdown() {}
};

inline std::ostringstream oss;
inline NullableActorSubscriberScope scope = nullptr;

extern "C" int Smoke42() { return 42; }
extern "C" std::string SmokeOK() { return "OK"; }

extern "C" void InitAndResetEmitterCounter(IDLib& iface, int value) {
  iface.Use<IActorModel>([](IActorModel& am) { C5T_ACTOR_MODEL_INJECT(am.ActorModel()); });
  magic = value;
}

extern "C" void ExternalEmitter(IDLib& iface, TopicID tid) {
  iface.Use<IActorModel>([tid](IActorModel& am) { C5T_EMIT<Event_DL2TEST>(tid, magic++); });
}

extern "C" void ExternalSubscriberCreate(TopicID tid) {
  scope = C5T_SUBSCRIBE<DLibTestWorker>(TopicKey<Event_DL2TEST>::FromID(tid), oss);
}

extern "C" std::string ExternalSubscriberData() { return oss.str(); }

extern "C" void ExternalSubscriberTerminate() { scope = nullptr; }
