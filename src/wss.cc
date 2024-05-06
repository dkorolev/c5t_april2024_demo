#include "blocks/http/api.h"
#include "bricks/dflags/dflags.h"
#include "bricks/file/file.h"
#include "bricks/strings/join.h"
#include "bricks/strings/split.h"
#include "bricks/sync/waitable_atomic.h"
#include "dlib_ext_msgreplier.h"
#include "lib_build_info.h"
#include "lib_c5t_dlib.h"
#include "lib_c5t_htmlform.h"
#include "lib_c5t_lifetime_manager.h"
#include "lib_c5t_logger.h"
#include "lib_c5t_popen2.h"  // IWYU pragma: keep

CURRENT_STRUCT(StopResponseSchema) { CURRENT_FIELD(msg, std::string); };
CURRENT_STRUCT(SumResponseSchema) {
  CURRENT_FIELD(sum, int64_t);
  CURRENT_FIELD(pw, Optional<std::string>);
};

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

  std::string const bin_path = []() {
    std::string const argv0 = current::Singleton<dflags::Argv0Container>().argv_0;
    std::vector<std::string> argv0_path = current::strings::Split(argv0, current::FileSystem::GetPathSeparator());
    argv0_path.pop_back();
    std::string const res = current::strings::Join(argv0_path, current::FileSystem::GetPathSeparator());
    return argv0[0] == current::FileSystem::GetPathSeparator() ? "/" + res : res;
  }();

  C5T_DLIB_SET_BASE_DIR(bin_path);
  C5T_LOGGER_SET_LOGS_DIR(bin_path);

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
    using namespace current::htmlform;
    if (r.method == "GET") {
      auto const f = Form()
                         .Title("Demo Stop Button")
                         .Caption("Press the button below to stop the service.")
                         .Add(Field("msg").Text("Optional message").Placeholder("... and thanks for all the fish ..."))
                         .ButtonText("Stop!");
      r(FormAsHTML(f), HTTPResponseCode.OK, current::net::constants::kDefaultHTMLContentType);
    } else {
      try {
        auto const body = ParseJSON<StopResponseSchema>(r.body);
        if (!body.msg.empty()) {
          std::cout << "optional stop message: " << body.msg << std::endl;
        }
      } catch (current::Exception& e) {
      }
      r(FormResponse().Fwd("/initiated"));
      time_to_stop_http_server_and_die.SetValue(true);
    }
  });

  routes += http.Register("/stop/initiated", [](Request r) { r("roger that, we're tearing down\n"); });

  routes += http.Register("/sum", [](Request r) {
    using namespace current::htmlform;
    if (r.method == "GET") {
      auto const f = Form()
                         .Add(Field("a").Text("First summand").Placeholder("3 for example").Value("3"))
                         .Add(Field("b").Text("Second summand").Placeholder("4 for example"))
                         .Add(Field("c").Text("Read-only caption").Value("Read-only text").Readonly())
                         .Add(Field("d").Text("Password caption").PasswordProtected())
                         .Title("Current Sum")
                         .Caption("Sum")
                         .ButtonText("Add")
                         .OnSubmit(R"({
        const a = parseInt(input.a);
        if (isNaN(a)) return { error: "A is not a number." };
        const b = parseInt(input.b);
        if (isNaN(b)) return { error: "B is not a number." };
        return { sum: a + b, pw: String(input.d) };
      })");
      r(FormAsHTML(f), HTTPResponseCode.OK, current::net::constants::kDefaultHTMLContentType);
    } else {
      FormResponse res;
      try {
        auto const body = ParseJSON<SumResponseSchema>(r.body);
        if (Exists(body.pw)) {
          std::cout << "PW: " << Value(body.pw) << std::endl;
        }
        res.fwd = "/is/" + current::ToString(body.sum);
      } catch (current::Exception& e) {
        res.msg = "error!";
      }
      r(res);
    }
  });

  routes += http.Register(
      "/sum/is", URLPathArgs::CountMask::One, [](Request r) { r("the sum is " + r.url_path_args[0] + '\n'); });

  routes += http.Register("/dlib", [](Request r) {
    std::ostringstream oss;
    int n = 0u;
    C5T_DLIB_LIST([&oss, &n](std::string const& s) {
      oss << ',' << s;
      ++n;
    });
    if (!n) {
      r("no dlibs loaded\n");
    } else {
      r(oss.str().substr(1));
    }
  });

  routes += http.Register("/dlib", URLPathArgs::CountMask::One, [](Request r) {
    std::string const name = r.url_path_args[0];
    C5T_DLIB_USE(
        name,
        [&r](C5T_DLib& dlib) {
          auto const s = dlib.CallReturningOptional<std::string()>("foo");
          if (Exists(s)) {
            r("has foo(): " + Value(s) + '\n');
          } else {
            r("no foo()\n");
          }
        },
        [&r]() { r("no such dlib\n"); });
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
        struct MsgReplier : IMsgReplier, ILogger {
          current::WaitableAtomic<State>& wa;
          char const* pmsg = nullptr;
          MsgReplier(current::WaitableAtomic<State>& wa) : ILogger(C5T_LOGGER()), wa(wa) {}
          char const* CurrentMessage() override { return pmsg; }
          void ReplyToAll(std::string const& msg) override {
            wa.MutableUse([&](State& state) { state.broadcasts.emplace_back(0, msg); });
          }
        };
        MsgReplier impl_msgreplier(wa);
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
                  if (std::string("stop") == s) {
                    wa.MutableUse([&](State& state) { state.broadcasts.emplace_back(0, "stopping"); });
                    time_to_stop_http_server_and_die.SetValue(true);
                  } else {
                    wa.MutableUse([&](State& state) { state.broadcasts.emplace_back(id, s); });
                    impl_msgreplier.pmsg = s;
                    C5T_DLIB_USE("msgreplier", [&impl_msgreplier](C5T_DLib& dlib) {
                      dlib.CallVoid<void(IDLib&)>("OnBroadcast", impl_msgreplier);
                    });
                  }
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
