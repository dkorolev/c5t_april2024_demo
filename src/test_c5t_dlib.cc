#include <vector>
#include <string>

#include <gtest/gtest.h>

#include "lib_c5t_dlib.h"
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

TEST(DLibTest, Smoke) {
  current::Singleton<InitDLibOnce>();
  Optional<std::string> s;
  C5T_DLIB_USE("test1", [&](C5T_DLib& dlib) { s = dlib.Call<std::string()>("ShouldReturnOK"); });
  ASSERT_TRUE(Exists(s));
  EXPECT_EQ("OK", Value(s));
}
