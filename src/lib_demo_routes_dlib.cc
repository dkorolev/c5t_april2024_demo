#include <sstream>

#include "lib_demo_routes_dlib.h"

#include "lib_c5t_dlib.h"
#include "lib_http_server.h"

void RegisterDemoRoutesDLib(std::string const& bin_path, HTTPServerContext& ctx) {
  C5T_DLIB_SET_BASE_DIR(bin_path);

  ctx.FastRegister("/dlib", HTTPServerContext::CountMask::None, [](FastRequest const&) {
    std::ostringstream oss;
    int n = 0u;
    C5T_DLIB_LIST([&oss, &n](std::string const& s) {
      oss << ',' << s;
      ++n;
    });
    if (!n) {
      return FastResponse().Body("no dlibs loaded\n");
    } else {
      return FastResponse().Body(oss.str().substr(1));
    }
  });

  ctx.FastRegister("/dlib", HTTPServerContext::CountMask::One, [](FastRequest const& r) {
    std::string const name = r.url_path_args[0];
    return C5T_DLIB_CALL(
        name,
        [](C5T_DLib& dlib) {
          auto const s = dlib.Call<std::string()>("foo");
          if (Exists(s)) {
            return FastResponse().Body("has foo(): " + Value(s) + '\n');
          } else {
            return FastResponse().Body("no foo()\n");
          }
        },
        []() { return FastResponse().Body("no such dlib\n"); });
  });

  ctx.FastRegister("/dlib_reload", HTTPServerContext::CountMask::One, [](FastRequest const& r) {
    std::string const name = r.url_path_args[0];
    auto const res = C5T_DLIB_RELOAD(name).res;
    if (res == C5T_DLIB_RELOAD_STATUS::UpToDate) {
      return FastResponse().Body("up to date\n");
    } else if (res == C5T_DLIB_RELOAD_STATUS::Loaded) {
      return FastResponse().Body("loaded\n");
    } else if (res == C5T_DLIB_RELOAD_STATUS::Reloaded) {
      return FastResponse().Body("reloaded\n");
    } else {
      return FastResponse().Body("failed\n");
    }
  });
}
