/**
 * @file EngineUnitTests.cpp
 * @brief C++ unit tests for engine subsystems (no graphics required)
 *
 * Tests ExpressionCache (Lua expression compilation/evaluation)
 * and FrameTiming (frame timing, accumulators, simulation speed).
 */

#include "systems/ExpressionCache.h"
#include "controllers/Registry.h"
#include "systems/SceneModule.h"
#include "util/FrameTiming.h"
#include "util/Logger.h"

#include <iostream>
#include <string>
#include <cmath>
#include <thread>
#include <chrono>

// ============================================================================
// Minimal test harness
// ============================================================================

static int g_passed = 0;
static int g_failed = 0;

static std::string FixturePath(const std::string& name)
{
    return std::string(IMHOTEP_SOURCE_DIR) + "/tests/fixtures/" + name;
}

#define TEST(name) \
    static void Test_##name(); \
    struct TestReg_##name { TestReg_##name() { RunTest(#name, Test_##name); } }; \
    static void Test_##name()

static void RunTest(const char* name, void (*fn)())
{
    try
    {
        fn();
        std::cout << "  PASS: " << name << std::endl;
        g_passed++;
    }
    catch (const std::exception& e)
    {
        std::cout << "  FAIL: " << name << " — " << e.what() << std::endl;
        g_failed++;
    }
    catch (...)
    {
        std::cout << "  FAIL: " << name << " — unknown exception" << std::endl;
        g_failed++;
    }
}

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) throw std::runtime_error("ASSERT_TRUE failed: " #expr); } while(0)

#define ASSERT_FALSE(expr) \
    do { if (expr) throw std::runtime_error("ASSERT_FALSE failed: " #expr); } while(0)

#define ASSERT_EQ(a, b) \
    do { if ((a) != (b)) { \
        throw std::runtime_error( \
            std::string("ASSERT_EQ failed: ") + #a + " != " + #b + \
            " (" + std::to_string(a) + " vs " + std::to_string(b) + ")"); \
    } } while(0)

#define ASSERT_STR_EQ(a, b) \
    do { if ((a) != (b)) { \
        throw std::runtime_error( \
            std::string("ASSERT_STR_EQ failed: \"") + (a) + "\" != \"" + (b) + "\""); \
    } } while(0)

#define ASSERT_NEAR(a, b, tol) \
    do { if (std::abs((a) - (b)) > (tol)) { \
        throw std::runtime_error( \
            std::string("ASSERT_NEAR failed: ") + std::to_string(a) + " vs " + std::to_string(b)); \
    } } while(0)

// ============================================================================
// ExpressionCache Tests
// ============================================================================

static void RunExpressionCacheTests()
{
    std::cout << "\n--- ExpressionCache ---" << std::endl;

    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string);

    // Test: Initialize
    {
        ExpressionCache cache;
        ASSERT_FALSE(cache.IsInitialized());
        cache.Initialize(&lua);
        ASSERT_TRUE(cache.IsInitialized());
        ASSERT_EQ(cache.Size(), (size_t)0);
        std::cout << "  PASS: Initialize" << std::endl;
        g_passed++;
    }

    // Test: Compile and cache hit
    {
        ExpressionCache cache;
        cache.Initialize(&lua);

        uint32_t id1 = cache.GetOrCompile("1 + 2");
        ASSERT_TRUE(id1 != UINT32_MAX);
        ASSERT_EQ(cache.Size(), (size_t)1);

        uint32_t id2 = cache.GetOrCompile("1 + 2");
        ASSERT_EQ(id1, id2); // Cache hit — same ID

        uint32_t id3 = cache.GetOrCompile("3 + 4");
        ASSERT_TRUE(id3 != id1); // Different expression — new ID
        ASSERT_EQ(cache.Size(), (size_t)2);

        auto [hits, misses] = cache.GetStats();
        ASSERT_EQ(hits, (uint64_t)1);   // One cache hit (second "1 + 2")
        ASSERT_EQ(misses, (uint64_t)2); // Two cache misses (first "1 + 2" + "3 + 4")

        std::cout << "  PASS: CompileAndCacheHit" << std::endl;
        g_passed++;
    }

    // Test: Evaluate integer expression
    {
        ExpressionCache cache;
        cache.Initialize(&lua);

        sol::table env = lua.create_table();
        env["x"] = 10;
        env["y"] = 20;

        uint32_t id = cache.GetOrCompile("x + y");
        std::string result = cache.EvaluateAsString(id, env);
        ASSERT_STR_EQ(result, "30");

        std::cout << "  PASS: EvaluateInteger" << std::endl;
        g_passed++;
    }

    // Test: Evaluate string expression
    {
        ExpressionCache cache;
        cache.Initialize(&lua);

        sol::table env = lua.create_table();
        env["name"] = "Tetris";

        uint32_t id = cache.GetOrCompile("name");
        std::string result = cache.EvaluateAsString(id, env);
        ASSERT_STR_EQ(result, "Tetris");

        std::cout << "  PASS: EvaluateString" << std::endl;
        g_passed++;
    }

    // Test: Evaluate boolean expression
    {
        ExpressionCache cache;
        cache.Initialize(&lua);

        sol::table env = lua.create_table();
        env["score"] = 100;

        uint32_t id = cache.GetOrCompile("score > 50");
        ASSERT_TRUE(cache.EvaluateAsBool(id, env));

        uint32_t id2 = cache.GetOrCompile("score < 50");
        ASSERT_FALSE(cache.EvaluateAsBool(id2, env));

        std::cout << "  PASS: EvaluateBool" << std::endl;
        g_passed++;
    }

    // Test: Evaluate double expression
    {
        ExpressionCache cache;
        cache.Initialize(&lua);

        sol::table env = lua.create_table();
        env["pi"] = 3.14159;

        uint32_t id = cache.GetOrCompile("pi");
        std::string result = cache.EvaluateAsString(id, env);
        ASSERT_STR_EQ(result, "3.14");

        std::cout << "  PASS: EvaluateDouble" << std::endl;
        g_passed++;
    }

    // Test: Evaluate nested table access (data.score pattern)
    {
        ExpressionCache cache;
        cache.Initialize(&lua);

        sol::table env = lua.create_table();
        sol::table data = lua.create_table();
        data["score"] = 42;
        data["level"] = 5;
        env["data"] = data;

        uint32_t id = cache.GetOrCompile("data.score");
        std::string result = cache.EvaluateAsString(id, env);
        ASSERT_STR_EQ(result, "42");

        uint32_t id2 = cache.GetOrCompile("data.level > 3");
        ASSERT_TRUE(cache.EvaluateAsBool(id2, env));

        std::cout << "  PASS: EvaluateNestedTable" << std::endl;
        g_passed++;
    }

    // Test: Invalid expression returns UINT32_MAX
    {
        ExpressionCache cache;
        cache.Initialize(&lua);

        uint32_t id = cache.GetOrCompile("!!!invalid syntax");
        ASSERT_EQ(id, UINT32_MAX);

        std::cout << "  PASS: InvalidExpression" << std::endl;
        g_passed++;
    }

    // Test: Evaluate with invalid ID
    {
        ExpressionCache cache;
        cache.Initialize(&lua);

        sol::table env = lua.create_table();
        std::string result = cache.EvaluateAsString(999, env);
        ASSERT_STR_EQ(result, "");

        bool boolResult = cache.EvaluateAsBool(999, env);
        ASSERT_FALSE(boolResult);

        std::cout << "  PASS: InvalidExprId" << std::endl;
        g_passed++;
    }

    // Test: Dependency analysis
    {
        ExpressionCache cache;
        cache.Initialize(&lua);

        uint32_t id = cache.GetOrCompile("data.score");
        const auto& deps = cache.GetDependencies(id);
        ASSERT_TRUE(deps.count("data.score") > 0);
        ASSERT_TRUE(deps.count("data") > 0);

        std::cout << "  PASS: DependencyAnalysis" << std::endl;
        g_passed++;
    }

    // Test: Clear resets cache
    {
        ExpressionCache cache;
        cache.Initialize(&lua);

        cache.GetOrCompile("1 + 1");
        cache.GetOrCompile("2 + 2");
        ASSERT_EQ(cache.Size(), (size_t)2);

        cache.Clear();
        ASSERT_EQ(cache.Size(), (size_t)0);

        auto [hits, misses] = cache.GetStats();
        ASSERT_EQ(hits, (uint64_t)0);
        ASSERT_EQ(misses, (uint64_t)0);

        std::cout << "  PASS: Clear" << std::endl;
        g_passed++;
    }

    // Test: Not initialized returns error
    {
        ExpressionCache cache;
        // Don't call Initialize
        uint32_t id = cache.GetOrCompile("1 + 1");
        ASSERT_EQ(id, UINT32_MAX);

        std::cout << "  PASS: NotInitialized" << std::endl;
        g_passed++;
    }
}

// ============================================================================
// FrameTiming Tests
// ============================================================================

static void RunFrameTimingTests()
{
    std::cout << "\n--- FrameTiming ---" << std::endl;

    // Test: Fixed delta calculation
    {
        FrameTiming timing(FrameTimingMode::FIXED, 60.0);
        ASSERT_NEAR(timing.GetFixedDelta(), 1000.0 / 60.0, 0.01);

        FrameTiming timing30(FrameTimingMode::FIXED, 30.0);
        ASSERT_NEAR(timing30.GetFixedDelta(), 1000.0 / 30.0, 0.01);

        std::cout << "  PASS: FixedDeltaCalculation" << std::endl;
        g_passed++;
    }

    // Test: Fixed step accumulation
    {
        FrameTiming timing(FrameTimingMode::FIXED, 60.0);

        // Before any Update(), accumulator is 0 — no steps
        ASSERT_FALSE(timing.ShouldUpdateFixedStep());

        // Sleep to build up time, then Update
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        timing.Update();

        // Should have accumulated ~20ms, enough for one 16.67ms step
        ASSERT_TRUE(timing.ShouldUpdateFixedStep());

        std::cout << "  PASS: FixedStepAccumulation" << std::endl;
        g_passed++;
    }

    // Test: ShouldUpdateFixedStep returns false in VARIABLE mode
    {
        FrameTiming timing(FrameTimingMode::VARIABLE, 60.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        timing.Update();
        ASSERT_FALSE(timing.ShouldUpdateFixedStep());

        std::cout << "  PASS: FixedStepWrongMode" << std::endl;
        g_passed++;
    }

    // Test: Variable mode simulation accumulation
    {
        FrameTiming timing(FrameTimingMode::VARIABLE, 60.0, 60.0);
        timing.SetSimulationMultiplier(1.0f);

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        timing.Update();

        // At 1x speed, ~20ms accumulated, one 16.67ms sim step
        ASSERT_TRUE(timing.ShouldUpdateSimulation());

        std::cout << "  PASS: VariableSimAccumulation" << std::endl;
        g_passed++;
    }

    // Test: Paused simulation (multiplier = 0)
    {
        FrameTiming timing(FrameTimingMode::VARIABLE, 60.0, 60.0);
        timing.SetSimulationMultiplier(0.0f);

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        timing.Update();

        // Paused — no simulation updates
        ASSERT_FALSE(timing.ShouldUpdateSimulation());

        std::cout << "  PASS: PausedSimulation" << std::endl;
        g_passed++;
    }

    // Test: ShouldRenderFrame in variable mode
    {
        FrameTiming timing(FrameTimingMode::VARIABLE, 60.0, 60.0);

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        timing.Update();

        // ~20ms elapsed >= 16.67ms render frame time
        ASSERT_TRUE(timing.ShouldRenderFrame());

        std::cout << "  PASS: VariableRenderFrame" << std::endl;
        g_passed++;
    }

    // Test: ShouldRenderFrame always true in FIXED mode
    {
        FrameTiming timing(FrameTimingMode::FIXED, 60.0);
        ASSERT_TRUE(timing.ShouldRenderFrame());

        std::cout << "  PASS: FixedAlwaysRenders" << std::endl;
        g_passed++;
    }

    // Test: Simple mode — delta only, no accumulators
    {
        FrameTiming timing(FrameTimingMode::SIMPLE);

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        timing.Update();

        // Delta should be roughly 10ms (allow variance for scheduling)
        ASSERT_TRUE(timing.GetDelta() >= 0.0);
        // No fixed step in SIMPLE mode
        ASSERT_FALSE(timing.ShouldUpdateFixedStep());
        // Always renders in non-VARIABLE modes
        ASSERT_TRUE(timing.ShouldRenderFrame());

        std::cout << "  PASS: SimpleMode" << std::endl;
        g_passed++;
    }

    // Test: Reset clears accumulators
    {
        FrameTiming timing(FrameTimingMode::FIXED, 60.0);

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        timing.Update();
        ASSERT_TRUE(timing.ShouldUpdateFixedStep());

        timing.Reset();

        // After reset, accumulator should be 0
        ASSERT_FALSE(timing.ShouldUpdateFixedStep());

        std::cout << "  PASS: Reset" << std::endl;
        g_passed++;
    }

    // Test: Simulation multiplier getter/setter
    {
        FrameTiming timing(FrameTimingMode::VARIABLE, 60.0);

        ASSERT_NEAR(timing.GetSimulationMultiplier(), 1.0f, 0.001f);

        timing.SetSimulationMultiplier(2.0f);
        ASSERT_NEAR(timing.GetSimulationMultiplier(), 2.0f, 0.001f);

        timing.SetSimulationMultiplier(0.0f);
        ASSERT_NEAR(timing.GetSimulationMultiplier(), 0.0f, 0.001f);

        std::cout << "  PASS: SimMultiplier" << std::endl;
        g_passed++;
    }

    // Test: VSync getter/setter
    {
        FrameTiming timing(FrameTimingMode::FIXED, 60.0);

        timing.SetVSync(true);
        ASSERT_TRUE(timing.IsVSyncEnabled());

        timing.SetVSync(false);
        ASSERT_FALSE(timing.IsVSyncEnabled());

        std::cout << "  PASS: VSync" << std::endl;
        g_passed++;
    }
}

// ============================================================================
// EventQueue Safety Tests
//
// Regression for: hang caused by sol::table queue = lua["EventQueue"] when
// EventQueue is nil (not yet initialized during splash screen glfwPollEvents).
// The direct assignment panics via sol::default_at_panic → std::terminate →
// abort() when thrown from a GLFW/Cocoa callback (noexcept context).
//
// Fix: use sol::optional<sol::table> — safely returns nullopt instead of panicking.
// ============================================================================

static void RunEventQueueSafetyTests()
{
    std::cout << "\n--- EventQueue Safety (sol::optional regression) ---" << std::endl;

    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::table);

    // Test: nil global → nullopt, no panic
    {
        // EventQueue not set — simulates state before ScriptManager::Initialize()
        sol::optional<sol::table> queue = lua["EventQueue"];
        ASSERT_FALSE(queue.has_value());

        std::cout << "  PASS: NilGlobalReturnsNullopt" << std::endl;
        g_passed++;
    }

    // Test: valid table global → has_value(), can add entries
    {
        lua["EventQueue"] = lua.create_table();

        sol::optional<sol::table> queue = lua["EventQueue"];
        ASSERT_TRUE(queue.has_value());

        sol::table event = lua.create_table();
        event["type"] = "mousemove";
        queue->add(event);
        ASSERT_EQ(queue->size(), (size_t)1);

        std::cout << "  PASS: ValidTableReturnsValue" << std::endl;
        g_passed++;
    }

    // Test: wrong type (number) → nullopt, no panic
    {
        lua["EventQueue"] = 42; // overwrite with a non-table

        sol::optional<sol::table> queue = lua["EventQueue"];
        ASSERT_FALSE(queue.has_value());

        std::cout << "  PASS: WrongTypeReturnsNullopt" << std::endl;
        g_passed++;
    }
}

// ============================================================================
// SparseSet Component Invariant Tests
// ============================================================================

static void RunSparseSetInvariantTests()
{
    std::cout << "\n--- SparseSet Component Invariants ---" << std::endl;

    // Registering a second tween must update in place, not duplicate iteration.
    {
        constexpr EntityID tweenEntityId = 7;
        SparseSet<Tween> tweens;
        auto noop = [](unsigned int, glm::vec3) {};
        Tween first(noop, glm::vec3(0.0f), glm::vec3(1.0f), 1.0f, TRANS_LINEAR);
        Tween replacement(noop, glm::vec3(2.0f), glm::vec3(3.0f), 2.0f, TRANS_SINE);

        tweens.AddComponent(tweenEntityId, first);
        tweens.AddComponent(tweenEntityId, replacement);

        ASSERT_EQ(tweens.GetEntities().size(), (size_t)1);
        ASSERT_EQ(tweens.GetEntities()[0], tweenEntityId);
        ASSERT_NEAR(tweens.GetComponent(tweenEntityId).Duration, 2.0f, 0.001f);
        ASSERT_EQ(tweens.GetComponent(tweenEntityId).Type, TRANS_SINE);

        tweens.RemoveComponent(tweenEntityId);
        ASSERT_FALSE(tweens.HasComponent(tweenEntityId));
        ASSERT_EQ(tweens.GetEntities().size(), (size_t)0);

        tweens.AddComponent(tweenEntityId, first);
        ASSERT_TRUE(tweens.HasComponent(tweenEntityId));
        ASSERT_EQ(tweens.GetEntities().size(), (size_t)1);

        std::cout << "  PASS: TweenUpsertRemoveReadd" << std::endl;
        g_passed++;
    }

    // Script systems iterate the entity list, so replacing a script must retain
    // exactly one entity entry and expose the replacement component.
    {
        constexpr EntityID scriptEntityId = 11;
        sol::state lua;
        lua.open_libraries(sol::lib::base);
        sol::table firstTable = lua.create_table();
        firstTable.set_function("ready", [](sol::table) {});
        sol::table replacementTable = lua.create_table();
        replacementTable.set_function("ready", [](sol::table) {});

        ScriptComponent first("first", firstTable);
        ScriptComponent replacement("replacement", replacementTable);
        SparseSet<ScriptComponent> scripts;

        scripts.AddComponent(scriptEntityId, first);
        scripts.AddComponent(scriptEntityId, replacement);

        ASSERT_EQ(scripts.GetEntities().size(), (size_t)1);
        ASSERT_EQ(scripts.GetEntities()[0], scriptEntityId);
        ASSERT_STR_EQ(scripts.GetComponent(scriptEntityId).Name, "replacement");

        scripts.RemoveComponent(scriptEntityId);
        ASSERT_FALSE(scripts.HasComponent(scriptEntityId));
        ASSERT_EQ(scripts.GetEntities().size(), (size_t)0);

        scripts.AddComponent(scriptEntityId, first);
        ASSERT_TRUE(scripts.HasComponent(scriptEntityId));
        ASSERT_EQ(scripts.GetEntities().size(), (size_t)1);
        ASSERT_STR_EQ(scripts.GetComponent(scriptEntityId).Name, "first");

        std::cout << "  PASS: ScriptUpsertRemoveReadd" << std::endl;
        g_passed++;
    }
}

// ============================================================================
// Scene module load failure propagation
// ============================================================================

static void RunSceneModuleLoadTests()
{
    std::cout << "\n--- SceneModule load results ---" << std::endl;

    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::table);

    {
        SceneModule module = SceneModuleLoader::Load(
            lua, FixturePath("scene_module_valid.lua"));
        ASSERT_TRUE(module.loadSucceeded);
        ASSERT_TRUE(module.loadError.empty());
        ASSERT_TRUE(module.returnValue.valid());
        std::cout << "  PASS: ValidScriptSucceeds" << std::endl;
        g_passed++;
    }

    {
        SceneModule module = SceneModuleLoader::Load(
            lua, FixturePath("scene_module_invalid.lua"));
        ASSERT_FALSE(module.loadSucceeded);
        ASSERT_FALSE(module.loadError.empty());
        std::cout << "  PASS: SyntaxErrorFails" << std::endl;
        g_passed++;
    }

    {
        SceneModule module = SceneModuleLoader::Load(
            lua, FixturePath("scene_module_missing.lua"));
        ASSERT_FALSE(module.loadSucceeded);
        ASSERT_FALSE(module.loadError.empty());
        std::cout << "  PASS: MissingFileFails" << std::endl;
        g_passed++;
    }
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    // Initialize logger (required by ExpressionCache and FrameTiming)
    imhotep::Logger::GetInstance().Initialize("logs/engine-unit-tests.log");

    std::cout << "=== Engine Unit Tests ===" << std::endl;

    RunExpressionCacheTests();
    RunFrameTimingTests();
    RunEventQueueSafetyTests();
    RunSparseSetInvariantTests();
    RunSceneModuleLoadTests();

    std::cout << "\n--- Results ---" << std::endl;
    std::cout << "  Passed: " << g_passed << std::endl;
    std::cout << "  Failed: " << g_failed << std::endl;

    imhotep::Logger::GetInstance().Shutdown();
    return g_failed > 0 ? 1 : 0;
}
