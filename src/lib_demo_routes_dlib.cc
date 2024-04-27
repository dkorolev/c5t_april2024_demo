#include "lib_demo_routes_dlib.h"

#include "lib_c5t_dlib.h"
#include "lib_http_server.h"

void RegisterDemoRoutesDLib(std::string const& bin_path, HTTPServerContext& ctx) {
  C5T_DLIB_SET_BASE_DIR(bin_path);

  ctx.FastRegister("/dlib", HTTPServerContext::CountMask::None, [](FastRequest const&) {
    current::http::Response resp;
    std::ostringstream oss;
    int n = 0u;
    C5T_DLIB_LIST([&oss, &n](std::string const& s) {
      oss << ',' << s;
      ++n;
    });
    if (!n) {
      resp.Body("no dlibs loaded\n");
    } else {
      resp.Body(oss.str().substr(1));
    }
    return resp;
  });

  ctx.FastRegister("/dlib", HTTPServerContext::CountMask::One, [](FastRequest const& r) {
    std::string const name = r.url_path_args[0];
    current::http::Response resp;
    C5T_DLIB_USE(
        name,
        [&resp](C5T_DLib& dlib) {
          auto const s = dlib.Call<std::string()>("foo");
          if (Exists(s)) {
            resp.Body("has foo(): " + Value(s) + '\n');
          } else {
            resp.Body("no foo()\n");
          }
        },
        [&resp]() { resp.Body("no such dlib\n"); });
    return resp;
  });

  ctx.FastRegister("/dlib_reload", HTTPServerContext::CountMask::One, [](FastRequest const& r) {
    std::string const name = r.url_path_args[0];
    current::http::Response resp;
    auto const res = C5T_DLIB_RELOAD(name).res;
    if (res == C5T_DLIB_RELOAD_STATUS::UpToDate) {
      resp.Body("up to date\n");
    } else if (res == C5T_DLIB_RELOAD_STATUS::Loaded) {
      resp.Body("loaded\n");
    } else if (res == C5T_DLIB_RELOAD_STATUS::Reloaded) {
      resp.Body("reloaded\n");
    } else {
      resp.Body("failed\n");
    }
    return resp;
  });
}
