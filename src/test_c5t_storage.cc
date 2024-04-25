#include <gtest/gtest.h>

#include "bricks/file/file.h"
#include "bricks/strings/split.h"
#include "bricks/strings/join.h"
#include "bricks/time/chrono.h"

#include "typesystem/struct.h"
#include "typesystem/optional.h"

#include "lib_c5t_storage.h"

inline std::string CurrentTestName() { return ::testing::UnitTest::GetInstance()->current_test_info()->name(); }

struct InitStorageOnce final {
  InitStorageOnce() {
    std::string const storage_base_path = []() {
      // NOTE(dkorolev): I didn't find a quick way to get current binary dir and/or argv[0] from under `googletest`.
      std::vector<std::string> path = current::strings::Split(__FILE__, current::FileSystem::GetPathSeparator());
      path.pop_back();
#ifdef NDEBUG
      path.back() = ".current";
#else
      path.back() = ".current_debug";
#endif
      path.push_back(current::ToString(current::time::Now().count()));
      std::string const res = current::strings::Join(path, current::FileSystem::GetPathSeparator());
      return *__FILE__ == current::FileSystem::GetPathSeparator() ? "/" + res : res;
    }();
    C5T_STORAGE_SET_BASE_DIR(storage_base_path);
  }
};

C5T_STORAGE_DECLARE(kv1, std::string, PERSIST_LATEST);

TEST(StorageTest, SmokeMapStringString) {
  current::Singleton<InitStorageOnce>();

  {
    ASSERT_FALSE(C5T_STORAGE(kv1).Has("k"));
    ASSERT_THROW(C5T_STORAGE(kv1).GetOrThrow("k"), StorageKeyNotFoundException);
    ASSERT_FALSE(Exists(C5T_STORAGE(kv1).Get("k")));
  }

  C5T_STORAGE(kv1).Set("k", "v");

  {
    ASSERT_TRUE(C5T_STORAGE(kv1).Has("k"));
    EXPECT_EQ("v", C5T_STORAGE(kv1).GetOrThrow("k"));

    auto const o = C5T_STORAGE(kv1).Get("k");
    ASSERT_TRUE(Exists(o));
    EXPECT_EQ("v", Value(o));
  }
}

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

TEST(StorageTest, SmokeMapStringObject) {
  current::Singleton<InitStorageOnce>();

  {
    ASSERT_FALSE(C5T_STORAGE(kv2).Has("k"));
    ASSERT_THROW(C5T_STORAGE(kv2).GetOrThrow("k"), StorageKeyNotFoundException);
    ASSERT_FALSE(Exists(C5T_STORAGE(kv2).Get("k")));
  }

  C5T_STORAGE(kv2).Set("k", SomeJSON().SetFoo(42).SetBar("bar"));

  {
    ASSERT_TRUE(C5T_STORAGE(kv2).Has("k"));

    {
      auto const e = C5T_STORAGE(kv2).GetOrThrow("k");
      EXPECT_EQ(42, e.foo);
      ASSERT_TRUE(Exists(e.bar));
      EXPECT_EQ("bar", Value(e.bar));
    }

    {
      auto const o = C5T_STORAGE(kv2).Get("k");
      ASSERT_TRUE(Exists(o));
      auto const& e = Value(o);
      EXPECT_EQ(42, e.foo);
      ASSERT_TRUE(Exists(e.bar));
      EXPECT_EQ("bar", Value(e.bar));
    }
  }
}

// TO TEST:
// [ ] do not use the global storage singleton, inject one per test
// [ ] persist restore trivial
// [ ] persist restore recovering
// [ ] persist move old files around, to not scan through them unnecesarily
// [ ] persist evolve, use `JSONFormat::Minimalistic`
// [ ] persist set logger and errors on failing to evolve
