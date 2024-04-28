#include <gtest/gtest.h>

#include "bricks/file/file.h"
#include "bricks/strings/split.h"
#include "bricks/strings/join.h"
#include "bricks/time/chrono.h"

#include "lib_c5t_dlib.h"
#include "lib_c5t_storage.h"
#include "lib_test_storage.h"

inline std::string CurrentTestName() { return ::testing::UnitTest::GetInstance()->current_test_info()->name(); }

struct TestStorageDir final {
  std::string const dir;

  operator std::string const&() const { return dir; }

  TestStorageDir() : dir(InitDir()) {}

  static std::string InitDir() {
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
    std::string const dir = *__FILE__ == current::FileSystem::GetPathSeparator() ? "/" + res : res;
    current::FileSystem::MkDir(dir, current::FileSystem::MkDirParameters::Silent);
    return dir;
  }
};

struct InitDLibOnce final {
  InitDLibOnce() {
    std::string const bin_path = []() {
      // NOTE(dkorolev): I didn't find a quick way to get current binary dir and/or argv[0] from under `googletest`.
      std::vector<std::string> path = current::strings::Split(__FILE__, current::FileSystem::GetPathSeparator());
      path.pop_back();
#ifdef NDEBUG
      path.back() = ".current";
#else
      path.back() = ".current_debug";
#endif
      std::string const res = current::strings::Join(path, current::FileSystem::GetPathSeparator());
      return *__FILE__ == current::FileSystem::GetPathSeparator() ? "/" + res : res;
    }();
    C5T_DLIB_SET_BASE_DIR(bin_path);
  }
};

TEST(StorageTest, FieldsList) {
  std::vector<std::string> v;
  C5T_STORAGE_LIST_FIELDS([&v](std::string const& s) { v.push_back(s); });
  EXPECT_EQ("kv1,kv2,kv3", current::strings::Join(v, ','));
}

TEST(StorageTest, SmokeNeedStorage) {
  ASSERT_THROW(C5T_STORAGE(kv1), StorageNotInitializedException);
  ASSERT_THROW(C5T_STORAGE(kv1).Has("nope"), StorageNotInitializedException);
  auto const dir = current::Singleton<TestStorageDir>().dir + '/' + CurrentTestName();
  auto const storage_scope = C5T_STORAGE_CREATE_UNIQUE_INSANCE(dir);
  EXPECT_FALSE(C5T_STORAGE(kv1).Has("nope"));
}

TEST(StorageTest, SmokeMapStringString) {
  auto const dir = current::Singleton<TestStorageDir>().dir + '/' + CurrentTestName();
  auto const storage_scope = C5T_STORAGE_CREATE_UNIQUE_INSANCE(dir);
  // C5T_STORAGE_INJECT(storage_scope);

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

TEST(StorageTest, SmokeMapStringObject) {
  auto const dir = current::Singleton<TestStorageDir>().dir + '/' + CurrentTestName();
  auto const storage_scope = C5T_STORAGE_CREATE_UNIQUE_INSANCE(dir);

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

TEST(StorageTest, SmokeMapPersists) {
  auto const dir1 = current::Singleton<TestStorageDir>().dir + '/' + CurrentTestName() + '1';
  auto const dir2 = current::Singleton<TestStorageDir>().dir + '/' + CurrentTestName() + '2';
  current::FileSystem::MkDir(dir1, current::FileSystem::MkDirParameters::Silent);

  {
    // Step 1/5: Create something and have it persisted.
    auto const storage_scope1 = C5T_STORAGE_CREATE_UNIQUE_INSANCE(dir1);
    // C5T_STORAGE_INJECT(storage_scope1);
    EXPECT_FALSE(C5T_STORAGE(kv1).Has("k"));
    C5T_STORAGE(kv1).Set("k", "v");
  }

  {
    // Step 2/5: Restore from the persisted storage.
    auto const storage_scope2 = C5T_STORAGE_CREATE_UNIQUE_INSANCE(dir1);
    // C5T_STORAGE_INJECT(storage_scope2);
    EXPECT_TRUE(C5T_STORAGE(kv1).Has("k"));
  }

  {
    // Step 3/5: But confirm that the freshly created storage from a different dir is empty.
    auto const storage_scope3 = C5T_STORAGE_CREATE_UNIQUE_INSANCE(dir2);
    // C5T_STORAGE_INJECT(storage_scope3);
    EXPECT_FALSE(C5T_STORAGE(kv1).Has("k"));
  }

  {
    // Step 4/5: Restore from the persisted storage and delete the entry.
    auto const storage_scope4 = C5T_STORAGE_CREATE_UNIQUE_INSANCE(dir1);
    // C5T_STORAGE_INJECT(storage_scope4);
    C5T_STORAGE(kv1).Del("k");
  }

  {
    // Step 5/5: Restore from the persisted storage.
    auto const storage_scope5 = C5T_STORAGE_CREATE_UNIQUE_INSANCE(dir1);
    // C5T_STORAGE_INJECT(storage_scope5);
    EXPECT_FALSE(C5T_STORAGE(kv1).Has("k"));
  }
}

TEST(StorageTest, InjectedFromDLib) {
  current::Singleton<InitDLibOnce>();

  Optional<std::string> const smoke =
      C5T_DLIB_CALL("test_storage", [&](C5T_DLib& dlib) { return dlib.Call<std::string()>("SmokeOK"); });

  EXPECT_TRUE(Exists(smoke));
  EXPECT_EQ("OK", Value(smoke));

  auto const dir = current::Singleton<TestStorageDir>().dir + '/' + CurrentTestName();
  current::FileSystem::MkDir(dir, current::FileSystem::MkDirParameters::Silent);
  auto const storage_scope = C5T_STORAGE_CREATE_UNIQUE_INSANCE(dir);

  // ... EXPECT_FALSE(C5T_STORAGE(kv1).Has("k")); ...
}

// TO TEST:
// [x] do not use the global storage singleton, inject one per test
// [x] persist restore trivial
// [x] persist restore recovering
// [ ] persist move old files around, to not scan through them unnecesarily
// [ ] persist evolve, use `JSONFormat::Minimalistic`
// [ ] persist set logger and errors on failing to evolve
// [x] delete
// [ ] injected storage
