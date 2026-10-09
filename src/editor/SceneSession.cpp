/** @file SceneSession.cpp
 *  @brief Transactional document switching and save command state.
 */
#include "editor/SceneSession.h"
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <limits>

bool SceneSession::Open(const std::string &path, bool discardEdits, const PreparePreview &prepare)
{
    if (IsDirty() && !discardEdits)
    {
        m_error = "Unsaved scene changes. Save first, or choose Discard and Open.";
        return false;
    }
    SceneDocument candidate;
    if (!candidate.Load(path))
    {
        m_error = candidate.GetError();
        return false;
    }
    try
    {
        if (prepare && !prepare(candidate.Snapshot(), m_error))
        {
            return false;
        }
    }
    catch (const std::exception &error)
    {
        m_error = error.what();
        return false;
    }
    m_document = std::move(candidate);
    m_invalidPositions.clear();
    m_path = path;
    m_error.clear();
    return true;
}

bool SceneSession::Save(const std::string &path)
{
    if (!m_invalidPositions.empty())
    {
        m_error = "Fix the invalid position before saving.";
        return false;
    }
    const std::string destination = path.empty() ? m_path : path;
    if (destination.empty() || !m_document.Save(destination))
    {
        m_error = destination.empty() ? "Use Save As to choose a scene destination." : m_document.GetError();
        return false;
    }
    m_path = destination;
    m_error.clear();
    return true;
}

bool SceneSession::SetPosition(const std::string &name, double x, double y, double z)
{
    if (!m_document.SetPosition(name, x, y, z))
    {
        m_error = m_document.GetError();
        return false;
    }
    m_error.clear();
    return true;
}

bool SceneSession::SetPositionText(const std::string &name, size_t axis, const std::string &text)
{
    if (!m_document.IsLoaded())
    {
        m_error = "No scene is open";
        return false;
    }
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    const bool parsed = end != text.c_str();
    while (end && std::isspace(static_cast<unsigned char>(*end)))
    {
        ++end;
    }
    if (axis > 2 || text.empty() || !parsed || !end || *end != '\0' ||
        !std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
    {
        m_invalidPositions.insert({name, axis});
        m_error = "Position must be a finite number; the scene keeps its last valid value.";
        return false;
    }
    const char *axes[] = {"x", "y", "z"};
    const auto snapshot = m_document.Snapshot();
    for (const auto &object : snapshot["scene"]["objects"])
    {
        if (object["name"].as<std::string>() != name)
        {
            continue;
        }
        const auto transform = object["transform"];
        const auto position = transform ? transform["pos"] : YAML::Node();
        double coordinates[3] = {0.0, 0.0, 0.0};
        for (size_t i = 0; i < 3; ++i)
        {
            if (position && position[axes[i]])
            {
                coordinates[i] = position[axes[i]].as<double>();
            }
        }
        coordinates[axis] = value;
        if (!SetPosition(name, coordinates[0], coordinates[1], coordinates[2]))
        {
            return false;
        }
        m_invalidPositions.erase({name, axis});
        if (!m_invalidPositions.empty())
        {
            m_error = "Another position field is invalid; fix it before saving.";
        }
        return true;
    }
    m_error = "Authored object not found: " + name;
    return false;
}

void SceneSession::DiscardInvalidPositionDrafts(const std::string &name)
{
    for (auto it = m_invalidPositions.begin(); it != m_invalidPositions.end();)
    {
        if (it->first == name)
        {
            it = m_invalidPositions.erase(it);
        }
        else
        {
            ++it;
        }
    }
    if (m_invalidPositions.empty())
    {
        m_error.clear();
    }
}

bool SceneSession::Create(const std::string &name, bool discardEdits, const PreparePreview &prepare)
{
    if (IsDirty() && !discardEdits)
    {
        m_error = "Unsaved scene changes. Save first, or choose Discard and New.";
        return false;
    }
    SceneDocument candidate;
    if (!candidate.Create(name))
    {
        m_error = candidate.GetError();
        return false;
    }
    try
    {
        if (prepare && !prepare(candidate.Snapshot(), m_error))
        {
            return false;
        }
    }
    catch (const std::exception &error)
    {
        m_error = error.what();
        return false;
    }
    m_document = std::move(candidate);
    m_path.clear();
    m_invalidPositions.clear();
    m_error.clear();
    return true;
}

bool SceneSession::Edit(const std::function<bool(SceneDocument &)> &operation, const PreparePreview &prepare)
{
    if (!m_invalidPositions.empty())
    {
        m_error = "Fix the invalid position before changing scene objects.";
        return false;
    }
    auto candidate = m_document.Clone();
    try
    {
        if (!operation(candidate))
        {
            m_error = candidate.GetError();
            return false;
        }
        if (prepare && !prepare(candidate.Snapshot(), m_error))
        {
            return false;
        }
    }
    catch (const std::exception &error)
    {
        m_error = error.what();
        return false;
    }
    m_document = std::move(candidate);
    m_error.clear();
    return true;
}

bool SceneSession::AddObject(const std::string &name, const PreparePreview &prepare)
{
    return Edit([&](SceneDocument &document) { return document.AddObject(name); }, prepare);
}

bool SceneSession::AssignRenderComponent(const std::string &name, const std::string &resourceRoot,
                                        const std::string &shader, const std::string &model, const PreparePreview &prepare)
{
    return Edit([&](SceneDocument &document)
    {
        return document.AssignRenderComponent(name, resourceRoot, shader, model);
    }, prepare);
}

bool SceneSession::AssignLuaScript(const std::string &name, const std::string &resourceRoot,
                                  const std::string &script, const std::string &shader, const std::string &model,
                                  const PreparePreview &prepare)
{
    return Edit([&](SceneDocument &document)
    {
        return document.AssignLuaScript(name, resourceRoot, script, shader, model);
    }, prepare);
}

bool SceneSession::SetModules(const std::string &resourceRoot, const std::vector<std::string> &scripts)
{
    return Edit([&](SceneDocument &document) { return document.SetModules(resourceRoot, scripts); }, {});
}

bool SceneSession::SetCapability(const std::string &name, bool enabled)
{
    return Edit([&](SceneDocument &document) { return document.SetCapability(name, enabled); }, {});
}

bool SceneSession::SetStrictMode(bool enabled)
{
    return Edit([&](SceneDocument &document) { return document.SetStrictMode(enabled); }, {});
}
