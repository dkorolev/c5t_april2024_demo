#include "lib_c5t_storage.h"
#include "lib_test_storage.h"

#include "typesystem/serialization/json.h"  // IWYU pragma: keep

C5T_STORAGE_DEFINE_FIELD(kv1, std::string, PERSIST_LATEST);
C5T_STORAGE_DEFINE_FIELD(kv2, SomeJSON, PERSIST_LATEST);
C5T_STORAGE_DEFINE_FIELD(kv3, int32_t, DO_NOT_PERSIST);
