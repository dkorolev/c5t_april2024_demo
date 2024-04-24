#include "blocks/http/api.h"
#include "bricks/dflags/dflags.h"
#include "lib_button.h"

DEFINE_uint16(port, 5555, "");

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);
  auto& http = HTTP(current::net::BarePort(FLAGS_port));
  auto routes = http.Register("/", ServeButton);
  routes += http.Register(
      "/sum", URLPathArgs::CountMask::One, [](Request r) { r("the sum is " + r.url_path_args[0] + '\n'); });
  http.Join();
}
