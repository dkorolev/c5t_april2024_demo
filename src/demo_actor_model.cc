// REMAINS TODO IN SYNTAX?
// - C5T_TOPIC / C5T_DECLARE_TOPIC?
// - C5T_EMIT
// - C5T_SUBSCRIBE

#include <iostream>
#include <chrono>
#include <thread>

#include "bricks/exception.h"
#include "lib_c5t_lifetime_manager.h"
#include "lib_c5t_actor_model.h"

#include "lib_demo_actor_model_extra.h"

#include "bricks/dflags/dflags.h"
#include "blocks/http/api.h"

DEFINE_uint16(port, 5555, "");

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  // The actor model requires the lifetime manager to be active.
  C5T_LIFETIME_MANAGER_SET_LOGGER([](std::string const& s) { std::cerr << "MGR: " << s << std::endl; });

  auto const topic_timer = Topic<TimerEvent>("timer");
  auto const topic_input = Topic<InputEvent>("input");

  auto& http = HTTP(current::net::BarePort(FLAGS_port));

  current::WaitableAtomic<bool> time_to_stop_http_server_and_die(false);
  auto scope = http.Register("/kill", [&time_to_stop_http_server_and_die](Request r) {
    r("Gone.\n");
    time_to_stop_http_server_and_die.SetValue(true);
  });

  scope += http.Register("/status", [&](Request r) {
    std::ostringstream oss;
    C5T_LIFETIME_MANAGER_TRACKED_DEBUG_DUMP([&oss](LifetimeTrackedInstance const& t) {
      oss << current::strings::Printf("- %s @ %s:%d, up %.3lfs",
                                      t.description.c_str(),
                                      t.file_basename.c_str(),
                                      t.line_as_number,
                                      1e-6 * (current::time::Now() - t.t_added).count())
          << std::endl;
    });
    r(oss.str());
  });

  StartTimerThread(topic_timer);

  scope += http.Register("/", [&](Request r) {
    C5T_LIFETIME_MANAGER_TRACKED_THREAD(
        "chunked socket", ([topic_timer, topic_input, moved_r = std::move(r)]() mutable {
          // Will stop this thread when one of two things happen:
          // 1) socket write failure ("connection reset by peer"), or
          // 2) application shutdown.
          current::WaitableAtomic<bool> stop_chunked_connection_thread(false);
          struct ChunksSender final {
            current::WaitableAtomic<bool>& stop_chunked_connection_thread;
            Request r;
            bool connection_reset_by_peer = false;
            decltype(std::declval<Request>().SendChunkedResponse()) rs;

            ChunksSender(current::WaitableAtomic<bool>& stop_chunked_connection_thread, Request r0)
                : stop_chunked_connection_thread(stop_chunked_connection_thread),
                  r(std::move(r0)),
                  rs(r.SendChunkedResponse()) {
              Send("Yo\n");
            }

            ~ChunksSender() {
              if (!connection_reset_by_peer) {
                try {
                  rs("terminating\n");
                } catch (current::Exception const&) {
                  connection_reset_by_peer = true;
                  stop_chunked_connection_thread.SetValue(true);
                }
              }
            }

            void Send(std::string s) {
              try {
                rs(s);
              } catch (current::Exception const&) {
                connection_reset_by_peer = true;
                stop_chunked_connection_thread.SetValue(true);
              }
            }

            void OnEvent(TimerEvent const& te) { Send(current::ToString(te.i) + '\n'); }
            void OnEvent(InputEvent const& ie) { Send(ie.s + '\n'); }
            void OnBatchDone() {}
            void OnShutdown() {}
          };

          ActorSubscriberScope const s1 =
              (topic_timer + topic_input)
                  .NewSubscribeTo<ChunksSender>(stop_chunked_connection_thread, std::move(moved_r));

          auto const s2 =
              C5T_LIFETIME_MANAGER_NOTIFY_OF_SHUTDOWN([&]() { stop_chunked_connection_thread.SetValue(true); });
          stop_chunked_connection_thread.Wait();
        }));
  });

  C5T_LIFETIME_MANAGER_TRACKED_THREAD("stdin!", [&topic_input]() {
    // NOTE(dkorolev): This thread does not terminate by itself, and will cause an `::abort()`.
    while (true) {
      std::cout << "Enter whatever: " << std::flush;
      std::string s;
      std::getline(std::cin, s);
      EmitTo<InputEvent>(topic_input, s);
      std::cout << "Line sent to all chunk HTTP listeners: " << s << std::endl;
    }
  });

  // NOTE(dkorolev): No `http.Join()`, since HTTP is not lifetime-management-friendly yet.
  time_to_stop_http_server_and_die.Wait([](bool b) { return b; });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  C5T_LIFETIME_MANAGER_EXIT(0);
}
