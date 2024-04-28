#include <vector>
#include <string>

#include <gtest/gtest.h>

#include "test_c5t_dlib.h"
#include "bricks/file/file.h"
#include "bricks/strings/split.h"
#include "bricks/strings/join.h"

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

TEST(DLibTest, Test1_Smoke) {
  current::Singleton<InitDLibOnce>();

  std::string const s1 =
      C5T_DLIB_CALL("test1", [&](C5T_DLib& dlib) { return dlib.CallOrDefault<std::string()>("ShouldReturnOK"); });
  EXPECT_EQ("OK", s1);

  Optional<std::string> const s2 = C5T_DLIB_CALL(
      "test1", [&](C5T_DLib& dlib) { return dlib.CallReturningOptional<std::string()>("NoSuchFunction"); });
  ASSERT_FALSE(Exists(s2));

  Optional<std::string> const s3 = C5T_DLIB_CALL(
      "no_such_lib", [&](C5T_DLib& dlib) { return dlib.CallReturningOptional<std::string()>("ShouldReturnOK"); });
  ASSERT_FALSE(Exists(s3));

  std::string const s4 = C5T_DLIB_CALL(
      "no_such_lib",
      [&](C5T_DLib& dlib) { return dlib.CallOrDefault<std::string()>("ShouldReturnOK"); },
      []() { return "HA!"; });
  EXPECT_EQ("HA!", s4);
}

TEST(DLibTest, Test2_UseInterface_WithoutFoo) {
  struct ImplWithoutFoo : IBar {
    std::string bar;
    void BarCalledFromDLib(std::string const& s) override { bar = s; }
  };
  ImplWithoutFoo impl;

  current::Singleton<InitDLibOnce>();
  EXPECT_EQ("", impl.bar);
  ASSERT_TRUE(C5T_DLIB_CALL("test2", [&](C5T_DLib& dlib) { return dlib.CallVoid<void(IDLib&)>("UsesIFooBar", impl); }));
  EXPECT_EQ("PASS BAR, FOO UNAVAILABLE", impl.bar);
}

TEST(DLibTest, Test2_UseInterface_WithFoo) {
  struct ImplWithFoo : IFoo, IBar {
    std::string foo;
    std::string bar;
    int forty_two = 42;
    int FooCalledFromDLib(std::string const& s) override {
      foo = s;
      return forty_two++;
    }
    void BarCalledFromDLib(std::string const& s) override { bar = s; }
  };
  ImplWithFoo impl;

  current::Singleton<InitDLibOnce>();
  EXPECT_EQ("", impl.foo);
  EXPECT_EQ("", impl.bar);
  ASSERT_TRUE(C5T_DLIB_USE("test2", [&](C5T_DLib& dlib) { dlib.CallVoid<void(IDLib&)>("UsesIFooBar", impl); }));
  EXPECT_EQ("PASS FOO", impl.foo);
  EXPECT_EQ("PASS BAR, FOO=42", impl.bar);
  ASSERT_TRUE(C5T_DLIB_USE("test2", [&](C5T_DLib& dlib) { dlib.CallVoid<void(IDLib&)>("UsesIFooBar", impl); }));
  EXPECT_EQ("PASS FOO", impl.foo);
  EXPECT_EQ("PASS BAR, FOO=43", impl.bar);
}

// Remains to test:
// [x] interfaces to dlib
// [ ] interfaces from dlib
// [ ] dummy onloading/reloading
// [ ] clever unloading/reloading
