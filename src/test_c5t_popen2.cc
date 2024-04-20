#include <vector>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "lib_c5t_popen2.h"
#include "bricks/strings/util.h"
#include "bricks/sync/waitable_atomic.h"

TEST(Popen2Test, Smoke) {
  std::string result;
  C5T_POPEN2({"/usr/bin/bash", "-c", "echo PASS"}, [&result](std::string const& line) { result = line; });
  EXPECT_EQ(result, "PASS");
}

TEST(Popen2Test, WithDelay) {
  std::string result;
  C5T_POPEN2({"/usr/bin/bash", "-c", "sleep 0.01; echo PASS2"}, [&result](std::string const& line) { result = line; });
  EXPECT_EQ(result, "PASS2");
}

TEST(Popen2Test, InnerBashBecauseParentheses) {
  std::string result;
  C5T_POPEN2({"/usr/bin/bash", "-c", "(sleep 0.01; echo PASS3)"},
             [&result](std::string const& line) { result = line; });
  EXPECT_EQ(result, "PASS3");
}

TEST(Popen2Test, ThreePrints) {
  std::string result;
  C5T_POPEN2({"/usr/bin/bash", "-c", "echo ONE; sleep 0.01; echo TWO; sleep 0.01; echo THREE"},
             [&result](std::string const& line) { result += line + ' '; });
  ASSERT_EQ(result, "ONE TWO THREE ");
}

TEST(Popen2Test, KillsSuccessfully) {
  bool nope = false;
  C5T_POPEN2(
      {"/usr/bin/bash", "-c", "sleep 10; echo NOPE"},
      [&nope](std::string const&) { nope = true; },
      [](Popen2Runtime& run) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        run.Kill();
      });
  ASSERT_TRUE(!nope);
}

TEST(Popen2Test, ReadsStdin) {
  std::string c;
  C5T_POPEN2(
      {"/usr/bin/bash", "-c", "read A; read B; echo $((A+B))"},
      [&c](std::string const& line) { c = line; },
      [](Popen2Runtime& run) { run("1\n2\n"); });
  ASSERT_EQ(c, "3");
}

TEST(Popen2Test, ReadsStdinForever) {
  std::string result = "result:";
  C5T_POPEN2(
      {"/usr/bin/bash",
       "-c",
       "while true; do read A; read B; C=$((A+B)); if [ $C == '0' ]; then exit; fi; echo $C; done"},
      [&result](std::string const& line) { result += ' ' + line; },
      [](Popen2Runtime& run) { run("1\n2\n3\n4\n0\n0\n"); });
  ASSERT_EQ(result, "result: 3 7");
}

TEST(Popen2Test, MultipleOutputLines) {
  std::string result = "result:";
  C5T_POPEN2({"/usr/bin/bash", "-c", "seq 10"}, [&result](std::string const& line) { result += ' ' + line; });
  ASSERT_EQ(result, "result: 1 2 3 4 5 6 7 8 9 10");
}

TEST(Popen2Test, MultipleOutputLinesWithMath) {
  std::string result = "result:";
  C5T_POPEN2({"/usr/bin/bash", "-c", "for i in $(seq 3 7) ; do echo $((i * i)) ; done"},
             [&result](std::string const& line) { result += ' ' + line; });
  ASSERT_EQ(result, "result: 9 16 25 36 49");
}

TEST(Popen2Test, ReadsStdinAndCanBeKilled) {
  struct Ctx final {
    std::vector<int> sums;
    bool active = true;
    operator bool() const { return !active; };
  };
  current::WaitableAtomic<Ctx> ctx;
  std::thread t([&ctx]() {
    C5T_POPEN2(
        {"/usr/bin/bash", "-c", "while true; do read A; read B; C=$((A+B)); echo $C; done"},
        [&ctx](std::string const& line) {
          ctx.MutableScopedAccessor()->sums.push_back(current::FromString<int>(line));
        },
        [&ctx](Popen2Runtime& run) {
          int i = 0;
          while (true) {
            run(current::ToString(++i) + '\n');
            // NOTE(dkorolev): Probably change the return type of `WaitFor` to make it clear whether stuff timed out.
            if (ctx.WaitFor(std::chrono::milliseconds(3))) {
              // Call `.Kill()` from the runtime context once `ctx.active` is set to `false` from the test code below.
              run.Kill();
              break;
            }
          }
        });
  });
  std::vector<int> sums;
  ctx.Wait([](Ctx const& ctx) { return ctx.sums.size() >= 3; },
           [&sums](Ctx& ctx) {
             sums = std::move(ctx.sums);
             // Signal the user code running from within `C5T_POPEN2()` that it's time to `.Kill()` the process.
             ctx.active = false;
           });
  EXPECT_GE(sums.size(), 3u);
  EXPECT_EQ(3, sums[0]);
  EXPECT_EQ(7, sums[1]);
  EXPECT_EQ(11, sums[2]);
  t.join();
}

TEST(Popen2Test, Stderr) {
  std::string text_stdout;
  std::string text_stderr;
  C5T_POPEN2(
      {"/usr/bin/bash", "-c", "echo out; echo err >/dev/stderr"},
      [&text_stdout](std::string const& s) { text_stdout = s; },
      [](Popen2Runtime&) {},
      [&text_stderr](std::string const& s) { text_stderr = s; });
  EXPECT_EQ("out", text_stdout);
  EXPECT_EQ("err", text_stderr);
}

TEST(Popen2Test, Env) {
  std::string result;
  C5T_POPEN2(
      {"/usr/bin/bash", "-c", "echo $FOO"},
      [&result](std::string const& s) { result = s; },
      [](Popen2Runtime&) {},
      [](std::string const& unused_stderr) { static_cast<void>(unused_stderr); },
      {"FOO=bar"});
  ASSERT_TRUE(result == "bar");
}
