#include "controllers/Registry.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template <typename Fn>
    void RequireOutOfRange(Fn &&fn, const std::string &message)
    {
        try
        {
            fn();
        }
        catch (const std::out_of_range &)
        {
            return;
        }
        throw std::runtime_error(message);
    }
}

int main()
{
    Registry &registry = Registry::GetInstance();

    EntityID original = registry.RegisterEntity("reusable-name");
    Require(registry.IsEntityAlive(original), "new entity must be live");
    Require(registry.GetEntityByName("reusable-name") == original, "live entity must be discoverable");
    Require(registry.GetEntityCount() == 1, "live count must include new entity");

    registry.DestroyEntity(original);
    Require(!registry.IsEntityAlive(original), "destroyed entity must not be live");
    Require(registry.GetEntityByName("reusable-name") == ENTITY_NULL, "destroyed entity must not be found by name");
    Require(registry.GetEntityCount() == 0, "destroyed entity must not be counted");

    const auto afterDestroy = registry.GetAllEntities();
    Require(std::find(afterDestroy.begin(), afterDestroy.end(), original) == afterDestroy.end(),
            "destroyed entity must not be enumerated");
    Require(!registry.HasComponent<Transform>(original), "destroyed entity must report no components");
    RequireOutOfRange([&] { (void)registry.GetComponent<Transform>(original); },
                      "component access on a destroyed entity must fail safely");

    // Repeated destruction must be harmless.
    registry.DestroyEntity(original);
    Require(registry.GetEntityCount() == 0, "repeated destroy must not change live state");

    // Reusing a name must resolve the new live entity, never the stale slot.
    EntityID replacement = registry.RegisterEntity("reusable-name");
    Require(replacement != original, "entity IDs remain monotonic");
    Require(registry.GetEntityByName("reusable-name") == replacement,
            "name reuse must resolve the live replacement");
    Require(registry.GetAllEntities() == std::vector<EntityID>{replacement},
            "enumeration must contain only the live replacement");

    RequireOutOfRange([&] {
        Transform transform;
        registry.RegisterComponent<Transform>(original, transform);
    }, "component registration on a destroyed entity must fail safely");

    registry.DestroyEntity(replacement);
    std::cout << "Registry liveness regression tests passed" << std::endl;
    return 0;
}
