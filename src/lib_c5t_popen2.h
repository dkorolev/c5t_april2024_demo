#pragma once

#include <vector>
#include <string>
#include <functional>

struct Popen2Runtime {
  virtual ~Popen2Runtime() = default;
  virtual void operator()(std::string const& string_to_write) = 0;
  virtual void Kill() = 0;
  // TODO(dkorolev): Add `.Close()` as well? And test it?
};

void C5T_POPEN2(
    std::vector<std::string> const& cmdline,
    std::function<void(std::string)> cb_stdout_line,
    std::function<void(Popen2Runtime&)> cb_user_code = [](Popen2Runtime&) {},
    std::function<void(std::string)> cb_stderr_line = [](std::string const&) {},
    std::vector<std::string> const& env = {});
