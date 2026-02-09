#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_aliasing_validator.h"

using namespace nge::rhi;

TEST(AliasingValidator, InitAndShutdown) {
    AliasingValidator validator;
    EXPECT_TRUE(validator.Init());

    auto stats = validator.GetStats();
    EXPECT_EQ(stats.totalResources, 0u);
    EXPECT_EQ(stats.violationsDetected, 0u);

    validator.Shutdown();
}

TEST(AliasingValidator, NoResourcesValidates) {
    AliasingValidator validator;
    validator.Init();

    EXPECT_TRUE(validator.Validate());
    EXPECT_TRUE(validator.GetViolations().empty());

    validator.Shutdown();
}

TEST(AliasingValidator, NonOverlappingMemoryPasses) {
    AliasingValidator validator;
    validator.Init();

    // Two transient resources at different memory offsets, same pass range
    ResourceAllocation a;
    a.resourceId = 1; a.name = "RT_A";
    a.region = {0, 1024, 0};
    a.firstUsePass = 0; a.lastUsePass = 3;
    a.isTransient = true;

    ResourceAllocation b;
    b.resourceId = 2; b.name = "RT_B";
    b.region = {1024, 1024, 0}; // Adjacent, no overlap
    b.firstUsePass = 0; b.lastUsePass = 3;
    b.isTransient = true;

    validator.RegisterResource(a);
    validator.RegisterResource(b);

    EXPECT_TRUE(validator.Validate());
    EXPECT_TRUE(validator.GetViolations().empty());

    validator.Shutdown();
}

TEST(AliasingValidator, OverlappingMemoryNonOverlappingLifetime) {
    AliasingValidator validator;
    validator.Init();

    // Same memory region, but non-overlapping pass lifetimes → valid aliasing
    ResourceAllocation a;
    a.resourceId = 1; a.name = "TempA";
    a.region = {0, 2048, 0};
    a.firstUsePass = 0; a.lastUsePass = 2;
    a.isTransient = true;

    ResourceAllocation b;
    b.resourceId = 2; b.name = "TempB";
    b.region = {0, 2048, 0}; // Same region
    b.firstUsePass = 3; b.lastUsePass = 5; // After A is done
    b.isTransient = true;

    validator.RegisterResource(a);
    validator.RegisterResource(b);

    EXPECT_TRUE(validator.Validate());
    EXPECT_TRUE(validator.GetViolations().empty());

    auto stats = validator.GetStats();
    EXPECT_EQ(stats.aliasedPairs, 1u);
    EXPECT_GT(stats.aliasedMemorySaved, 0u);

    validator.Shutdown();
}

TEST(AliasingValidator, DetectsViolation) {
    AliasingValidator validator;
    validator.Init();

    // Same memory + overlapping lifetimes → violation
    ResourceAllocation a;
    a.resourceId = 1; a.name = "GBufferA";
    a.region = {0, 4096, 0};
    a.firstUsePass = 0; a.lastUsePass = 4;
    a.isTransient = true;

    ResourceAllocation b;
    b.resourceId = 2; b.name = "GBufferB";
    b.region = {2048, 4096, 0}; // Overlaps [2048, 4096)
    b.firstUsePass = 2; b.lastUsePass = 6; // Overlaps passes 2-4
    b.isTransient = true;

    validator.RegisterResource(a);
    validator.RegisterResource(b);

    EXPECT_FALSE(validator.Validate());
    EXPECT_EQ(validator.GetViolations().size(), 1u);

    const auto& v = validator.GetViolations()[0];
    EXPECT_EQ(v.resourceA, 1u);
    EXPECT_EQ(v.resourceB, 2u);
    EXPECT_EQ(v.overlapOffset, 2048u);
    EXPECT_EQ(v.overlapSize, 2048u);
    EXPECT_FALSE(v.message.empty());

    validator.Shutdown();
}

TEST(AliasingValidator, DifferentHeapsNoOverlap) {
    AliasingValidator validator;
    validator.Init();

    // Same offset/size but different heaps → no overlap
    ResourceAllocation a;
    a.resourceId = 1; a.name = "HeapA_Res";
    a.region = {0, 1024, 0}; // Heap 0
    a.firstUsePass = 0; a.lastUsePass = 5;
    a.isTransient = true;

    ResourceAllocation b;
    b.resourceId = 2; b.name = "HeapB_Res";
    b.region = {0, 1024, 1}; // Heap 1
    b.firstUsePass = 0; b.lastUsePass = 5;
    b.isTransient = true;

    validator.RegisterResource(a);
    validator.RegisterResource(b);

    EXPECT_TRUE(validator.Validate());

    validator.Shutdown();
}

TEST(AliasingValidator, NonTransientIgnored) {
    AliasingValidator validator;
    validator.Init();

    // Non-transient resources are not checked for aliasing
    ResourceAllocation a;
    a.resourceId = 1; a.name = "Persistent";
    a.region = {0, 4096, 0};
    a.firstUsePass = 0; a.lastUsePass = 10;
    a.isTransient = false; // Not transient

    ResourceAllocation b;
    b.resourceId = 2; b.name = "Transient";
    b.region = {0, 4096, 0};
    b.firstUsePass = 0; b.lastUsePass = 10;
    b.isTransient = true;

    validator.RegisterResource(a);
    validator.RegisterResource(b);

    // Only one transient, so no pair to check
    EXPECT_TRUE(validator.Validate());

    validator.Shutdown();
}

TEST(AliasingValidator, MarkUsedInPassUpdatesLifetime) {
    AliasingValidator validator;
    validator.Init();

    ResourceAllocation a;
    a.resourceId = 1; a.name = "Dynamic";
    a.region = {0, 1024, 0};
    a.firstUsePass = UINT32_MAX; a.lastUsePass = 0;
    a.isTransient = true;

    validator.RegisterResource(a);
    validator.MarkUsedInPass(1, 2);
    validator.MarkUsedInPass(1, 5);
    validator.MarkUsedInPass(1, 3);

    // Now register an overlapping resource in pass 4 — should conflict
    ResourceAllocation b;
    b.resourceId = 2; b.name = "Overlapper";
    b.region = {0, 1024, 0};
    b.firstUsePass = 4; b.lastUsePass = 4;
    b.isTransient = true;

    validator.RegisterResource(b);

    EXPECT_FALSE(validator.Validate()); // Pass 4 is within [2,5]

    validator.Shutdown();
}

TEST(AliasingValidator, CheckPairOverlap) {
    AliasingValidator validator;
    validator.Init();

    ResourceAllocation a;
    a.resourceId = 10; a.name = "A";
    a.region = {0, 512, 0};
    a.firstUsePass = 0; a.lastUsePass = 2;
    a.isTransient = true;

    ResourceAllocation b;
    b.resourceId = 20; b.name = "B";
    b.region = {256, 512, 0}; // Overlaps memory
    b.firstUsePass = 1; b.lastUsePass = 3; // Overlaps lifetime
    b.isTransient = true;

    ResourceAllocation c;
    c.resourceId = 30; c.name = "C";
    c.region = {256, 512, 0}; // Overlaps memory with A
    c.firstUsePass = 5; c.lastUsePass = 7; // No lifetime overlap with A
    c.isTransient = true;

    validator.RegisterResource(a);
    validator.RegisterResource(b);
    validator.RegisterResource(c);

    EXPECT_TRUE(validator.CheckPairOverlap(10, 20));  // Both overlap
    EXPECT_FALSE(validator.CheckPairOverlap(10, 30)); // Memory overlaps but lifetime doesn't
    EXPECT_FALSE(validator.CheckPairOverlap(10, 99)); // Unknown resource

    validator.Shutdown();
}

TEST(AliasingValidator, ResetClearsState) {
    AliasingValidator validator;
    validator.Init();

    ResourceAllocation a;
    a.resourceId = 1; a.name = "X";
    a.region = {0, 1024, 0};
    a.firstUsePass = 0; a.lastUsePass = 5;
    a.isTransient = true;

    validator.RegisterResource(a);
    EXPECT_EQ(validator.GetStats().totalResources, 1u);

    validator.Reset();
    EXPECT_EQ(validator.GetStats().totalResources, 0u);
    EXPECT_TRUE(validator.GetViolations().empty());

    validator.Shutdown();
}

TEST(AliasingValidator, DisabledSkipsValidation) {
    AliasingValidator validator;
    AliasingValidatorConfig config;
    config.enabled = false;
    validator.Init(config);

    // Register a clear violation
    ResourceAllocation a;
    a.resourceId = 1; a.name = "A";
    a.region = {0, 1024, 0};
    a.firstUsePass = 0; a.lastUsePass = 5;
    a.isTransient = true;

    ResourceAllocation b;
    b.resourceId = 2; b.name = "B";
    b.region = {0, 1024, 0};
    b.firstUsePass = 0; b.lastUsePass = 5;
    b.isTransient = true;

    validator.RegisterResource(a);
    validator.RegisterResource(b);

    // Disabled — should pass regardless
    EXPECT_TRUE(validator.Validate());
    EXPECT_TRUE(validator.GetViolations().empty());

    validator.Shutdown();
}

TEST(AliasingValidator, StatsTracking) {
    AliasingValidator validator;
    validator.Init();

    // 3 transients: A=[0,2048] pass 0-2, B=[0,2048] pass 3-5 (valid alias), C=[1024,2048] pass 1-4 (violation with A)
    ResourceAllocation a;
    a.resourceId = 1; a.name = "A";
    a.region = {0, 2048, 0};
    a.firstUsePass = 0; a.lastUsePass = 2;
    a.isTransient = true;

    ResourceAllocation b;
    b.resourceId = 2; b.name = "B";
    b.region = {0, 2048, 0};
    b.firstUsePass = 3; b.lastUsePass = 5;
    b.isTransient = true;

    ResourceAllocation c;
    c.resourceId = 3; c.name = "C";
    c.region = {1024, 2048, 0};
    c.firstUsePass = 1; c.lastUsePass = 4;
    c.isTransient = true;

    validator.RegisterResource(a);
    validator.RegisterResource(b);
    validator.RegisterResource(c);

    validator.Validate();

    auto stats = validator.GetStats();
    EXPECT_EQ(stats.totalResources, 3u);
    EXPECT_EQ(stats.transientResources, 3u);
    EXPECT_GT(stats.violationsDetected, 0u); // A-C overlap
    EXPECT_GT(stats.totalMemoryTracked, 0u);

    validator.Shutdown();
}
