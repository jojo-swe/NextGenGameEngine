#include <gtest/gtest.h>
#include "engine/core/containers/array.h"
#include "engine/core/containers/hash_map.h"
#include "engine/core/containers/ring_buffer.h"
#include "engine/core/containers/optional.h"
#include "engine/core/containers/span.h"

using namespace nge;

// ─── Array Tests ─────────────────────────────────────────────────────────

TEST(Array, PushBackAndAccess) {
    Array<int> arr;
    arr.PushBack(10);
    arr.PushBack(20);
    arr.PushBack(30);

    EXPECT_EQ(arr.Size(), 3u);
    EXPECT_EQ(arr[0], 10);
    EXPECT_EQ(arr[1], 20);
    EXPECT_EQ(arr[2], 30);
}

TEST(Array, PopBack) {
    Array<int> arr;
    arr.PushBack(1);
    arr.PushBack(2);
    arr.PopBack();
    EXPECT_EQ(arr.Size(), 1u);
    EXPECT_EQ(arr[0], 1);
}

TEST(Array, SwapRemove) {
    Array<int> arr;
    arr.PushBack(10);
    arr.PushBack(20);
    arr.PushBack(30);

    arr.SwapRemove(0); // Remove 10, replace with 30
    EXPECT_EQ(arr.Size(), 2u);
    EXPECT_EQ(arr[0], 30);
    EXPECT_EQ(arr[1], 20);
}

TEST(Array, Resize) {
    Array<int> arr;
    arr.Resize(5, 42);
    EXPECT_EQ(arr.Size(), 5u);
    for (usize i = 0; i < 5; ++i) EXPECT_EQ(arr[i], 42);

    arr.Resize(2);
    EXPECT_EQ(arr.Size(), 2u);
}

TEST(Array, EmplaceBack) {
    struct Pair { int a; float b; };
    Array<Pair> arr;
    arr.EmplaceBack(Pair{1, 2.0f});
    arr.EmplaceBack(Pair{3, 4.0f});

    EXPECT_EQ(arr[0].a, 1);
    EXPECT_FLOAT_EQ(arr[1].b, 4.0f);
}

TEST(Array, MoveSemantics) {
    Array<int> a;
    a.PushBack(1);
    a.PushBack(2);

    Array<int> b = std::move(a);
    EXPECT_EQ(b.Size(), 2u);
    EXPECT_EQ(a.Size(), 0u);
}

TEST(Array, InitializerList) {
    Array<int> arr({1, 2, 3, 4, 5});
    EXPECT_EQ(arr.Size(), 5u);
    EXPECT_EQ(arr[4], 5);
}

TEST(Array, RangeFor) {
    Array<int> arr({10, 20, 30});
    int sum = 0;
    for (int v : arr) sum += v;
    EXPECT_EQ(sum, 60);
}

// ─── HashMap Tests ───────────────────────────────────────────────────────

TEST(HashMap, InsertAndFind) {
    HashMap<int, std::string> map;
    map.Insert(1, "one");
    map.Insert(2, "two");
    map.Insert(3, "three");

    EXPECT_EQ(map.Size(), 3u);
    EXPECT_NE(map.Find(1), nullptr);
    EXPECT_EQ(*map.Find(1), "one");
    EXPECT_EQ(*map.Find(2), "two");
    EXPECT_EQ(map.Find(99), nullptr);
}

TEST(HashMap, Erase) {
    HashMap<int, int> map;
    map.Insert(1, 100);
    map.Insert(2, 200);

    EXPECT_TRUE(map.Erase(1));
    EXPECT_EQ(map.Size(), 1u);
    EXPECT_EQ(map.Find(1), nullptr);
    EXPECT_NE(map.Find(2), nullptr);
}

TEST(HashMap, OperatorBracket) {
    HashMap<std::string, int> map;
    map["hello"] = 42;
    map["world"] = 99;

    EXPECT_EQ(map["hello"], 42);
    EXPECT_EQ(map["world"], 99);
}

TEST(HashMap, ManyEntries) {
    HashMap<int, int> map;
    for (int i = 0; i < 1000; ++i) {
        map.Insert(i, i * 10);
    }

    EXPECT_EQ(map.Size(), 1000u);
    for (int i = 0; i < 1000; ++i) {
        int* val = map.Find(i);
        EXPECT_NE(val, nullptr);
        EXPECT_EQ(*val, i * 10);
    }
}

TEST(HashMap, Overwrite) {
    HashMap<int, int> map;
    map.Insert(1, 100);
    map.Insert(1, 200);
    EXPECT_EQ(map.Size(), 1u);
    EXPECT_EQ(*map.Find(1), 200);
}

// ─── RingBuffer Tests ────────────────────────────────────────────────────

TEST(RingBuffer, PushPop) {
    RingBuffer<int, 16> rb;
    EXPECT_TRUE(rb.TryPush(42));
    EXPECT_TRUE(rb.TryPush(99));

    int val;
    EXPECT_TRUE(rb.TryPop(val));
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(rb.TryPop(val));
    EXPECT_EQ(val, 99);
    EXPECT_FALSE(rb.TryPop(val));
}

TEST(RingBuffer, Full) {
    RingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.TryPush(1));
    EXPECT_TRUE(rb.TryPush(2));
    EXPECT_TRUE(rb.TryPush(3));
    EXPECT_TRUE(rb.TryPush(4));
    EXPECT_FALSE(rb.TryPush(5)); // Full
    EXPECT_TRUE(rb.IsFull());
}

// ─── Optional Tests ──────────────────────────────────────────────────────

TEST(Optional, Empty) {
    Optional<int> opt;
    EXPECT_FALSE(opt.HasValue());
    EXPECT_FALSE(static_cast<bool>(opt));
}

TEST(Optional, WithValue) {
    Optional<int> opt(42);
    EXPECT_TRUE(opt.HasValue());
    EXPECT_EQ(opt.Value(), 42);
    EXPECT_EQ(*opt, 42);
}

TEST(Optional, ValueOr) {
    Optional<int> empty;
    EXPECT_EQ(empty.ValueOr(99), 99);

    Optional<int> full(42);
    EXPECT_EQ(full.ValueOr(99), 42);
}

TEST(Optional, Reset) {
    Optional<std::string> opt("hello");
    EXPECT_TRUE(opt.HasValue());
    opt.Reset();
    EXPECT_FALSE(opt.HasValue());
}

TEST(Optional, Emplace) {
    Optional<std::string> opt;
    opt.Emplace("test");
    EXPECT_TRUE(opt.HasValue());
    EXPECT_EQ(*opt, "test");
}

// ─── Span Tests ──────────────────────────────────────────────────────────

TEST(Span, FromArray) {
    int data[] = {1, 2, 3, 4, 5};
    Span<int> s(data);

    EXPECT_EQ(s.Size(), 5u);
    EXPECT_EQ(s[0], 1);
    EXPECT_EQ(s[4], 5);
}

TEST(Span, Subspan) {
    int data[] = {10, 20, 30, 40, 50};
    Span<int> s(data);
    auto sub = s.Subspan(1, 3);

    EXPECT_EQ(sub.Size(), 3u);
    EXPECT_EQ(sub[0], 20);
    EXPECT_EQ(sub[2], 40);
}

TEST(Span, RangeFor) {
    int data[] = {1, 2, 3};
    Span<int> s(data);
    int sum = 0;
    for (int v : s) sum += v;
    EXPECT_EQ(sum, 6);
}
