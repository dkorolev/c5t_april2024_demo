#include "lib_demo_routes_dlib.h"
#include "blocks/http/api.h"
#include "lib_c5t_dlib.h"
#include "lib_http_server.h"

void RegisterDemoRoutesDLib(std::string const& bin_path, HTTPServerContext& ctx) {
  C5T_DLIB_SET_BASE_DIR(bin_path);

  current::http::HTTPServerPOSIX& http = ctx.http;
  HTTPRoutesScope& routes = *reinterpret_cast<HTTPRoutesScope*>(ctx.proutes);

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
}
