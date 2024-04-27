#include "lib_demo_routes_basic.h"  // IWYU pragma: keep

#include "bricks/sync/waitable_atomic.h"

void RegisterDemoRoutesBasic(current::WaitableAtomic<bool>& time_to_stop_http_server_and_die, HTTPServerContext& ctx) {
  ctx.FastRegister(
      "/stop", HTTPServerContext::CountMask::None, [&time_to_stop_http_server_and_die](FastRequest const&) {
        time_to_stop_http_server_and_die.SetValue(true);
        return FastResponse()
            .Body("stopping\n")
            .Code(HTTPResponseCode.Found)
            .SetHeader("Location", "/up?from=stop")
            .SetHeader("Cache-Control", "no-store, must-revalidate")
            .ContentType("text/html");  // NOTE: Was `current::net::constants::kDefaultHTMLContentType`.
      });
}
