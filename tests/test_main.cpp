#include <gtest/gtest.h>
#include "engine/core/logging/log.h"

class EngineTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        nge::Log::Init();
    }

    void TearDown() override {
        nge::Log::Shutdown();
    }
};

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new EngineTestEnvironment());
    return RUN_ALL_TESTS();
}
