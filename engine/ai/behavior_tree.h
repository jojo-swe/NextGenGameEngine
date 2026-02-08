#pragma once

#include "engine/core/types.h"
#include "engine/core/ecs/entity.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <any>

namespace nge::ai {

// ─── Behavior Tree Status ────────────────────────────────────────────────

enum class BTStatus : u8 {
    Success,
    Failure,
    Running,
};

// ─── Blackboard (shared AI memory) ───────────────────────────────────────

class Blackboard {
public:
    template<typename T>
    void Set(const std::string& key, const T& value) {
        m_data[key] = value;
    }

    template<typename T>
    T Get(const std::string& key, const T& defaultVal = T{}) const {
        auto it = m_data.find(key);
        if (it == m_data.end()) return defaultVal;
        try { return std::any_cast<T>(it->second); }
        catch (...) { return defaultVal; }
    }

    bool Has(const std::string& key) const { return m_data.count(key) > 0; }
    void Remove(const std::string& key) { m_data.erase(key); }
    void Clear() { m_data.clear(); }

private:
    std::unordered_map<std::string, std::any> m_data;
};

// ─── BT Context (passed to nodes during tick) ────────────────────────────

struct BTContext {
    ecs::Entity entity;
    Blackboard* blackboard = nullptr;
    f32         deltaTime  = 0;
};

// ─── BT Node Base ────────────────────────────────────────────────────────

class BTNode {
public:
    virtual ~BTNode() = default;

    BTStatus Tick(BTContext& ctx) {
        if (!m_initialized) { OnInit(ctx); m_initialized = true; }
        BTStatus status = OnTick(ctx);
        if (status != BTStatus::Running) { OnTerminate(ctx, status); m_initialized = false; }
        return status;
    }

    void Abort(BTContext& ctx) {
        if (m_initialized) { OnTerminate(ctx, BTStatus::Failure); m_initialized = false; }
    }

    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

protected:
    virtual void OnInit(BTContext& /*ctx*/) {}
    virtual BTStatus OnTick(BTContext& ctx) = 0;
    virtual void OnTerminate(BTContext& /*ctx*/, BTStatus /*status*/) {}

    std::string m_name;
    bool m_initialized = false;
};

// ─── Composite Nodes ─────────────────────────────────────────────────────

class BTComposite : public BTNode {
public:
    void AddChild(std::unique_ptr<BTNode> child) {
        m_children.push_back(std::move(child));
    }

protected:
    std::vector<std::unique_ptr<BTNode>> m_children;
};

// Sequence: runs children in order, fails if any child fails
class BTSequence : public BTComposite {
protected:
    BTStatus OnTick(BTContext& ctx) override {
        for (u32 i = m_currentChild; i < static_cast<u32>(m_children.size()); ++i) {
            BTStatus status = m_children[i]->Tick(ctx);
            if (status == BTStatus::Running) { m_currentChild = i; return BTStatus::Running; }
            if (status == BTStatus::Failure) { m_currentChild = 0; return BTStatus::Failure; }
        }
        m_currentChild = 0;
        return BTStatus::Success;
    }
    void OnInit(BTContext& /*ctx*/) override { m_currentChild = 0; }
    u32 m_currentChild = 0;
};

// Selector: runs children in order, succeeds if any child succeeds
class BTSelector : public BTComposite {
protected:
    BTStatus OnTick(BTContext& ctx) override {
        for (u32 i = m_currentChild; i < static_cast<u32>(m_children.size()); ++i) {
            BTStatus status = m_children[i]->Tick(ctx);
            if (status == BTStatus::Running) { m_currentChild = i; return BTStatus::Running; }
            if (status == BTStatus::Success) { m_currentChild = 0; return BTStatus::Success; }
        }
        m_currentChild = 0;
        return BTStatus::Failure;
    }
    void OnInit(BTContext& /*ctx*/) override { m_currentChild = 0; }
    u32 m_currentChild = 0;
};

// Parallel: runs all children simultaneously, configurable success/fail policy
class BTParallel : public BTComposite {
public:
    enum class Policy : u8 { RequireOne, RequireAll };

    BTParallel(Policy successPolicy = Policy::RequireAll, Policy failPolicy = Policy::RequireOne)
        : m_successPolicy(successPolicy), m_failPolicy(failPolicy) {}

protected:
    BTStatus OnTick(BTContext& ctx) override {
        u32 successCount = 0, failCount = 0;
        for (auto& child : m_children) {
            BTStatus s = child->Tick(ctx);
            if (s == BTStatus::Success) successCount++;
            else if (s == BTStatus::Failure) failCount++;
        }
        u32 total = static_cast<u32>(m_children.size());
        if (m_failPolicy == Policy::RequireOne && failCount > 0) return BTStatus::Failure;
        if (m_failPolicy == Policy::RequireAll && failCount == total) return BTStatus::Failure;
        if (m_successPolicy == Policy::RequireOne && successCount > 0) return BTStatus::Success;
        if (m_successPolicy == Policy::RequireAll && successCount == total) return BTStatus::Success;
        return BTStatus::Running;
    }

    Policy m_successPolicy;
    Policy m_failPolicy;
};

// ─── Decorator Nodes ─────────────────────────────────────────────────────

class BTDecorator : public BTNode {
public:
    void SetChild(std::unique_ptr<BTNode> child) { m_child = std::move(child); }
protected:
    std::unique_ptr<BTNode> m_child;
};

// Inverter: flips Success↔Failure
class BTInverter : public BTDecorator {
protected:
    BTStatus OnTick(BTContext& ctx) override {
        if (!m_child) return BTStatus::Failure;
        BTStatus s = m_child->Tick(ctx);
        if (s == BTStatus::Success) return BTStatus::Failure;
        if (s == BTStatus::Failure) return BTStatus::Success;
        return BTStatus::Running;
    }
};

// Repeater: repeats child N times (0 = infinite)
class BTRepeater : public BTDecorator {
public:
    explicit BTRepeater(u32 count = 0) : m_maxCount(count) {}
protected:
    BTStatus OnTick(BTContext& ctx) override {
        if (!m_child) return BTStatus::Failure;
        BTStatus s = m_child->Tick(ctx);
        if (s == BTStatus::Running) return BTStatus::Running;
        m_count++;
        if (m_maxCount > 0 && m_count >= m_maxCount) { m_count = 0; return BTStatus::Success; }
        return BTStatus::Running;
    }
    void OnInit(BTContext& /*ctx*/) override { m_count = 0; }
    u32 m_maxCount = 0;
    u32 m_count = 0;
};

// Cooldown: prevents re-execution for a duration
class BTCooldown : public BTDecorator {
public:
    explicit BTCooldown(f32 duration) : m_duration(duration) {}
protected:
    BTStatus OnTick(BTContext& ctx) override {
        if (m_timer > 0) { m_timer -= ctx.deltaTime; return BTStatus::Failure; }
        if (!m_child) return BTStatus::Failure;
        BTStatus s = m_child->Tick(ctx);
        if (s != BTStatus::Running) m_timer = m_duration;
        return s;
    }
    f32 m_duration;
    f32 m_timer = 0;
};

// ─── Leaf Nodes ──────────────────────────────────────────────────────────

// Action: executes a lambda
class BTAction : public BTNode {
public:
    using ActionFunc = std::function<BTStatus(BTContext&)>;
    explicit BTAction(ActionFunc func, const std::string& name = "Action")
        : m_func(std::move(func)) { m_name = name; }
protected:
    BTStatus OnTick(BTContext& ctx) override { return m_func ? m_func(ctx) : BTStatus::Failure; }
    ActionFunc m_func;
};

// Condition: tests a predicate
class BTCondition : public BTNode {
public:
    using CondFunc = std::function<bool(BTContext&)>;
    explicit BTCondition(CondFunc func, const std::string& name = "Condition")
        : m_func(std::move(func)) { m_name = name; }
protected:
    BTStatus OnTick(BTContext& ctx) override {
        return (m_func && m_func(ctx)) ? BTStatus::Success : BTStatus::Failure;
    }
    CondFunc m_func;
};

// Wait: returns Running for a specified duration
class BTWait : public BTNode {
public:
    explicit BTWait(f32 duration) : m_duration(duration) {}
protected:
    BTStatus OnTick(BTContext& ctx) override {
        m_elapsed += ctx.deltaTime;
        return (m_elapsed >= m_duration) ? BTStatus::Success : BTStatus::Running;
    }
    void OnInit(BTContext& /*ctx*/) override { m_elapsed = 0; }
    f32 m_duration;
    f32 m_elapsed = 0;
};

// ─── Behavior Tree ───────────────────────────────────────────────────────

class BehaviorTree {
public:
    void SetRoot(std::unique_ptr<BTNode> root) { m_root = std::move(root); }

    BTStatus Tick(BTContext& ctx) {
        if (!m_root) return BTStatus::Failure;
        return m_root->Tick(ctx);
    }

    void Abort(BTContext& ctx) {
        if (m_root) m_root->Abort(ctx);
    }

private:
    std::unique_ptr<BTNode> m_root;
};

// ─── Navigation (A* pathfinding stub) ────────────────────────────────────

struct NavMeshNode {
    math::Vec3 position;
    std::vector<u32> neighbors;
    f32 cost = 1.0f;
};

class NavMesh {
public:
    u32 AddNode(const math::Vec3& pos) {
        u32 id = static_cast<u32>(m_nodes.size());
        m_nodes.push_back({pos, {}, 1.0f});
        return id;
    }

    void Connect(u32 a, u32 b) {
        if (a < m_nodes.size() && b < m_nodes.size()) {
            m_nodes[a].neighbors.push_back(b);
            m_nodes[b].neighbors.push_back(a);
        }
    }

    // A* pathfinding
    std::vector<math::Vec3> FindPath(u32 startNode, u32 endNode) const;

    u32 FindNearestNode(const math::Vec3& pos) const;

    const std::vector<NavMeshNode>& GetNodes() const { return m_nodes; }

private:
    std::vector<NavMeshNode> m_nodes;
};

} // namespace nge::ai
