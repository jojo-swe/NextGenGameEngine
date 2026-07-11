#include <gtest/gtest.h>
#include "engine/core/jobs/job_system.h"
#include <atomic>
#include <vector>
#include <numeric>

using namespace nge;
using namespace nge::jobs;

class JobSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        JobSystem::Init(4);
    }
    void TearDown() override {
        JobSystem::Shutdown();
    }
};

TEST_F(JobSystemTest, SingleJob) {
    std::atomic<int> counter{0};
    auto handle = JobSystem::Submit([&]() {
        counter.fetch_add(1, std::memory_order_relaxed);
    });
    JobSystem::Wait(handle);
    EXPECT_EQ(counter.load(), 1);
}

TEST_F(JobSystemTest, ManyJobs) {
    constexpr int N = 1000;
    std::atomic<int> counter{0};

    std::vector<JobFunction> functions(N);
    for (int i = 0; i < N; ++i) {
        functions[i] = [&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        };
    }

    auto handle = JobSystem::SubmitBatch(functions.data(), N);
    JobSystem::Wait(handle);
    EXPECT_EQ(counter.load(), N);
}

TEST_F(JobSystemTest, ParallelFor) {
    constexpr int N = 10000;
    std::vector<int> data(N, 0);

    auto handle = JobSystem::ParallelFor(N, [&](u32 begin, u32 end) {
        for (u32 i = begin; i < end; ++i) {
            data[i] = static_cast<int>(i * 2);
        }
    });
    JobSystem::Wait(handle);

    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(data[i], i * 2);
    }
}

TEST_F(JobSystemTest, ParallelForAccumulate) {
    constexpr int N = 100000;
    std::vector<int> data(N);
    std::iota(data.begin(), data.end(), 1); // 1, 2, 3, ..., N

    std::atomic<i64> sum{0};
    auto handle = JobSystem::ParallelFor(N, [&](u32 begin, u32 end) {
        i64 localSum = 0;
        for (u32 i = begin; i < end; ++i) {
            localSum += data[i];
        }
        sum.fetch_add(localSum, std::memory_order_relaxed);
    });
    JobSystem::Wait(handle);

    i64 expected = static_cast<i64>(N) * (N + 1) / 2;
    EXPECT_EQ(sum.load(), expected);
}

TEST_F(JobSystemTest, WorkerCount) {
    EXPECT_EQ(JobSystem::GetWorkerCount(), 4u);
}

// ─── WorkStealingDeque Tests ─────────────────────────────────────────────

TEST(WorkStealingDeque, PushPop) {
    WorkStealingDeque deque;
    std::atomic<int> counter{0};

    // The deque holds heap-allocated jobs; the taker owns and deletes them.
    deque.Push(new Job{[&]() { counter++; }, nullptr});
    deque.Push(new Job{[&]() { counter += 10; }, nullptr});
    EXPECT_EQ(deque.Size(), 2u);

    Job* out = nullptr;
    EXPECT_TRUE(deque.Pop(out));
    out->function();
    delete out;
    EXPECT_EQ(counter.load(), 10); // Last pushed (LIFO)

    EXPECT_TRUE(deque.Pop(out));
    out->function();
    delete out;
    EXPECT_EQ(counter.load(), 11);

    EXPECT_FALSE(deque.Pop(out)); // Empty
}

TEST(WorkStealingDeque, Steal) {
    WorkStealingDeque deque;
    std::atomic<int> val{0};

    deque.Push(new Job{[&]() { val = 1; }, nullptr});
    deque.Push(new Job{[&]() { val = 2; }, nullptr});
    deque.Push(new Job{[&]() { val = 3; }, nullptr});

    Job* stolen = nullptr;
    EXPECT_TRUE(deque.Steal(stolen)); // Steals from top (FIFO)
    stolen->function();
    delete stolen;
    EXPECT_EQ(val.load(), 1); // First pushed

    EXPECT_TRUE(deque.Steal(stolen));
    stolen->function();
    delete stolen;
    EXPECT_EQ(val.load(), 2);

    // Drain the remaining job so the deque does not leak it
    Job* last = nullptr;
    EXPECT_TRUE(deque.Pop(last));
    delete last;
}
