#include "blocks/http/api.h"
#include "bricks/dflags/dflags.h"
#include "bricks/file/file.h"
#include "bricks/strings/join.h"
#include "bricks/strings/split.h"
#include "bricks/sync/waitable_atomic.h"
#include "lib_c5t_lifetime_manager.h"
#include "lib_c5t_popen2.h"  // IWYU pragma: keep
#include "lib_build_info.h"

inline std::string BasePathOf(std::string const& s) {
  std::vector<std::string> parts = current::strings::Split(s, current::FileSystem::GetPathSeparator());
  parts.pop_back();
  std::string const res = current::strings::Join(parts, current::FileSystem::GetPathSeparator());
  return s[0] == current::FileSystem::GetPathSeparator() ? "/" + res : res;
}

constexpr static char const* const kPythonSourceFile = "python_wss.py";
constexpr static char const* const kHtmlSourceFile = "python_wss.html";

DEFINE_uint16(port, 5555, "");
DEFINE_string(python_src, BasePathOf(__FILE__) + "/" + kPythonSourceFile, "");
DEFINE_string(html_src, BasePathOf(__FILE__) + "/" + kHtmlSourceFile, "");

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  try {
    if (current::FileSystem::GetFileSize(FLAGS_python_src) == 0u) {
      std::cout << "the python source file appears to be empty" << std::endl;
      std::exit(1);
    }
  } catch (const current::Exception& e) {
    std::cout << "the python source file appears to be missing" << std::endl;
    std::exit(1);
  }

  std::string const html = []() {
    try {
      std::string body = current::FileSystem::ReadFileAsString(FLAGS_html_src);
      if (body.empty()) {
        std::cout << "the html source file appears to be empty" << std::endl;
        std::exit(1);
      }
      return body;
    } catch (const current::Exception& e) {
      std::cout << "the html source file appears to be missing" << std::endl;
      std::exit(1);
    }
  }();

  current::WaitableAtomic<bool> time_to_stop_http_server_and_die(false);

  auto& http = []() -> current::http::HTTPServerPOSIX& {
    uint64_t const port_number = FLAGS_port;
    try {
      auto hold_port = current::net::ReservedLocalPort(
          current::net::ReservedLocalPort::Construct(),
          port_number,
          current::net::SocketHandle(current::net::SocketHandle::BindAndListen(), current::net::BarePort(port_number)));
      return HTTP(std::move(hold_port));
    } catch (const current::Exception& e) {
      std::cout << "port " << port_number << " appears to be taken" << std::endl;
      std::exit(1);
    }
  }();

  auto const start_time = current::time::Now();
  auto routes = http.Register("/up", [start_time, &time_to_stop_http_server_and_die](Request r) {
    r(current::strings::Printf("up %0.lfs\n%s",
                               1e-6 * (current::time::Now() - start_time).count(),
                               time_to_stop_http_server_and_die.GetValue() ? "IN TERMINATION SEQUENCE\n" : ""));
  });

  routes += http.Register(
      "/", [&html](Request r) { r(html, HTTPResponseCode.OK, current::net::constants::kDefaultHTMLContentType); });

  routes += http.Register("/stop", [&time_to_stop_http_server_and_die](Request r) {
    r("stopping\n",
      HTTPResponseCode.Found,
      current::net::http::Headers({{"Location", "/up?from=stop"}, {"Cache-Control", "no-store, must-revalidate"}}),
      current::net::constants::kDefaultHTMLContentType);
    time_to_stop_http_server_and_die.SetValue(true);
  });

  C5T_LIFETIME_MANAGER_TRACKED_THREAD(
      "thread for wss.py", ([&]() {
        struct State final {
          bool dying = false;
          std::set<int> conns;
          std::set<int> to_greet;
          std::vector<std::pair<int, std::string>> broadcasts;
        };
        current::WaitableAtomic<State> wa;
        bool done = false;
        std::thread t([&]() {
          time_to_stop_http_server_and_die.Wait();
          wa.MutableScopedAccessor()->dying = true;
          done = true;
        });
        while (!done) {
          C5T_LIFETIME_MANAGER_TRACKED_POPEN2(
              "python wss.py",
              {FLAGS_python_src},
              [&](std::string const& line) {
                if (line[0] == '+') {
                  auto const id = current::FromString<int>(line.c_str() + 1);
                  wa.MutableUse([&](State& state) {
                    state.conns.insert(id);
                    state.to_greet.insert(id);
                  });
                } else if (line[0] == '-') {
                  auto const id = current::FromString<int>(line.c_str() + 1);
                  wa.MutableUse([&](State& state) {
                    state.conns.erase(id);
                    state.to_greet.erase(id);
                  });
                } else {
                  char const* s = line.c_str();
                  int id;
                  s += sscanf(s, "%d", &id);
                  ++s;
                  wa.MutableUse([&](State& state) { state.broadcasts.emplace_back(id, s); });
                }
              },
              [&](Popen2Runtime& runtime) {
                std::cout << "python wss.py up and running" << std::endl;
                bool dying = false;
                std::set<int> conns;
                std::set<int> to_greet;
                std::vector<std::pair<int, std::string>> broadcasts;
                while (true) {
                  wa.Wait([](State const& s) { return s.dying || !s.to_greet.empty() || !s.broadcasts.empty(); },
                          [&](State& s) {
                            dying = s.dying;
                            conns = s.conns;
                            to_greet = std::move(s.to_greet);
                            broadcasts = std::move(s.broadcasts);
                          });
                  if (dying) {
                    for (int id : conns) {
                      runtime(current::ToString(id) + "\ndying\n");
                    }
                    runtime.Kill();
                    break;
                  }
                  for (int id : to_greet) {
                    runtime(current::ToString(id) + "\n# server " + GitCommit().substr(0u, 7u) + '\n');
                    runtime(current::ToString(id) + "\n# you are client index " + current::ToString(id) + '\n');
                  }

                  for (auto const& e : broadcasts) {
                    for (int id : conns) {
                      if (id != e.first) {
                        runtime(current::ToString(id) + '\n' + current::ToString(e.first) + "> " + e.second + '\n');
                      }
                    }
                  }
                }
              });
          if (!done) {
            std::cout << "python wss.py failed, waiting for 1s" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << "python wss.py failed, restarting" << std::endl;
          }
        }
        t.join();
      }));

  routes += http.Register("/tasks", [](Request r) {
    std::ostringstream oss;
    int n = 0u;
    C5T_LIFETIME_MANAGER_TRACKED_DEBUG_DUMP([&oss, &n](LifetimeTrackedInstance const& t) {
      if (!n) {
        oss << "running tasks:\n";
      }
      ++n;
      oss << current::strings::Printf("%d) %s @ %s:%d, up %.3lfs",
                                      n,
                                      t.description.c_str(),
                                      t.file_basename.c_str(),
                                      t.line_as_number,
                                      1e-6 * (current::time::Now() - t.t_added).count())
          << std::endl;
    });
    if (!n) {
      oss << "no running tasks\n";
    }
    r(oss.str());
  });

  time_to_stop_http_server_and_die.Wait();
  std::cout << "terminating per user request" << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  C5T_LIFETIME_MANAGER_SET_LOGGER([](std::string const&) {});  // Disable logging for the exit sequence.
  C5T_LIFETIME_MANAGER_EXIT(0);
  std::cerr << "should not see this!" << std::endl;
}
