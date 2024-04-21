#include "blocks/http/api.h"
#include "bricks/dflags/dflags.h"

#include "bricks/sync/waitable_atomic.h"
#include "lib_c5t_popen2.h"  // IWYU pragma: keep
#include "lib_c5t_lifetime_manager.h"

DEFINE_uint16(port, 5555, "");

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  C5T_LIFETIME_MANAGER_SET_LOGGER([](std::string const& s) { std::cout << "C5T lifetime: " << s << std::endl; });

  // NOTE(dkorolev): Using two workaround for `HTTP()` for it to work with graceful termination / lifetime management.
  // 1) [ optional ] Construct it as a `C5T_LIFETIME_MANAGER_TRACKED_INSTANCE()`, not via the `HTTP()` singleton, and
  // 2) [ required ] Do not call `C5T_LIFETIME_MANAGER_EXIT(0)` directly from the HTTP route handler.
  // TODO(dkorolev): Have Current's HTTP server use `Owned/Borrowed` at least, or, better yet, the lifetime manager.
  using current::http::HTTPServerPOSIX;
  auto& http = []() -> HTTPServerPOSIX& {
    uint16_t const port_number = FLAGS_port;
    try {
      auto hold_port = current::net::ReservedLocalPort(
          current::net::ReservedLocalPort::Construct(),
          port_number,
          current::net::SocketHandle(current::net::SocketHandle::BindAndListen(), current::net::BarePort(port_number)));
      std::cout << "listening on http://localhost: " << port_number << ", /stop to terminate" << std::endl;
      return C5T_LIFETIME_MANAGER_TRACKED_INSTANCE(HTTPServerPOSIX, "http server lifetime", std::move(hold_port));
    } catch (const current::Exception& e) {
      std::cout << "port " << port_number << " appears to be taken" << std::endl;
      C5T_LIFETIME_MANAGER_EXIT(1);
      return *static_cast<HTTPServerPOSIX*>(nullptr);
    }
  }();

  auto const start_time = current::time::Now();
  auto routes = http.Register("/up", [start_time](Request r) {
    r(current::strings::Printf("up %0.lfs\n%s",
                               1e-6 * (current::time::Now() - start_time).count(),
                               C5T_LIFETIME_MANAGER_SHUTTING_DOWN ? "IN TERMINATION SEQUENCE\n" : ""));
  });

  current::WaitableAtomic<bool> time_to_stop_http_server_and_die(false);  // NOTE(dkorolev): The workaround part 2/2.
  routes += http.Register("/stop", [&time_to_stop_http_server_and_die](Request r) {
    r("stopping\n",
      HTTPResponseCode.Found,
      current::net::http::Headers({{"Location", "/up?from=stop"}, {"Cache-Control", "no-store, must-revalidate"}}),
      current::net::constants::kDefaultHTMLContentType);
    time_to_stop_http_server_and_die.SetValue(true);
  });

  routes += http.Register("/seq", URLPathArgs::CountMask::None | URLPathArgs::CountMask::One, [](Request r) {
    C5T_LIFETIME_MANAGER_TRACKED_THREAD(
        "chunked response sender",
        [](Request r) {
          std::string N = "5";
          if (r.url_path_args.size() >= 1u) {
            N = r.url_path_args[0];
          }
          std::string cmd = "for i in $(seq " + N + "); do echo $i; sleep 0.05; done";
          auto rc = r.SendChunkedResponse();
          C5T_LIFETIME_MANAGER_TRACKED_POPEN2(cmd, {"bash", "-c", cmd}, [&rc](std::string const& s) { rc(s + '\n'); });
        },
        std::move(r));
  });

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
