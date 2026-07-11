#include <gtest/gtest.h>

#include "editor/core/panel_manager.h"
#include "editor/core/service_container.h"

#include <string>
#include <vector>

using namespace nge;
using namespace nge::editor;

namespace {

class TestPanel final : public EditorPanel {
public:
    explicit TestPanel(const char* name) : m_name(name) {}
    const char* GetName() const override { return m_name.c_str(); }
    void OnDraw() override { ++drawCount; }
    void OnUpdate(f32) override { ++updateCount; }

    int drawCount = 0;
    int updateCount = 0;

private:
    std::string m_name;
};

class TestService final : public EditorService {
public:
    TestService(std::vector<int>& events, int id, bool succeeds = true)
        : m_events(events), m_id(id), m_succeeds(succeeds) {}

    bool Initialize() override {
        m_events.push_back(m_id);
        return m_succeeds;
    }
    void Shutdown() override { m_events.push_back(-m_id); }

private:
    std::vector<int>& m_events;
    int m_id;
    bool m_succeeds;
};

class SecondTestService final : public EditorService {
public:
    SecondTestService(std::vector<int>& events, bool succeeds)
        : m_events(events), m_succeeds(succeeds) {}
    bool Initialize() override {
        m_events.push_back(2);
        return m_succeeds;
    }
    void Shutdown() override { m_events.push_back(-2); }
private:
    std::vector<int>& m_events;
    bool m_succeeds;
};

} // namespace

TEST(PanelManager, RegistersFindsAndRejectsDuplicates) {
    PanelManager panels;
    EXPECT_TRUE(panels.Register(std::make_unique<TestPanel>("Viewport")));
    EXPECT_FALSE(panels.Register(std::make_unique<TestPanel>("Viewport")));
    EXPECT_EQ(panels.Size(), 1u);
    EXPECT_NE(panels.Find("Viewport"), nullptr);
    EXPECT_EQ(panels.Find("Missing"), nullptr);
}

TEST(PanelManager, UpdatesAndDrawsOnlyVisiblePanels) {
    PanelManager panels;
    auto panel = std::make_unique<TestPanel>("Inspector");
    auto* observed = panel.get();
    ASSERT_TRUE(panels.Register(std::move(panel)));

    panels.UpdateVisible(0.016f);
    panels.DrawVisible();
    EXPECT_EQ(observed->updateCount, 1);
    EXPECT_EQ(observed->drawCount, 1);

    ASSERT_TRUE(panels.SetVisible("Inspector", false));
    panels.UpdateVisible(0.016f);
    panels.DrawVisible();
    EXPECT_EQ(observed->updateCount, 1);
    EXPECT_EQ(observed->drawCount, 1);
}

TEST(EditorServiceContainer, InitializesAndShutsDownInDependencyOrder) {
    std::vector<int> events;
    EditorServiceContainer services;
    services.Emplace<TestService>(events, 1);
    services.Emplace<SecondTestService>(events, true);

    ASSERT_TRUE(services.InitializeAll());
    services.ShutdownAll();
    EXPECT_EQ(events, (std::vector<int>{1, 2, -2, -1}));
}

TEST(EditorServiceContainer, RollsBackInitializedServicesOnFailure) {
    std::vector<int> events;
    EditorServiceContainer services;
    services.Emplace<TestService>(events, 1);
    services.Emplace<SecondTestService>(events, false);

    EXPECT_FALSE(services.InitializeAll());
    EXPECT_FALSE(services.IsInitialized());
    EXPECT_EQ(events, (std::vector<int>{1, 2, -1}));
}
