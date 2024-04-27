#include "lib_demo_routes.h"

#include "blocks/http/api.h"
#include "lib_c5t_logger.h"
#include "lib_c5t_lifetime_manager.h"
#include "lib_c5t_popen2.h"  // IWYU pragma: keep

void RegisterDemoRoutes(HTTPServerContext& ctx) {
  current::http::HTTPServerPOSIX& http = ctx.http;
  HTTPRoutesScope& routes = *reinterpret_cast<HTTPRoutesScope*>(ctx.proutes);

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
}
