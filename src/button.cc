#include "blocks/http/api.h"
#include "bricks/dflags/dflags.h"
#include "lib_button.h"

DEFINE_uint16(port, 5555, "");

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);
  auto& http = HTTP(current::net::BarePort(FLAGS_port));
  auto routes = http.Register("/", ServeButton);
  http.Join();
}
