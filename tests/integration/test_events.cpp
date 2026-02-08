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

class EventSystemTest : public ::testing::Test {
protected:
    EventBus m_bus;
};

TEST_F(EventSystemTest, SubscribeAndPublish) {
    int received = 0;
    m_bus.Subscribe<TestEvent>([&](const TestEvent& e) { received = e.value; });

    TestEvent evt;
    evt.value = 42;
    m_bus.Publish(evt);
    EXPECT_EQ(received, 42);
}

TEST_F(EventSystemTest, MultipleSubscribers) {
    int countA = 0, countB = 0;
    m_bus.Subscribe<TestEvent>([&](const TestEvent&) { countA++; });
    m_bus.Subscribe<TestEvent>([&](const TestEvent&) { countB++; });

    m_bus.Publish(TestEvent{});
    EXPECT_EQ(countA, 1);
    EXPECT_EQ(countB, 1);

    m_bus.Publish(TestEvent{});
    EXPECT_EQ(countA, 2);
    EXPECT_EQ(countB, 2);
}

TEST_F(EventSystemTest, Unsubscribe) {
    int count = 0;
    auto id = m_bus.Subscribe<TestEvent>([&](const TestEvent&) { count++; });
    m_bus.Publish(TestEvent{});
    EXPECT_EQ(count, 1);

    m_bus.Unsubscribe<TestEvent>(id);
    m_bus.Publish(TestEvent{});
    EXPECT_EQ(count, 1);
}

TEST_F(EventSystemTest, DifferentEventTypesAreIsolated) {
    int testCount = 0;
    std::string message;

    m_bus.Subscribe<TestEvent>([&](const TestEvent& e) { testCount = e.value; });
    m_bus.Subscribe<AnotherEvent>([&](const AnotherEvent& e) { message = e.message; });

    TestEvent te;
    te.value = 99;
    m_bus.Publish(te);

    AnotherEvent ae;
    ae.message = "hello";
    m_bus.Publish(ae);

    EXPECT_EQ(testCount, 99);
    EXPECT_EQ(message, "hello");
}

TEST_F(EventSystemTest, ClearRemovesAllHandlers) {
    int count = 0;
    m_bus.Subscribe<TestEvent>([&](const TestEvent&) { count++; });
    m_bus.Publish(TestEvent{});
    EXPECT_EQ(count, 1);

    m_bus.Clear();
    m_bus.Publish(TestEvent{});
    EXPECT_EQ(count, 1);
}

TEST_F(EventSystemTest, PublishWithNoSubscribersIsNoOp) {
    // Should not crash
    m_bus.Publish(TestEvent{});
    m_bus.Publish(AnotherEvent{});
}

TEST_F(EventSystemTest, GlobalEventBusSingleton) {
    auto& busA = GetGlobalEventBus();
    auto& busB = GetGlobalEventBus();
    EXPECT_EQ(&busA, &busB);

    int count = 0;
    auto id = busA.Subscribe<TestEvent>([&](const TestEvent&) { count++; });
    busB.Publish(TestEvent{});
    EXPECT_EQ(count, 1);
    busA.Unsubscribe<TestEvent>(id);
}
