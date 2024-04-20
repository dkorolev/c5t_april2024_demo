#include "lib_build_info.h"
#include "current_build_info.h"

std::string const& GitCommit() {
  static std::string const s = current::build::cmake::kGitCommit;
  return s;
}
