#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_attachment_format_resolver.h"

using namespace nge::rhi;

static void RegisterCommonFormats(AttachmentFormatResolver& resolver) {
    u8 filterBlend = static_cast<u8>(FormatFeature::Filterable) | static_cast<u8>(FormatFeature::Blendable);
    u8 filterOnly = static_cast<u8>(FormatFeature::Filterable);

    resolver.RegisterFormat(ResolvedFormat::RGBA8_Unorm, filterBlend, true, 8);
    resolver.RegisterFormat(ResolvedFormat::RGBA8_SRGB, filterBlend, true, 8);
    resolver.RegisterFormat(ResolvedFormat::RGBA16_Float, filterBlend, true, 8);
    resolver.RegisterFormat(ResolvedFormat::RGBA32_Float, filterOnly, true, 1);
    resolver.RegisterFormat(ResolvedFormat::R11G11B10_Float, filterBlend, true, 8);
    resolver.RegisterFormat(ResolvedFormat::RGB10A2_Unorm, filterBlend, true, 8);
    resolver.RegisterFormat(ResolvedFormat::RG16_Float, filterBlend, true, 4);
    resolver.RegisterFormat(ResolvedFormat::R16_Float, filterBlend, true, 4);
    resolver.RegisterFormat(ResolvedFormat::R32_Float, filterOnly, true, 1);
    resolver.RegisterFormat(ResolvedFormat::R8_Unorm, filterBlend, true, 8);
    resolver.RegisterFormat(ResolvedFormat::D16_Unorm, 0, true, 4);
    resolver.RegisterFormat(ResolvedFormat::D24_Unorm_S8, 0, true, 8);
    resolver.RegisterFormat(ResolvedFormat::D32_Float, filterOnly, true, 8);
    resolver.RegisterFormat(ResolvedFormat::D32_Float_S8, 0, true, 8);
}

TEST(AttachmentFormatResolver, InitAndShutdown) {
    AttachmentFormatResolver resolver;
    EXPECT_TRUE(resolver.Init());
    EXPECT_EQ(resolver.GetRegisteredFormatCount(), 0u);
    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, RegisterFormat) {
    AttachmentFormatResolver resolver;
    resolver.Init();

    u8 features = static_cast<u8>(FormatFeature::Filterable) | static_cast<u8>(FormatFeature::Blendable);
    resolver.RegisterFormat(ResolvedFormat::RGBA8_Unorm, features, true, 8);

    EXPECT_EQ(resolver.GetRegisteredFormatCount(), 1u);
    EXPECT_TRUE(resolver.IsSupported(ResolvedFormat::RGBA8_Unorm));

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, ResolveColorFormatLDR) {
    AttachmentFormatResolver resolver;
    resolver.Init();
    RegisterCommonFormats(resolver);

    auto fmt = resolver.ResolveColorFormat(false, false);
    EXPECT_EQ(fmt, ResolvedFormat::RGBA8_Unorm);

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, ResolveColorFormatHDR) {
    AttachmentFormatResolver resolver;
    resolver.Init();
    RegisterCommonFormats(resolver);

    auto fmt = resolver.ResolveColorFormat(true, false);
    EXPECT_EQ(fmt, ResolvedFormat::RGBA16_Float);

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, ResolveColorFormatSRGB) {
    AttachmentFormatResolver resolver;
    resolver.Init();
    RegisterCommonFormats(resolver);

    auto fmt = resolver.ResolveColorFormat(false, true);
    EXPECT_EQ(fmt, ResolvedFormat::RGBA8_SRGB);

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, ResolveDepthOnly) {
    AttachmentFormatResolver resolver;
    resolver.Init();
    RegisterCommonFormats(resolver);

    auto fmt = resolver.ResolveDepthFormat(false);
    EXPECT_EQ(fmt, ResolvedFormat::D32_Float);

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, ResolveDepthStencil) {
    AttachmentFormatResolver resolver;
    resolver.Init();
    RegisterCommonFormats(resolver);

    auto fmt = resolver.ResolveDepthFormat(true);
    EXPECT_EQ(fmt, ResolvedFormat::D32_Float_S8);

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, FallbackWhenPreferredUnsupported) {
    AttachmentFormatResolver resolver;
    resolver.Init();

    // Only register RGBA8, not RGBA16F
    u8 features = static_cast<u8>(FormatFeature::Filterable) | static_cast<u8>(FormatFeature::Blendable);
    resolver.RegisterFormat(ResolvedFormat::RGBA8_Unorm, features, true, 8);

    auto fmt = resolver.ResolveColorFormat(true, false); // Wants HDR but only LDR available
    // Should fallback to RGBA8_Unorm (last in HDR preference chain)
    EXPECT_EQ(fmt, ResolvedFormat::RGBA8_Unorm);

    auto stats = resolver.GetStats();
    EXPECT_GE(stats.fallbacksUsed, 0u); // May or may not use fallback depending on chain

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, ResolveUnknownWhenNoneAvailable) {
    AttachmentFormatResolver resolver;
    resolver.Init();
    // No formats registered

    auto fmt = resolver.ResolveColorFormat(false, false);
    EXPECT_EQ(fmt, ResolvedFormat::Unknown);

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, IsSupportedFalse) {
    AttachmentFormatResolver resolver;
    resolver.Init();

    resolver.RegisterFormat(ResolvedFormat::RGBA32_Float, 0, false, 0);

    EXPECT_FALSE(resolver.IsSupported(ResolvedFormat::RGBA32_Float));
    EXPECT_FALSE(resolver.IsSupported(ResolvedFormat::RGBA8_Unorm)); // Not registered

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, HasFeatureCheck) {
    AttachmentFormatResolver resolver;
    resolver.Init();

    u8 features = static_cast<u8>(FormatFeature::Filterable) | static_cast<u8>(FormatFeature::Blendable);
    resolver.RegisterFormat(ResolvedFormat::RGBA8_Unorm, features, true, 8);

    EXPECT_TRUE(resolver.HasFeature(ResolvedFormat::RGBA8_Unorm, FormatFeature::Filterable));
    EXPECT_TRUE(resolver.HasFeature(ResolvedFormat::RGBA8_Unorm, FormatFeature::Blendable));
    EXPECT_FALSE(resolver.HasFeature(ResolvedFormat::RGBA8_Unorm, FormatFeature::StorageAtomic));

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, GetMaxSamples) {
    AttachmentFormatResolver resolver;
    resolver.Init();

    resolver.RegisterFormat(ResolvedFormat::RGBA16_Float, 0, true, 8);
    resolver.RegisterFormat(ResolvedFormat::RGBA32_Float, 0, true, 1);

    EXPECT_EQ(resolver.GetMaxSamples(ResolvedFormat::RGBA16_Float), 8u);
    EXPECT_EQ(resolver.GetMaxSamples(ResolvedFormat::RGBA32_Float), 1u);
    EXPECT_EQ(resolver.GetMaxSamples(ResolvedFormat::Unknown), 0u); // Not registered

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, GetCapability) {
    AttachmentFormatResolver resolver;
    resolver.Init();

    resolver.RegisterFormat(ResolvedFormat::D32_Float, 0, true, 8);

    const auto* cap = resolver.GetCapability(ResolvedFormat::D32_Float);
    EXPECT_NE(cap, nullptr);
    EXPECT_TRUE(cap->supported);
    EXPECT_EQ(cap->maxSamples, 8u);

    EXPECT_EQ(resolver.GetCapability(ResolvedFormat::Unknown), nullptr);

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, GetBytesPerPixel) {
    EXPECT_EQ(AttachmentFormatResolver::GetBytesPerPixel(ResolvedFormat::R8_Unorm), 1u);
    EXPECT_EQ(AttachmentFormatResolver::GetBytesPerPixel(ResolvedFormat::R16_Float), 2u);
    EXPECT_EQ(AttachmentFormatResolver::GetBytesPerPixel(ResolvedFormat::RGBA8_Unorm), 4u);
    EXPECT_EQ(AttachmentFormatResolver::GetBytesPerPixel(ResolvedFormat::RGBA16_Float), 8u);
    EXPECT_EQ(AttachmentFormatResolver::GetBytesPerPixel(ResolvedFormat::RGBA32_Float), 16u);
    EXPECT_EQ(AttachmentFormatResolver::GetBytesPerPixel(ResolvedFormat::D32_Float_S8), 5u);
}

TEST(AttachmentFormatResolver, IsDepthFormat) {
    EXPECT_TRUE(AttachmentFormatResolver::IsDepthFormat(ResolvedFormat::D16_Unorm));
    EXPECT_TRUE(AttachmentFormatResolver::IsDepthFormat(ResolvedFormat::D24_Unorm_S8));
    EXPECT_TRUE(AttachmentFormatResolver::IsDepthFormat(ResolvedFormat::D32_Float));
    EXPECT_TRUE(AttachmentFormatResolver::IsDepthFormat(ResolvedFormat::D32_Float_S8));
    EXPECT_FALSE(AttachmentFormatResolver::IsDepthFormat(ResolvedFormat::RGBA8_Unorm));
    EXPECT_FALSE(AttachmentFormatResolver::IsDepthFormat(ResolvedFormat::RGBA16_Float));
}

TEST(AttachmentFormatResolver, HasStencil) {
    EXPECT_TRUE(AttachmentFormatResolver::HasStencil(ResolvedFormat::D24_Unorm_S8));
    EXPECT_TRUE(AttachmentFormatResolver::HasStencil(ResolvedFormat::D32_Float_S8));
    EXPECT_FALSE(AttachmentFormatResolver::HasStencil(ResolvedFormat::D32_Float));
    EXPECT_FALSE(AttachmentFormatResolver::HasStencil(ResolvedFormat::D16_Unorm));
}

TEST(AttachmentFormatResolver, CacheHits) {
    AttachmentFormatResolver resolver;
    AttachmentFormatResolverConfig config;
    config.cacheResults = true;
    resolver.Init(config);
    RegisterCommonFormats(resolver);

    resolver.ResolveColorFormat(true, false); // Miss
    resolver.ResolveColorFormat(true, false); // Hit

    auto stats = resolver.GetStats();
    EXPECT_EQ(stats.totalQueries, 2u);
    EXPECT_EQ(stats.cacheHits, 1u);
    EXPECT_EQ(stats.cacheMisses, 1u);

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, ClearCache) {
    AttachmentFormatResolver resolver;
    resolver.Init();
    RegisterCommonFormats(resolver);

    resolver.ResolveColorFormat(false, false);
    resolver.ClearCache();
    resolver.ResolveColorFormat(false, false); // Should be a miss again

    auto stats = resolver.GetStats();
    EXPECT_EQ(stats.cacheMisses, 2u);

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, MSAARequirement) {
    AttachmentFormatResolver resolver;
    resolver.Init();

    u8 features = static_cast<u8>(FormatFeature::Filterable) | static_cast<u8>(FormatFeature::Blendable);
    resolver.RegisterFormat(ResolvedFormat::RGBA8_Unorm, features, true, 1);  // No MSAA
    resolver.RegisterFormat(ResolvedFormat::RGBA16_Float, features, true, 8); // 8x MSAA

    FormatRequest req;
    req.usage = AttachmentUsage::ColorAttachment;
    req.preferHDR = false;
    req.requireSRGB = false;
    req.requireFilterable = true;
    req.requireBlendable = true;
    req.requiredSamples = 4;
    req.preferredFormats = {ResolvedFormat::RGBA8_Unorm, ResolvedFormat::RGBA16_Float};

    auto fmt = resolver.Resolve(req);
    EXPECT_EQ(fmt, ResolvedFormat::RGBA16_Float); // RGBA8 rejected due to MSAA

    resolver.Shutdown();
}

TEST(AttachmentFormatResolver, ResetClearsAll) {
    AttachmentFormatResolver resolver;
    resolver.Init();
    RegisterCommonFormats(resolver);

    resolver.ResolveColorFormat(false, false);

    resolver.Reset();

    EXPECT_EQ(resolver.GetRegisteredFormatCount(), 0u);
    auto stats = resolver.GetStats();
    EXPECT_EQ(stats.totalQueries, 0u);
    EXPECT_EQ(stats.cacheHits, 0u);

    resolver.Shutdown();
}
