#include "lib_demo_actor_model_extra.h"

#include "lib_c5t_lifetime_manager.h"

void StartTimerThread(TopicKey<TimerEvent> topic_timer) {
  C5T_LIFETIME_MANAGER_TRACKED_THREAD("timer", [topic_timer]() {
    int i = 0;
    while (C5T_LIFETIME_MANAGER_SLEEP_FOR(std::chrono::milliseconds(1000))) {
      C5T_EMIT<TimerEvent>(topic_timer, ++i);
    }
  });
}
