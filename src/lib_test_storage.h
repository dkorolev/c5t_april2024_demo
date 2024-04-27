#pragma once

#include "lib_c5t_storage.h"

#include "typesystem/struct.h"
#include "typesystem/optional.h"

CURRENT_STRUCT(SomeJSON) {
  CURRENT_FIELD(foo, int32_t, 0);
  CURRENT_FIELD(bar, Optional<std::string>);
  SomeJSON& SetFoo(int32_t v) {
    foo = v;
    return *this;
  }
  SomeJSON& SetBar(std::string v) {
    bar = std::move(v);
    return *this;
  }
};

C5T_STORAGE_DECLARE_FIELD(kv1, std::string);
C5T_STORAGE_DECLARE_FIELD(kv2, SomeJSON);
C5T_STORAGE_DECLARE_FIELD(kv3, int32_t);
