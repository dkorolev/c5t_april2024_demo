#include "blocks/http/api.h"

#include "bricks/dflags/dflags.h"
#include "bricks/strings/split.h"
#include "bricks/strings/join.h"
#include "bricks/file/file.h"

#include "bricks/sync/waitable_atomic.h"
#include "lib_c5t_dlib.h"
#include "lib_c5t_lifetime_manager.h"
#include "lib_c5t_logger.h"
#include "lib_c5t_popen2.h"  // IWYU pragma: keep
#include "lib_http_server.h"

DEFINE_uint16(port, 5555, "");

void Run(HTTPServerContext& ctx);

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

  C5T_LOGGER("demo") << "demo started";
  C5T_LIFETIME_MANAGER_SET_LOGGER([](std::string const& s) { C5T_LOGGER("life") << s; });

  RunHTTPServer(FLAGS_port, Run);
}

void Run(HTTPServerContext& ctx) {
  current::http::HTTPServerPOSIX& http = ctx.http;
  HTTPRoutesScope& routes = *reinterpret_cast<HTTPRoutesScope*>(ctx.proutes);
  current::WaitableAtomic<bool> time_to_stop_http_server_and_die(false);  // NOTE(dkorolev): The workaround part 2/2.
  routes += http.Register("/stop", [&time_to_stop_http_server_and_die](Request r) {
    C5T_LOGGER("demo") << "/stop requested";
    r("stopping\n",
      HTTPResponseCode.Found,
      current::net::http::Headers({{"Location", "/up?from=stop"}, {"Cache-Control", "no-store, must-revalidate"}}),
      current::net::constants::kDefaultHTMLContentType);
    time_to_stop_http_server_and_die.SetValue(true);
  });

  routes += http.Register("/seq", URLPathArgs::CountMask::None | URLPathArgs::CountMask::One, [](Request r) {
    C5T_LOGGER("demo") << "/seq requested";
    C5T_LIFETIME_MANAGER_TRACKED_THREAD(
        "chunked response sender",
        [](Request r) {
          std::string N = "5";
          if (r.url_path_args.size() >= 1u) {
            N = r.url_path_args[0];
          }
          std::string cmd = "for i in $(seq " + N + "); do echo $i; sleep 0.05; done";
          auto rc = r.SendChunkedResponse();
          C5T_LOGGER("demo") << "/seq started";
          C5T_LIFETIME_MANAGER_TRACKED_POPEN2(cmd, {"bash", "-c", cmd}, [&rc](std::string const& s) {
            C5T_LOGGER("demo") << "/seq: " << s;
            rc(s + '\n');
          });
          C5T_LOGGER("demo") << "/seq done";
        },
        std::move(r));
  });

  routes += http.Register("/tasks", [](Request r) {
    C5T_LOGGER("life") << "/tasks";
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
    std::string const s = oss.str();
    C5T_LOGGER("life") << s;
    r(s);
  });

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
          auto const s = dlib.Call<std::string()>("foo");
          if (Exists(s)) {
            r("has foo(): " + Value(s) + '\n');
          } else {
            r("no foo()\n");
          }
        },
        [&r]() { r("no such dlib\n"); });
  });

  routes += http.Register("/dlib_reload", URLPathArgs::CountMask::One, [](Request r) {
    std::string const name = r.url_path_args[0];
    auto const res = C5T_DLIB_RELOAD(name).res;
    if (res == C5T_DLIB_RELOAD_STATUS::UpToDate) {
      r("up to date\n");
    } else if (res == C5T_DLIB_RELOAD_STATUS::Loaded) {
      r("loaded\n");
    } else if (res == C5T_DLIB_RELOAD_STATUS::Reloaded) {
      r("reloaded\n");
    } else {
      r("failed\n");
    }
  });

  time_to_stop_http_server_and_die.Wait();
  std::cout << "terminating per user request" << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  C5T_LIFETIME_MANAGER_EXIT(0);
  std::cerr << "should not see this!" << std::endl;
}
