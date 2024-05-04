#pragma once

#include "typesystem/types.h"

struct Event_DL2TEST final : crnt::CurrentSuper {
  int v;
  Event_DL2TEST(int v) : v(v) {}
};
