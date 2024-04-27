#include "lib_http_server.h"  // IWYU pragma: keep

#include "lib_c5t_lifetime_manager.h"

#include "blocks/http/api.h"

// NOTE(dkorolev): Using two workaround for `HTTP()` for it to work with graceful termination / lifetime management.
// 1) [ optional ] Construct it as a `C5T_LIFETIME_MANAGER_TRACKED_INSTANCE()`, not via the `HTTP()` singleton, and
// 2) [ required ] Do not call `C5T_LIFETIME_MANAGER_EXIT(0)` directly from the HTTP route handler.
// TODO(dkorolev): Have Current's HTTP server use `Owned/Borrowed` at least, or, better yet, the lifetime manager.
void RunHTTPServer(uint16_t port_number, std::function<void(HTTPServerContext&)> code) {
  try {
    auto hold_port = current::net::ReservedLocalPort(
        current::net::ReservedLocalPort::Construct(),
        port_number,
        current::net::SocketHandle(current::net::SocketHandle::BindAndListen(), current::net::BarePort(port_number)));
    std::cout << "listening on http://localhost: " << port_number << ", /stop to terminate" << std::endl;
    current::http::HTTPServerPOSIX& http = C5T_LIFETIME_MANAGER_TRACKED_INSTANCE(
        current::http::HTTPServerPOSIX, "http server lifetime", std::move(hold_port));
    auto const start_time = current::time::Now();
    HTTPRoutesScope routes = http.Register("/up", [start_time](Request r) {
      r(current::strings::Printf("up %0.lfs\n%s",
                                 1e-6 * (current::time::Now() - start_time).count(),
                                 C5T_LIFETIME_MANAGER_SHUTTING_DOWN ? "IN TERMINATION SEQUENCE\n" : ""));
    });
    HTTPServerContext ctx(http, reinterpret_cast<void*>(&routes));
    code(ctx);
  } catch (const current::Exception& e) {
    std::cout << "port " << port_number << " appears to be taken" << std::endl;
    C5T_LIFETIME_MANAGER_EXIT(1);
  }
}

void HTTPServerContext::FastRegister(std::string const& route,
                                     HTTPServerContext::CountMask mask,
                                     std::function<current::http::Response(FastRequest const&)> f) {
  *(reinterpret_cast<HTTPRoutesScope*>(proutes)) +=
      http.Register(route, static_cast<URLPathArgs::CountMask>(mask), [ff = std::move(f)](Request r) {
        FastRequest req(r.method, r.body);
        req.url_path_args.reserve(r.url_path_args.size());
        for (size_t i = 0; i < r.url_path_args.size(); ++i) {
          req.url_path_args.push_back(r.url_path_args[i]);
        }
        Response res = ff(req);
        r(res);
      });
}
