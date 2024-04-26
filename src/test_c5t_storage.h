#include "typesystem/struct.h"
#include "typesystem/optional.h"

#include "lib_c5t_storage.h"

C5T_STORAGE_DECLARE(kv1, std::string, PERSIST_LATEST);

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

C5T_STORAGE_DECLARE(kv2, SomeJSON, PERSIST_LATEST);
C5T_STORAGE_DECLARE(kv3, int32_t, DO_NOT_PERSIST);
