#include "lib_c5t_popen2.h"

#include <atomic>
#include <iostream>

#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/eventfd.h>
#include <poll.h>

#include <string>
#include <thread>
#include <functional>

#include "bricks/strings/group_by_lines.h"

template <typename F>
static void MutableCStyleVectorStringsArg(const std::vector<std::string>& in, F&& f) {
  std::vector<std::vector<char>> mutable_in;
  mutable_in.resize(in.size());
  for (size_t i = 0u; i < in.size(); ++i) {
    mutable_in[i].assign(in[i].c_str(), in[i].c_str() + in[i].length() + 1u);
  }
  std::vector<char*> out;
  for (auto& e : mutable_in) {
    out.push_back(&e[0]);
  }
  out.push_back(nullptr);
  f(&out[0]);
}

inline void CreatePipeOrFail(int r[2]) {
  if (::pipe(r)) {
    std::cerr << "FATAL: " << __LINE__ << std::endl;
    ::abort();
  }
}

void C5T_POPEN2(std::vector<std::string> const& cmdline,
                std::function<void(const std::string&)> cb_stdout_line,
                std::function<void(Popen2Runtime&)> cb_user_code,
                std::function<void(const std::string&)> cb_stderr_line,
                std::vector<std::string> const& env) {
  pid_t pid;
  int pipe_stdin[2];
  int pipe_stdout[2];
  int pipe_stderr[2];

  CreatePipeOrFail(pipe_stdin);
  CreatePipeOrFail(pipe_stdout);
  CreatePipeOrFail(pipe_stderr);

  int const efd_stdout = eventfd(0, 0);
  if (efd_stdout < 0) {
    std::cerr << "FATAL: " << __LINE__ << std::endl;
    ::abort();
  }

  int const efd_stderr = eventfd(0, 0);
  if (efd_stderr < 0) {
    std::cerr << "FATAL: " << __LINE__ << std::endl;
    ::abort();
  }

  pid = fork();
  if (pid < 0) {
    std::cerr << "FATAL: " << __LINE__ << std::endl;
    ::abort();
  }

  if (pid == 0) {
    // Child.
    ::close(pipe_stdin[1]);
    dup2(pipe_stdin[0], 0);
    ::close(pipe_stdout[0]);
    dup2(pipe_stdout[1], 1);
    ::close(pipe_stderr[0]);
    dup2(pipe_stderr[1], 2);

    if (env.empty()) {
      MutableCStyleVectorStringsArg(cmdline, [&](char* const argv[]) {
        int r = execvp(cmdline[0].c_str(), argv);
        std::cerr << "FATAL: " << __LINE__ << " R=" << r << ", errno=" << errno << std::endl;
        perror("execvp");
      });
    } else {
      MutableCStyleVectorStringsArg(cmdline, [&](char* const argv[]) {
        MutableCStyleVectorStringsArg(env, [&](char* const envp[]) {
          int r = execvpe(cmdline[0].c_str(), argv, envp);
          std::cerr << "FATAL: " << __LINE__ << " R=" << r << ", errno=" << errno << std::endl;
          perror("execvpe");
        });
      });
    }
  }

  ::close(pipe_stdin[0]);
  ::close(pipe_stdout[1]);
  ::close(pipe_stderr[1]);

  struct TrivialPopen2Runtime final : Popen2Runtime {
    std::function<void(std::string const&)> write_;
    std::function<void()> kill_;
    void operator()(std::string const& data) override { write_(data); }
    void Kill() override { kill_(); }
  };
  TrivialPopen2Runtime runtime_context;

  std::shared_ptr<std::atomic_bool> already_done = std::make_shared<std::atomic_bool>(false);
  std::thread thread_user_code(
      [copy_already_done_or_killed = already_done, &runtime_context](
          std::function<void(Popen2Runtime&)> cb_code, int write_fd, int pid) {
        runtime_context.write_ = [write_fd](std::string const& s) {
          ssize_t const n = write(write_fd, s.c_str(), s.length());
          if (n < 0 || static_cast<size_t>(n) != s.length()) {
            return false;
          } else {
            return true;
          }
        };
        runtime_context.kill_ = [pid, moved_already_done_or_killed = std::move(copy_already_done_or_killed)]() {
          if (!*moved_already_done_or_killed) {
            *moved_already_done_or_killed = true;
            kill(pid, SIGTERM);
          }
        };
        cb_code(runtime_context);
      },
      std::move(cb_user_code),
      pipe_stdin[1],
      pid);

  auto const SpawnReader = [](std::function<void(const std::string&)> cb_line, int read_fd, int efd) -> std::thread {
    return std::thread(
        [](std::function<void(const std::string&)> cb_line, int read_fd, int efd) {
          struct pollfd fds[2];
          fds[0].fd = read_fd;
          fds[0].events = POLLIN;
          fds[1].fd = efd;
          fds[1].events = POLLIN;

          char buf[1000];

          auto grouper = current::strings::CreateStatefulGroupByLines(std::move(cb_line));

          while (true) {
            ::poll(fds, 2, -1);
            if (fds[0].revents & POLLIN) {
              ssize_t const n = read(read_fd, buf, sizeof(buf) - 1);
              if (n < 0) {
                // NOTE(dkorolev): This may or may not be a major issue.
                // std::cerr << __LINE__ << std::endl;
                break;
              }
              buf[n] = '\0';
              grouper.Feed(buf);
            } else if (fds[1].revents & POLLIN) {
              // NOTE(dkorolev): Termination signaled.
              // std::cerr << __LINE__ << std::endl;
              break;
            }
          }
        },
        cb_line,
        read_fd,
        efd);
  };

  auto thread_reader_stdout = SpawnReader(std::move(cb_stdout_line), pipe_stdout[0], efd_stdout);
  auto thread_reader_stderr = SpawnReader(std::move(cb_stderr_line), pipe_stderr[0], efd_stderr);

  ::waitpid(pid, NULL, 0);
  *already_done = true;

  uint64_t u = 1;
  if (write(efd_stdout, &u, sizeof(uint64_t)) != sizeof(uint64_t)) {
    std::cerr << "FATAL: " << __LINE__ << std::endl;
    ::abort();
  }
  if (write(efd_stderr, &u, sizeof(uint64_t)) != sizeof(uint64_t)) {
    std::cerr << "FATAL: " << __LINE__ << std::endl;
    ::abort();
  }

  thread_user_code.join();
  thread_reader_stdout.join();
  thread_reader_stderr.join();

  ::close(efd_stdout);
  ::close(efd_stderr);

  ::close(pipe_stdin[1]);
  ::close(pipe_stdout[0]);
  ::close(pipe_stderr[0]);
}
