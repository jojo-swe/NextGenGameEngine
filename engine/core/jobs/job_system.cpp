#include "engine/core/jobs/job_system.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <random>

namespace nge::jobs {

// Chase-Lev deques allow Push/Pop only from the owning thread; other threads
// may only Steal. Track which deque the current thread owns: workers own
// index threadIndex (1..N), every other thread (incl. main) uses index 0.
// Contract: Submit/SubmitBatch/Wait must be called from the main thread or
// from inside a job — never from arbitrary foreign threads.
namespace {
thread_local u32 t_ownDeque = 0;
}

// ─── WorkStealingDeque Implementation ────────────────────────────────────

WorkStealingDeque::WorkStealingDeque() {
    m_array.store(new CircularArray(INITIAL_CAPACITY), std::memory_order_relaxed);
}

WorkStealingDeque::~WorkStealingDeque() {
    delete m_array.load();
    for (auto* arr : m_garbage) delete arr;
}

void WorkStealingDeque::Push(Job* job) {
    i64 b = m_bottom.load(std::memory_order_relaxed);
    i64 t = m_top.load(std::memory_order_acquire);
    CircularArray* arr = m_array.load(std::memory_order_relaxed);

    if (b - t >= static_cast<i64>(arr->capacity) - 1) {
        CircularArray* newArr = arr->Grow(b, t);
        m_garbage.push_back(arr);
        // Release so thieves that acquire-load m_array see the copied slots
        m_array.store(newArr, std::memory_order_release);
        arr = newArr;
    }

    arr->Put(b, job);
    std::atomic_thread_fence(std::memory_order_release);
    m_bottom.store(b + 1, std::memory_order_relaxed);
}

bool WorkStealingDeque::Pop(Job*& job) {
    i64 b = m_bottom.load(std::memory_order_relaxed) - 1;
    CircularArray* arr = m_array.load(std::memory_order_relaxed);
    m_bottom.store(b, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    i64 t = m_top.load(std::memory_order_relaxed);

    if (t <= b) {
        // Reading the slot is a relaxed atomic pointer load; the Job itself is
        // only dereferenced by the side that ends up owning it.
        job = arr->Get(b);
        if (t == b) {
            // Last element — race with a thief for it
            if (!m_top.compare_exchange_strong(t, t + 1,
                    std::memory_order_seq_cst, std::memory_order_relaxed)) {
                // Lost: the thief owns the job
                m_bottom.store(b + 1, std::memory_order_relaxed);
                return false;
            }
            m_bottom.store(b + 1, std::memory_order_relaxed);
        }
        return true;
    }

    // Deque was empty
    m_bottom.store(b + 1, std::memory_order_relaxed);
    return false;
}

bool WorkStealingDeque::Steal(Job*& job) {
    i64 t = m_top.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    i64 b = m_bottom.load(std::memory_order_acquire);

    if (t >= b) return false; // Empty

    // Load the pointer BEFORE the CAS; dereference only after winning it.
    CircularArray* arr = m_array.load(std::memory_order_acquire);
    Job* candidate = arr->Get(t);

    if (!m_top.compare_exchange_strong(t, t + 1,
            std::memory_order_seq_cst, std::memory_order_relaxed)) {
        return false; // Lost race with another thief or the owner
    }

    job = candidate;
    return true;
}

usize WorkStealingDeque::Size() const {
    i64 b = m_bottom.load(std::memory_order_relaxed);
    i64 t = m_top.load(std::memory_order_relaxed);
    return static_cast<usize>(b >= t ? b - t : 0);
}

// ─── JobSystem Implementation ────────────────────────────────────────────

void JobSystem::Init(u32 numThreads) {
    if (numThreads == 0) {
        numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    }

    s_workerCount = numThreads;
    s_running.store(true, std::memory_order_relaxed);

    // Create per-thread deques (including main thread at index 0)
    s_deques.resize(numThreads + 1);
    for (u32 i = 0; i <= numThreads; ++i) {
        s_deques[i] = new WorkStealingDeque();
    }

    // Launch worker threads (main thread is index 0)
    s_workers.reserve(numThreads);
    for (u32 i = 0; i < numThreads; ++i) {
        s_workers.emplace_back(WorkerThread, i + 1);
    }

    NGE_LOG_INFO("Job system initialized: {} worker threads", numThreads);
}

void JobSystem::Shutdown() {
    s_running.store(false, std::memory_order_release);
    s_wakeCondition.notify_all();

    for (auto& thread : s_workers) {
        if (thread.joinable()) thread.join();
    }
    s_workers.clear();

    for (auto* deque : s_deques) {
        // Drain unexecuted jobs so their allocations are not leaked
        Job* leftover = nullptr;
        while (deque->Pop(leftover)) {
            if (leftover) delete leftover;
        }
        delete deque;
    }
    s_deques.clear();

    NGE_LOG_INFO("Job system shut down");
}

JobHandle JobSystem::Submit(JobFunction fn) {
    auto* counter = new std::atomic<i32>(1);
    JobHandle handle{counter};

    auto* job = new Job{std::move(fn), counter};

    // Push to the calling thread's own deque (Chase-Lev owner-only Push);
    // idle workers redistribute the load by stealing. The deque owns the
    // Job allocation until an executor takes and deletes it.
    s_deques[t_ownDeque]->Push(job);

    s_jobCount.fetch_add(1, std::memory_order_release);
    s_wakeCondition.notify_one();

    return handle;
}

JobHandle JobSystem::SubmitBatch(JobFunction* functions, u32 count) {
    auto* counter = new std::atomic<i32>(static_cast<i32>(count));
    JobHandle handle{counter};

    for (u32 i = 0; i < count; ++i) {
        auto* job = new Job{std::move(functions[i]), counter};
        s_deques[t_ownDeque]->Push(job);
    }

    s_jobCount.fetch_add(count, std::memory_order_release);
    s_wakeCondition.notify_all();

    return handle;
}

JobHandle JobSystem::ParallelFor(u32 count, std::function<void(u32 begin, u32 end)> fn) {
    if (count == 0) return JobHandle{};

    u32 batchCount = std::min(count, s_workerCount + 1);
    u32 batchSize = (count + batchCount - 1) / batchCount;

    std::vector<JobFunction> jobs(batchCount);
    for (u32 i = 0; i < batchCount; ++i) {
        u32 begin = i * batchSize;
        u32 end = std::min(begin + batchSize, count);
        if (begin >= end) {
            batchCount = i;
            break;
        }
        jobs[i] = [fn, begin, end]() { fn(begin, end); };
    }

    return SubmitBatch(jobs.data(), batchCount);
}

void JobSystem::Wait(JobHandle handle) {
    while (!handle.IsComplete()) {
        // Help process jobs while waiting (pop own deque, steal from others)
        if (!TryExecuteOne(t_ownDeque)) {
            std::this_thread::yield();
        }
    }
    // Clean up counter
    delete handle.counter;
}

u32 JobSystem::GetWorkerCount() { return s_workerCount; }

void JobSystem::WorkerThread(u32 threadIndex) {
    t_ownDeque = threadIndex;
    while (s_running.load(std::memory_order_relaxed)) {
        if (!TryExecuteOne(threadIndex)) {
            // No work found — wait for notification
            std::unique_lock<std::mutex> lock(s_wakeMutex);
            s_wakeCondition.wait_for(lock, std::chrono::microseconds(100), []() {
                return s_jobCount.load(std::memory_order_relaxed) > 0 ||
                       !s_running.load(std::memory_order_relaxed);
            });
        }
    }
}

bool JobSystem::TryExecuteOne(u32 threadIndex) {
    Job* job = nullptr;

    // Try own deque first
    if (s_deques[threadIndex]->Pop(job)) {
        ExecuteJob(job);
        return true;
    }

    // Try stealing from another deque
    u32 totalDeques = s_workerCount + 1;
    u32 start = threadIndex + 1;
    for (u32 i = 0; i < totalDeques - 1; ++i) {
        u32 victim = (start + i) % totalDeques;
        if (s_deques[victim]->Steal(job)) {
            ExecuteJob(job);
            return true;
        }
    }

    return false;
}

void JobSystem::ExecuteJob(Job* job) {
    job->function();
    if (job->counter) {
        job->counter->fetch_sub(1, std::memory_order_release);
    }
    s_jobCount.fetch_sub(1, std::memory_order_relaxed);
    delete job;
}

} // namespace nge::jobs
