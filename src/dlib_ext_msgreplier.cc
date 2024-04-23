#include "dlib_ext_msgreplier.h"
#include <cstdio>

bool welcome_sent = false;
int c = 0;
extern "C" void OnBroadcast(IDLib& iface) {
  iface.Use<IMsgReplier>([](IMsgReplier& msg_replier) {
    if (!welcome_sent) {
      msg_replier.ReplyToAll("welcome to the `msgreplier` dlib!");
      msg_replier.ReplyToAll("try `2+2` or `c=100` to seem me in action");
      msg_replier.ReplyToAll("if you change `dlib_ext_msgreplier.cc` and `make`, i will re-load for the next msg");
      welcome_sent = true;
    }
    int a, b;
    int c_value;
    if (::sscanf(msg_replier.CurrentMessage(), "%d+%d", &a, &b) == 2u) {
      if (!c) {
        msg_replier.ReplyToAll(current::strings::Printf("%d+%d=%d", a, b, a + b));
      } else {
        msg_replier.ReplyToAll(current::strings::Printf("%d+%d+C=%d", a, b, a + b + c));
      }
    } else if (::sscanf(msg_replier.CurrentMessage(), "c=%d", &c_value) == 1u) {
      if (c_value == 0) {
        c = 0;
        msg_replier.ReplyToAll("the value of C is reset");
      } else {
        c = c_value;
        msg_replier.ReplyToAll("set C to a new value, try 2+2 again");
      }
    }
  });
}
