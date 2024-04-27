#include "lib_demo_routes_basic.h"  // IWYU pragma: keep

#include "blocks/http/api.h"
#include "bricks/sync/waitable_atomic.h"

void RegisterDemoRoutesBasic(current::WaitableAtomic<bool>& time_to_stop_http_server_and_die, HTTPServerContext& ctx) {
  current::http::HTTPServerPOSIX& http = ctx.http;
  HTTPRoutesScope& routes = *reinterpret_cast<HTTPRoutesScope*>(ctx.proutes);

  routes += http.Register("/stop", [&time_to_stop_http_server_and_die](Request r) {
    r("stopping\n",
      HTTPResponseCode.Found,
      current::net::http::Headers({{"Location", "/up?from=stop"}, {"Cache-Control", "no-store, must-revalidate"}}),
      current::net::constants::kDefaultHTMLContentType);
    time_to_stop_http_server_and_die.SetValue(true);
  });
}
