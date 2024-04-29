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

#include "lib_c5t_lifetime_manager.h"
#include "lib_c5t_actor_model.h"

struct InitLifetimeManager final {
  InitLifetimeManager() {
    C5T_LIFETIME_MANAGER_SET_LOGGER([](std::string const&) {});
  }
};
InitLifetimeManager InitLifetimeManager_impl;

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
struct TestEvent {
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
    ActorSubscriberScope const _ = (a1 + a2).NewSubscribeTo<TestWorker>(oss);
    EmitTo<TestEvent<'a'>>(a1, 101);
    EmitTo<TestEvent<'a'>>(a2, 102);
    EmitTo<TestEvent<'a'>>(a3, 103);
    C5T_ACTORS_DEBUG_WAIT_FOR_ALL_EVENTS_TO_PROPAGATE();
    EXPECT_EQ("a101a102", oss.str());
  }
}
