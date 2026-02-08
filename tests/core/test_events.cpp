#include <gtest/gtest.h>
#include "engine/core/events/event_system.h"

using namespace nge;
using namespace nge::events;

struct TestEvent : Event {
    int value = 0;
};

struct AnotherEvent : Event {
    std::string message;
};

TEST(EventSystem, SubscribeAndPublish) {
    EventBus bus;
    int received = 0;

    auto id = bus.Subscribe<TestEvent>([&](const TestEvent& e) {
        received = e.value;
    });

    TestEvent evt;
    evt.value = 42;
    bus.Publish(evt);

    EXPECT_EQ(received, 42);
    (void)id;
}

TEST(EventSystem, MultipleSubscribers) {
    EventBus bus;
    int countA = 0, countB = 0;

    bus.Subscribe<TestEvent>([&](const TestEvent&) { countA++; });
    bus.Subscribe<TestEvent>([&](const TestEvent&) { countB++; });

    bus.Publish(TestEvent{});
    EXPECT_EQ(countA, 1);
    EXPECT_EQ(countB, 1);

    bus.Publish(TestEvent{});
    EXPECT_EQ(countA, 2);
    EXPECT_EQ(countB, 2);
}

TEST(EventSystem, Unsubscribe) {
    EventBus bus;
    int count = 0;

    auto id = bus.Subscribe<TestEvent>([&](const TestEvent&) { count++; });
    bus.Publish(TestEvent{});
    EXPECT_EQ(count, 1);

    bus.Unsubscribe<TestEvent>(id);
    bus.Publish(TestEvent{});
    EXPECT_EQ(count, 1); // Should not increment
}

TEST(EventSystem, DifferentEventTypes) {
    EventBus bus;
    int testCount = 0;
    std::string message;

    bus.Subscribe<TestEvent>([&](const TestEvent& e) { testCount = e.value; });
    bus.Subscribe<AnotherEvent>([&](const AnotherEvent& e) { message = e.message; });

    TestEvent te;
    te.value = 99;
    bus.Publish(te);

    AnotherEvent ae;
    ae.message = "hello";
    bus.Publish(ae);

    EXPECT_EQ(testCount, 99);
    EXPECT_EQ(message, "hello");
}

TEST(EventSystem, EmitHelper) {
    EventBus bus;
    int received = 0;

    bus.Subscribe<TestEvent>([&](const TestEvent& e) { received = e.value; });
    bus.Emit<TestEvent>(TestEvent{.value = 77});

    EXPECT_EQ(received, 77);
}

TEST(EventSystem, ClearAll) {
    EventBus bus;
    int count = 0;

    bus.Subscribe<TestEvent>([&](const TestEvent&) { count++; });
    bus.Publish(TestEvent{});
    EXPECT_EQ(count, 1);

    bus.Clear();
    bus.Publish(TestEvent{});
    EXPECT_EQ(count, 1); // Cleared, should not increment
}

TEST(EventSystem, NoSubscribersIsNoOp) {
    EventBus bus;
    // Publishing with no subscribers should not crash
    bus.Publish(TestEvent{});
    bus.Publish(AnotherEvent{});
}

TEST(EventSystem, GlobalEventBus) {
    auto& bus = GetGlobalEventBus();
    int count = 0;

    auto id = bus.Subscribe<TestEvent>([&](const TestEvent&) { count++; });
    bus.Publish(TestEvent{});
    EXPECT_EQ(count, 1);

    // Cleanup
    bus.Unsubscribe<TestEvent>(id);
}
