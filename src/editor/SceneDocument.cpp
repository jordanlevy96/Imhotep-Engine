/**
 * @file SceneDocument.cpp
 * @brief Load, edit and save authored scenes independently of the live registry.
 */
#include "editor/SceneDocument.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <limits>
#include <cctype>

bool SceneDocument::Load(const std::string &path)
{
    try
    {
        YAML::Node scene = YAML::LoadFile(path);
        const YAML::Node objects = scene["scene"]["objects"];
        if (!scene.IsMap() || !objects.IsSequence())
        {
            m_error = "Scene must contain a scene.objects sequence";
            return false;
        }
        std::set<std::string> names;
        for (const auto &object : objects)
        {
            const auto name = object["name"].as<std::string>();
            if (name.empty() || !names.insert(name).second)
            {
                m_error = "Authored object names must be nonempty and unique";
                return false;
            }
            if (object["transform"] && (!object["transform"].IsMap() ||
                (object["transform"]["pos"] && !object["transform"]["pos"].IsMap())))
            {
                m_error = "Object transform must be a map";
                return false;
            }
        }
        for (const auto &object : objects)
        {
            const auto transform = object["transform"];
            if (!transform)
            {
                continue;
            }
            for (const char *group : {"pos", "scale", "color"})
            {
                const auto values = transform[group];
                if (!values)
                {
                    continue;
                }
                if (!values.IsMap())
                {
                    m_error = "Transform values must be maps";
                    return false;
                }
                const std::vector<std::string> axes = std::string(group) == "color" ?
                    std::vector<std::string>{"r", "g", "b", "a"} : std::vector<std::string>{"x", "y", "z"};
                for (const auto &axis : axes)
                {
                    if (values[axis] && !std::isfinite(values[axis].as<float>()))
                    {
                        m_error = "Transform values must be finite floats";
                        return false;
                    }
                }
            }
        }
        m_scene = scene;
        m_loaded = true;
        m_dirty = false;
        m_error.clear();
        return true;
    }
    catch (const YAML::Exception &error)
    {
        m_error = error.what();
        return false;
    }
}

bool SceneDocument::SetPosition(const std::string &objectName, double x, double y, double z)
{
    if (!m_loaded || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        std::abs(x) > std::numeric_limits<float>::max() ||
        std::abs(y) > std::numeric_limits<float>::max() ||
        std::abs(z) > std::numeric_limits<float>::max())
    {
        m_error = "A loaded scene and finite coordinates are required";
        return false;
    }
    for (auto object : m_scene["scene"]["objects"])
    {
        if (object["name"].as<std::string>() == objectName)
        {
            const auto position = object["transform"]["pos"];
            if (position && position["x"] && position["y"] && position["z"] &&
                position["x"].as<double>() == x && position["y"].as<double>() == y && position["z"].as<double>() == z)
            {
                m_error.clear();
                return true;
            }
            // Replace only position; retain components and unrecognized authored fields.
            object["transform"]["pos"]["x"] = x;
            object["transform"]["pos"]["y"] = y;
            object["transform"]["pos"]["z"] = z;
            m_dirty = true;
            m_error.clear();
            return true;
        }
    }
    m_error = "Authored object not found: " + objectName;
    return false;
}

bool SceneDocument::Save(const std::string &path)
{
    if (!m_loaded)
    {
        m_error = "No scene is loaded";
        return false;
    }
    // Reserve a private sibling directory so concurrent saves cannot share a temporary file.
    const std::filesystem::path destination(path);
    const auto temporaryDirectory = destination.string() + ".imhotep-save";
    std::error_code error;
    if (!std::filesystem::create_directory(temporaryDirectory, error))
    {
        m_error = "Cannot reserve scene save directory: " + temporaryDirectory;
        return false;
    }
    const auto temporaryFile = std::filesystem::path(temporaryDirectory) / "scene.yaml";
    bool saved = false;
    try
    {
        YAML::Emitter output;
        output << m_scene;
        if (!output.good())
        {
            m_error = output.GetLastError();
        }
        else
        {
            std::ofstream file(temporaryFile, std::ios::binary);
            file << output.c_str() << '\n';
            file.close();
            if (!file)
            {
                m_error = "Cannot write scene file";
            }
            else
            {
                std::filesystem::rename(temporaryFile, destination, error);
                saved = !error;
                if (error)
                {
                    m_error = "Cannot replace scene file: " + error.message();
                }
            }
        }
    }
    catch (const std::exception &exception)
    {
        m_error = exception.what();
    }
    std::filesystem::remove(temporaryFile, error);
    std::filesystem::remove(temporaryDirectory, error);
    if (saved)
    {
        m_dirty = false;
        m_error.clear();
    }
    return saved;
}

namespace
{
    bool IsObjectName(const std::string &name)
    {
        if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_'))
        {
            return false;
        }
        for (unsigned char character : name)
        {
            if (!(std::isalnum(character) || character == '_'))
            {
                return false;
            }
        }
        return true;
    }

    bool IsResourceFile(const std::string &root, const std::string &relative, const std::string &extension)
    {
        namespace fs = std::filesystem;
        const fs::path path(relative);
        if (root.empty() || relative.empty() || path.is_absolute() || path.extension() != extension)
        {
            return false;
        }
        std::error_code error;
        const auto base = fs::canonical(root, error);
        if (error)
        {
            return false;
        }
        const auto resolved = fs::canonical(base / path, error);
        if (error || !fs::is_regular_file(resolved, error) || error)
        {
            return false;
        }
        const auto within = resolved.lexically_relative(base);
        return !within.empty() && *within.begin() != "..";
    }
}

SceneDocument SceneDocument::Clone() const
{
    SceneDocument copy;
    copy.m_scene = YAML::Clone(m_scene);
    copy.m_loaded = m_loaded;
    copy.m_dirty = m_dirty;
    copy.m_error = m_error;
    return copy;
}

bool SceneDocument::Create(const std::string &name)
{
    if (!IsObjectName(name))
    {
        m_error = "Scene name must start with a letter or underscore and contain letters, digits or underscores.";
        return false;
    }
    YAML::Node scene;
    scene["name"] = name;
    scene["scene"]["objects"] = YAML::Node(YAML::NodeType::Sequence);
    m_scene = scene;
    m_loaded = true;
    m_dirty = true;
    m_error.clear();
    return true;
}

bool SceneDocument::AddObject(const std::string &name)
{
    if (!m_loaded || !IsObjectName(name))
    {
        m_error = "Open a scene and use an object name containing letters, digits or underscores, starting with a letter or underscore.";
        return false;
    }
    for (const auto &object : m_scene["scene"]["objects"])
    {
        if (object["name"].as<std::string>() == name)
        {
            m_error = "An object named '" + name + "' already exists.";
            return false;
        }
    }
    YAML::Node object;
    object["name"] = name;
    for (const char *axis : {"x", "y", "z"})
    {
        object["transform"]["pos"][axis] = 0.0;
    }
    m_scene["scene"]["objects"].push_back(object);
    m_dirty = true;
    m_error.clear();
    return true;
}

bool SceneDocument::AssignComponent(const std::string &name, const YAML::Node &fields)
{
    if (!m_loaded)
    {
        m_error = "No scene is open.";
        return false;
    }
    // Work on an independent tree; validation failure must not mutate the document.
    auto candidate = YAML::Clone(m_scene);
    for (auto object : candidate["scene"]["objects"])
    {
        if (object["name"].as<std::string>() != name)
        {
            continue;
        }
        auto components = object["components"];
        if (components && !components.IsSequence())
        {
            m_error = "Object components must be a sequence.";
            return false;
        }
        YAML::Node target;
        size_t matches = 0;
        const auto type = fields["type"].as<std::string>();
        for (auto component : components)
        {
            if (!component.IsMap() || !component["type"].IsScalar())
            {
                m_error = "Existing component entries must be maps with a type.";
                return false;
            }
            const auto existingType = component["type"].as<std::string>();
            if (type == "RenderComponent" && existingType == "Lighting")
            {
                m_error = "This object already has a Lighting component that owns its mesh and shader.";
                return false;
            }
            if (existingType == type)
            {
                target.reset(component);
                ++matches;
            }
        }
        if (matches > 1)
        {
            m_error = "Object has duplicate components of this type; assignment is ambiguous.";
            return false;
        }
        if (matches == 0)
        {
            target = YAML::Node(YAML::NodeType::Map);
            object["components"].push_back(target);
        }
        for (const auto &field : fields)
        {
            target[field.first.as<std::string>()] = YAML::Clone(field.second);
        }
        if (YAML::Dump(m_scene) != YAML::Dump(candidate))
        {
            m_scene = candidate;
            m_dirty = true;
        }
        m_error.clear();
        return true;
    }
    m_error = "Authored object not found: " + name;
    return false;
}

bool SceneDocument::AssignRenderComponent(const std::string &name, const std::string &resourceRoot,
                                          const std::string &shader, const std::string &model)
{
    if (resourceRoot.empty() || !IsResourceFile(resourceRoot + "/shaders", shader, ".shader") ||
        !IsResourceFile(resourceRoot + "/models", model, ".obj"))
    {
        m_error = "Choose an existing .shader and .obj inside the resource shader/model directories.";
        return false;
    }
    YAML::Node fields;
    fields["type"] = "RenderComponent";
    fields["shader"] = std::filesystem::path(shader).lexically_normal().generic_string();
    fields["model"] = std::filesystem::path(model).lexically_normal().generic_string();
    return AssignComponent(name, fields);
}

bool SceneDocument::AssignLuaScript(const std::string &name, const std::string &resourceRoot,
                                    const std::string &script, const std::string &shader, const std::string &model)
{
    if (!IsObjectName(name) || std::filesystem::path(script).stem() != name)
    {
        m_error = "Lua attachment requires the object name to match the script filename. The script must expose that global table; no automatic rename is performed.";
        return false;
    }
    if (!IsResourceFile(resourceRoot, script, ".lua") ||
        (!shader.empty() && !IsResourceFile(resourceRoot + "/shaders", shader, ".shader")) ||
        (!model.empty() && !IsResourceFile(resourceRoot + "/models", model, ".obj")))
    {
        m_error = "Lua script and optional shader/model must reference existing project resources.";
        return false;
    }
    YAML::Node fields;
    fields["type"] = "LuaScript";
    fields["script"] = std::filesystem::path(script).lexically_normal().generic_string();
    if (!shader.empty())
    {
        fields["shader"] = std::filesystem::path(shader).lexically_normal().generic_string();
    }
    if (!model.empty())
    {
        fields["model"] = std::filesystem::path(model).lexically_normal().generic_string();
    }
    return AssignComponent(name, fields);
}
