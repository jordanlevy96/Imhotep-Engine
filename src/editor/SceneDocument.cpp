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
