#include "bricks/dflags/dflags.h"
#include "bricks/strings/split.h"
#include "bricks/strings/join.h"
#include "bricks/file/file.h"

#include "bricks/sync/waitable_atomic.h"
#include "lib_c5t_lifetime_manager.h"
#include "lib_c5t_logger.h"
#include "lib_demo_routes_basic.h"
#include "lib_demo_routes_dlib.h"
#include "lib_demo_routes_heavy.h"
#include "lib_http_server.h"

DEFINE_uint16(port, 5555, "");

void Run(HTTPServerContext& ctx);

struct BinPathSingleton final {
  std::string const bin_path;
  BinPathSingleton() : bin_path(GetBinPath()) {}

  static std::string GetBinPath() {
    std::string const argv0 = current::Singleton<dflags::Argv0Container>().argv_0;
    std::vector<std::string> argv0_path = current::strings::Split(argv0, current::FileSystem::GetPathSeparator());
    argv0_path.pop_back();
    std::string const res = current::strings::Join(argv0_path, current::FileSystem::GetPathSeparator());
    return argv0[0] == current::FileSystem::GetPathSeparator() ? "/" + res : res;
  }
};

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  std::string const& bin_path = current::Singleton<BinPathSingleton>().bin_path;
  C5T_LOGGER_SET_LOGS_DIR(bin_path);

  C5T_LOGGER("demo") << "demo started";
  C5T_LIFETIME_MANAGER_SET_LOGGER([](std::string const& s) { C5T_LOGGER("life") << s; });

  RunHTTPServer(FLAGS_port, Run);
}

void Run(HTTPServerContext& ctx) {
  // NOTE(dkorolev): This `WaitableAtomic<bool>` is the HTTP graceful shutdown workaround part 2/2.
  current::WaitableAtomic<bool> time_to_stop_http_server_and_die(false);

  RegisterDemoRoutesBasic(time_to_stop_http_server_and_die, ctx);
  RegisterDemoRoutesHeavy(ctx);
  RegisterDemoRoutesDLib(current::Singleton<BinPathSingleton>().bin_path, ctx);

  time_to_stop_http_server_and_die.Wait();
  std::cout << "terminating per user request" << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  C5T_LIFETIME_MANAGER_EXIT(0);
  std::cerr << "should not see this!" << std::endl;
}
