#pragma once

#include "lib_c5t_actor_model.h"
#include "typesystem/types.h"

struct TimerEvent final : crnt::CurrentSuper {
  uint32_t const i;

  TimerEvent() = delete;
  TimerEvent(TimerEvent const&) = delete;
  TimerEvent& operator=(TimerEvent const&) = delete;

  TimerEvent(uint32_t i) : i(i) {}
};

struct InputEvent final : crnt::CurrentSuper {
  std::string const s;

  InputEvent() = delete;
  InputEvent(InputEvent const&) = delete;
  InputEvent& operator=(InputEvent const&) = delete;

  InputEvent(std::string s) : s(std::move(s)) {}
};

void StartTimerThread(TopicKey<TimerEvent> topic_timer);
