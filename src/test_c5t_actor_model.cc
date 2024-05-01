/*
TESTS:
- create topic
  - dispatch events
  - nothing gest there
  - add one subscriber
  - dispatch
  - something gets there!
  - stop the subscriber scope
  - dispatch
  - does not get there!

- send to multiple
- unsubscribe some of them
-

- semantics: emit to  multiple destinations

- add some DEBUG WAIT FOR ALL QUEUES TO COMPLETE!
- add DISPATCHBATCOMPLETE!
- add DISPATCHTERMINATING!


TODOs;

- MULTIPLE TYPES PER TOPIC
- NEED THE REGISTRY OF TYPES PER TOPIC!
- TELEMETRY
- BETTER USAGE SYNTAX
  - emit to multiple destinations
- TESTS:
  - create topic
  - dispatch events
  - nothing gest there
  - add one subscriber
  - dispatch
  - something gets there!
  - stop the subscriber scope
  - dispatch
  - does not get there!
  - send to multiple
  - unsubscribe some of them
  - add some DEBUG WAIT FOR ALL QUEUES TO COMPLETE!
*/

#include <gtest/gtest.h>

#include "lib_c5t_actor_model.h"
#include "lib_c5t_dlib.h"
#include "lib_c5t_lifetime_manager.h"
#include "lib_test_actor_model.h"

#include "bricks/file/file.h"
#include "bricks/strings/split.h"
#include "bricks/strings/join.h"

struct InitLifetimeManager final {
  InitLifetimeManager() {
    C5T_LIFETIME_MANAGER_SET_LOGGER([](std::string const&) {});
  }
};
InitLifetimeManager InitLifetimeManager_impl;

struct InitDLibOnce final {
  InitDLibOnce() {
    std::string const bin_path = []() {
      // NOTE(dkorolev): I didn't find a quick way to get current binary dir and/or argv[0] from under `googletest`.
      std::vector<std::string> path = current::strings::Split(__FILE__, current::FileSystem::GetPathSeparator());
      path.pop_back();
#ifdef NDEBUG
      path.back() = ".current";
#else
      path.back() = ".current_debug";
#endif
      std::string const res = current::strings::Join(path, current::FileSystem::GetPathSeparator());
      return *__FILE__ == current::FileSystem::GetPathSeparator() ? "/" + res : res;
    }();
    C5T_DLIB_SET_BASE_DIR(bin_path);
  }
};
InitDLibOnce InitDLibOnce_impl;

TEST(ActorModelTest, StaticAsserts) {
  struct A final {};
  struct B final {};

  auto const a1 = Topic<A>("a1");
  auto const a2 = Topic<A>("a2");
  auto const a3 = Topic<A>("a3");
  auto const b1 = Topic<B>("b1");
  auto const b2 = Topic<B>("b1");
  auto const b3 = Topic<B>("b1");

  CURRENT_FAIL_IF_NOT_SAME_TYPE(decltype(a1), TopicKey<A> const);
  CURRENT_FAIL_IF_NOT_SAME_TYPE(decltype(b1), TopicKey<B> const);

  using a_t = TopicKeys<A>;
  using b_t = TopicKeys<B>;
  using ab_t = TopicKeys<A, B>;
  using ba_t = TopicKeys<B, A>;

  CURRENT_FAIL_IF_NOT_SAME_TYPE(decltype(a1 + a2 + a3), a_t);
  CURRENT_FAIL_IF_NOT_SAME_TYPE(decltype(+a1), a_t);
  CURRENT_FAIL_IF_NOT_SAME_TYPE(decltype(+a1 + a2), a_t);

  CURRENT_FAIL_IF_NOT_SAME_TYPE(decltype(b1 + b2 + b3), b_t);
  CURRENT_FAIL_IF_NOT_SAME_TYPE(decltype(+b1), b_t);
  CURRENT_FAIL_IF_NOT_SAME_TYPE(decltype(+b1 + b2), b_t);

  CURRENT_FAIL_IF_NOT_SAME_TYPE(decltype(a1 + b1), ab_t);
  CURRENT_FAIL_IF_NOT_SAME_TYPE(decltype(b1 + a1), ba_t);

  CURRENT_FAIL_IF_NOT_SAME_TYPE(decltype(a1 + b1 + a2 + a3), ab_t);
  CURRENT_FAIL_IF_NOT_SAME_TYPE(decltype(a1 + a2 + a3 + b1), ab_t);

  CURRENT_FAIL_IF_NOT_SAME_TYPE(decltype(b1 + a1 + b2 + b3), ba_t);
  CURRENT_FAIL_IF_NOT_SAME_TYPE(decltype(b1 + b2 + b3 + a1), ba_t);
}

template <char C>
struct TestEvent : crnt::CurrentSuper {
  int x;
  TestEvent(int x = 0) : x(x) {}
};

struct TestWorker final {
  std::ostringstream& oss;
  TestWorker(std::ostringstream& oss) : oss(oss) {}
  template <char C>
  void OnEvent(TestEvent<C> const& e) {
    oss << C << e.x;
  }
  void OnBatchDone() {}
  void OnShutdown() {}
};

TEST(ActorModelTest, Smoke) {
  auto const a1 = Topic<TestEvent<'a'>>("a1");
  auto const a2 = Topic<TestEvent<'a'>>("a2");
  auto const a3 = Topic<TestEvent<'a'>>("a3");
  auto const b1 = Topic<TestEvent<'b'>>("b1");
  auto const b2 = Topic<TestEvent<'b'>>("b2");
  auto const b3 = Topic<TestEvent<'b'>>("b3");

  {
    std::ostringstream oss;
    ActorSubscriberScope const s1 = (a1 + a2).NewSubscribeTo<TestWorker>(oss);
    EmitTo<TestEvent<'a'>>(a1, 101);
    EmitTo<TestEvent<'a'>>(a2, 102);
    EmitTo<TestEvent<'a'>>(a3, 103);
    C5T_ACTORS_DEBUG_WAIT_FOR_ALL_EVENTS_TO_PROPAGATE();
    EXPECT_EQ("a101a102", oss.str());
    // TODO: emitting the wrong type should be a compilation error, no?
    EmitTo<TestEvent<'b'>>(b1, 201);
    EmitTo<TestEvent<'b'>>(b2, 202);
    EmitTo<TestEvent<'b'>>(b3, 203);
    C5T_ACTORS_DEBUG_WAIT_FOR_ALL_EVENTS_TO_PROPAGATE();
    EXPECT_EQ("a101a102", oss.str());
    ActorSubscriberScope const s2 = (b1 + b2).NewSubscribeTo<TestWorker>(oss);
    EmitTo<TestEvent<'b'>>(b1, 301);
    EmitTo<TestEvent<'b'>>(b2, 302);
    EmitTo<TestEvent<'b'>>(b3, 303);
    C5T_ACTORS_DEBUG_WAIT_FOR_ALL_EVENTS_TO_PROPAGATE();
    EXPECT_EQ("a101a102b301b302", oss.str());
  }
}

TEST(ActorModelTest, InjectedFromDLib) {
  EXPECT_EQ(42,
            C5T_DLIB_CALL("test_actor_model", [&](C5T_DLib& dlib) { return dlib.CallOrDefault<int()>("Smoke42"); }));
  EXPECT_EQ(
      0, C5T_DLIB_CALL("test_actor_model", [&](C5T_DLib& dlib) { return dlib.CallOrDefault<int()>("NonExistent"); }));

  IActorModel iam(C5T_ACTOR_MODEL_INSTANCE());
  auto const t = Topic<Event_DL2TEST>("topic_from_dlib");

  struct TestWorker final {
    bool first = true;
    std::ostringstream& oss;
    TestWorker(std::ostringstream& oss) : oss(oss) {}
    void OnEvent(Event_DL2TEST const& e) {
      if (first) {
        first = false;
      } else {
        oss << ',';
      }
      oss << e.v;
    }
    void OnBatchDone() {}
    void OnShutdown() {}
  };

  std::ostringstream oss;
  // TODO: (t+t) is ugly ...
  ActorSubscriberScope const s = (t + t).NewSubscribeTo<TestWorker>(oss);

  C5T_DLIB_CALL("test_actor_model",
                [&](C5T_DLib& dlib) { dlib.CallVoid<void(IDLib&, TopicID)>("ExternalEmitter", iam, t.GetTopicID()); });

  C5T_ACTORS_DEBUG_WAIT_FOR_ALL_EVENTS_TO_PROPAGATE();
  EXPECT_EQ("42", oss.str());

  for (int i = 0; i < 2; ++i) {
    C5T_DLIB_CALL("test_actor_model", [&](C5T_DLib& dlib) {
      dlib.CallVoid<void(IDLib&, TopicID)>("ExternalEmitter", iam, t.GetTopicID());
    });
  }

  C5T_ACTORS_DEBUG_WAIT_FOR_ALL_EVENTS_TO_PROPAGATE();
  EXPECT_EQ("42,43,44", oss.str());
}
