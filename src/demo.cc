#include "blocks/http/api.h"
#include "bricks/dflags/dflags.h"
#include "bricks/sync/waitable_atomic.h"

DEFINE_uint16(port, 5555, "");

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  current::WaitableAtomic<bool> time_to_stop_http_server_and_die(false);

  auto& http = []() -> current::http::HTTPServerPOSIX& {
    uint64_t const port_number = FLAGS_port;
    try {
      auto hold_port = current::net::ReservedLocalPort(
          current::net::ReservedLocalPort::Construct(),
          port_number,
          current::net::SocketHandle(current::net::SocketHandle::BindAndListen(), current::net::BarePort(port_number)));
      std::cout << "listening on http://localhost: " << port_number << ", /stop to terminate" << std::endl;
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

  routes += http.Register("/stop", [&time_to_stop_http_server_and_die](Request r) {
    r("stopping\n",
      HTTPResponseCode.Found,
      current::net::http::Headers({{"Location", "/up?from=stop"}, {"Cache-Control", "no-store, must-revalidate"}}),
      current::net::constants::kDefaultHTMLContentType);
    time_to_stop_http_server_and_die.SetValue(true);
  });

  time_to_stop_http_server_and_die.Wait();
  std::cout << "terminating per user request" << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
