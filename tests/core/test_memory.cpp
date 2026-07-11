#include <gtest/gtest.h>
#include "engine/core/memory/linear_allocator.h"
#include "engine/core/memory/stack_allocator.h"
#include "engine/core/memory/pool_allocator.h"
#include "engine/core/memory/tlsf_allocator.h"
#include <vector>
#include <cstring>
#include <memory>

using namespace nge;

// ─── LinearAllocator Tests ───────────────────────────────────────────────

TEST(LinearAllocator, BasicAllocation) {
    alignas(64) byte buffer[4096];
    LinearAllocator alloc(buffer, 4096, "TestLinear");

    void* p1 = alloc.Allocate(100);
    EXPECT_NE(p1, nullptr);
    EXPECT_EQ(alloc.GetAllocatedSize(), 100u);

    void* p2 = alloc.Allocate(200);
    EXPECT_NE(p2, nullptr);
    EXPECT_NE(p1, p2);
}

TEST(LinearAllocator, Reset) {
    alignas(64) byte buffer[1024];
    LinearAllocator alloc(buffer, 1024);

    (void)alloc.Allocate(512);
    EXPECT_EQ(alloc.GetAllocatedSize(), 512u);

    alloc.Reset();
    EXPECT_EQ(alloc.GetAllocatedSize(), 0u);
}

TEST(LinearAllocator, OutOfMemory) {
    alignas(64) byte buffer[128];
    LinearAllocator alloc(buffer, 128);

    void* p1 = alloc.Allocate(64);
    EXPECT_NE(p1, nullptr);

    void* p2 = alloc.Allocate(128); // Won't fit
    EXPECT_EQ(p2, nullptr);
}

TEST(LinearAllocator, Alignment) {
    alignas(64) byte buffer[4096];
    LinearAllocator alloc(buffer, 4096);

    (void)alloc.Allocate(1); // 1 byte
    void* p = alloc.Allocate(16, 16); // Request 16-byte alignment
    EXPECT_TRUE(IsAligned(reinterpret_cast<usize>(p), 16));
}

// ─── StackAllocator Tests ────────────────────────────────────────────────

TEST(StackAllocator, MarkerRestore) {
    alignas(64) byte buffer[4096];
    StackAllocator alloc(buffer, 4096);

    auto marker = alloc.GetMarker();
    (void)alloc.Allocate(256);
    (void)alloc.Allocate(256);
    EXPECT_EQ(alloc.GetAllocatedSize(), 512u);

    alloc.FreeToMarker(marker);
    EXPECT_EQ(alloc.GetAllocatedSize(), 0u);
}

TEST(StackAllocator, ScopeGuard) {
    alignas(64) byte buffer[4096];
    StackAllocator alloc(buffer, 4096);

    (void)alloc.Allocate(100);
    {
        StackScope scope(alloc);
        (void)alloc.Allocate(200);
        (void)alloc.Allocate(300);
        // ~StackScope restores to before the block
    }

    // Should be back to 100 (with alignment rounding, close to 100)
    EXPECT_LE(alloc.GetAllocatedSize(), 128u);
}

// ─── PoolAllocator Tests ─────────────────────────────────────────────────

TEST(PoolAllocator, AllocFree) {
    alignas(64) byte buffer[4096];
    PoolAllocator<64> alloc(buffer, 4096, "TestPool");

    void* p1 = alloc.Allocate(64, 64);
    EXPECT_NE(p1, nullptr);

    void* p2 = alloc.Allocate(64, 64);
    EXPECT_NE(p2, nullptr);
    EXPECT_NE(p1, p2);

    alloc.Free(p1);
    void* p3 = alloc.Allocate(64, 64);
    EXPECT_EQ(p3, p1); // Reuses freed block
}

TEST(PoolAllocator, ExhaustAndReset) {
    alignas(64) byte buffer[512]; // ~8 blocks of 64 bytes
    PoolAllocator<64> alloc(buffer, 512);

    std::vector<void*> ptrs;
    while (true) {
        void* p = alloc.Allocate(64, 64);
        if (!p) break;
        ptrs.push_back(p);
    }

    EXPECT_GT(ptrs.size(), 0u);
    EXPECT_EQ(alloc.GetFreeCount(), 0u);

    alloc.Reset();
    EXPECT_EQ(alloc.GetFreeCount(), alloc.GetBlockCount());
}

// ─── TLSFAllocator Tests ─────────────────────────────────────────────────

TEST(TLSFAllocator, BasicAllocFree) {
    alignas(64) byte buffer[65536];
    TLSFAllocator alloc(buffer, 65536, "TestTLSF");

    void* p1 = alloc.Allocate(128);
    EXPECT_NE(p1, nullptr);

    void* p2 = alloc.Allocate(256);
    EXPECT_NE(p2, nullptr);

    alloc.Free(p1);
    alloc.Free(p2);
}

TEST(TLSFAllocator, ManyAllocations) {
    auto bufferStorage = std::make_unique<std::byte[]>(1024 * 1024);
    void* buffer = bufferStorage.get();
    alignas(64) std::byte* alignedBuffer = static_cast<std::byte*>(buffer);
    TLSFAllocator alloc(alignedBuffer, 1024 * 1024);

    std::vector<void*> ptrs;
    for (int i = 0; i < 100; ++i) {
        void* p = alloc.Allocate(64 + i * 8);
        EXPECT_NE(p, nullptr);
        std::memset(p, 0xAB, 64 + i * 8); // Write to check no overlap
        ptrs.push_back(p);
    }

    // Free every other allocation
    for (usize i = 0; i < ptrs.size(); i += 2) {
        alloc.Free(ptrs[i]);
    }

    // Allocate again — should reuse freed blocks (coalesced)
    for (int i = 0; i < 50; ++i) {
        void* p = alloc.Allocate(128);
        EXPECT_NE(p, nullptr);
    }
}

TEST(TLSFAllocator, Reset) {
    alignas(64) byte buffer[65536];
    TLSFAllocator alloc(buffer, 65536);

    (void)alloc.Allocate(1024);
    (void)alloc.Allocate(2048);
    EXPECT_GT(alloc.GetAllocatedSize(), 0u);

    alloc.Reset();
    EXPECT_EQ(alloc.GetAllocatedSize(), 0u);

    // Should be able to allocate the full buffer again
    void* p = alloc.Allocate(60000);
    EXPECT_NE(p, nullptr);
}
