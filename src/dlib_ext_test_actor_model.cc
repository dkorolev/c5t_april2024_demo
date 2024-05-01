#include "lib_c5t_dlib.h"
#include "lib_c5t_actor_model.h"
#include "lib_test_actor_model.h"  // IWYU pragma: keep

#include <string>

extern "C" int Smoke42() { return 42; }
extern "C" std::string SmokeOK() { return "OK"; }

inline int magic = 42;

// TODO: reset `magic`!
extern "C" void ExternalEmitter(IDLib& iface, TopicID tid) {
  iface.Use<IActorModel>([tid](IActorModel& am) {
    C5T_ACTOR_MODEL_INJECT(am.ActorModel());
    EmitTo<Event_DL2TEST>(tid, magic++);
  });
}
