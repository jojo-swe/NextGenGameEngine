#pragma once

#include "engine/core/types.h"
#include <atomic>
#include <functional>
#include <thread>
#include <vector>
#include <condition_variable>
#include <mutex>

namespace nge::jobs {

// ─── Job Handle ──────────────────────────────────────────────────────────
struct JobHandle {
    std::atomic<i32>* counter = nullptr;

    bool IsComplete() const {
        return counter == nullptr || counter->load(std::memory_order_acquire) <= 0;
    }
};

// ─── Job Declaration ─────────────────────────────────────────────────────
using JobFunction = std::function<void()>;

struct Job {
    JobFunction   function;
    std::atomic<i32>* counter = nullptr;
};

// ─── Chase-Lev Work-Stealing Deque ───────────────────────────────────────
// Lock-free deque: owning thread pushes/pops from bottom; thieves steal from top.
class WorkStealingDeque {
    static constexpr usize INITIAL_CAPACITY = 4096;

public:
    WorkStealingDeque();
    ~WorkStealingDeque();

    void Push(Job job);
    bool Pop(Job& job);      // Owner pops from bottom
    bool Steal(Job& job);    // Thief steals from top

    usize Size() const;

private:
    struct CircularArray {
        usize       capacity;
        Job*        buffer;

        CircularArray(usize cap)
            : capacity(cap)
            , buffer(new Job[cap])
        {}

        ~CircularArray() { delete[] buffer; }

        Job& Get(i64 index) { return buffer[index & (capacity - 1)]; }
        void Put(i64 index, Job job) { buffer[index & (capacity - 1)] = std::move(job); }

        CircularArray* Grow(i64 bottom, i64 top) {
            auto* newArr = new CircularArray(capacity * 2);
            for (i64 i = top; i < bottom; ++i) {
                newArr->Put(i, Get(i));
            }
            return newArr;
        }
    };

    alignas(CACHE_LINE_SIZE) std::atomic<i64> m_bottom{0};
    alignas(CACHE_LINE_SIZE) std::atomic<i64> m_top{0};
    std::atomic<CircularArray*> m_array;
    std::vector<CircularArray*> m_garbage; // Old arrays to clean up
};

// ─── Job System ──────────────────────────────────────────────────────────
// One worker thread per hardware core, each with its own work-stealing deque.
class JobSystem {
public:
    static void Init(u32 numThreads = 0); // 0 = auto-detect
    static void Shutdown();

    // Submit a single job, returns handle for waiting
    static JobHandle Submit(JobFunction fn);

    // Submit a batch of jobs sharing a single counter
    static JobHandle SubmitBatch(JobFunction* functions, u32 count);

    // Parallel for: splits [0, count) across worker threads
    static JobHandle ParallelFor(u32 count, std::function<void(u32 begin, u32 end)> fn);

    // Wait for a job to complete (processes other jobs while waiting)
    static void Wait(JobHandle handle);

    // Get number of worker threads
    static u32 GetWorkerCount();

private:
    static void WorkerThread(u32 threadIndex);
    static bool TryExecuteOne(u32 threadIndex);

    inline static std::vector<std::thread>         s_workers;
    inline static std::vector<WorkStealingDeque*>   s_deques;
    inline static std::atomic<bool>                 s_running{false};
    inline static std::atomic<u32>                  s_nextDeque{0};
    inline static u32                               s_workerCount = 0;

    // Notification
    inline static std::mutex              s_wakeMutex;
    inline static std::condition_variable s_wakeCondition;
    inline static std::atomic<u32>        s_jobCount{0};
};

} // namespace nge::jobs
