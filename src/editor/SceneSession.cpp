/**
 * @file SceneSession.cpp
 * @brief Transactional document switching and save command state.
 * @lines ~266
 *
 * Key functions:
 * - Open() - Stage file and preview before replacing the current session (line ~10)
 * - Save() - Guard invalid drafts and update current path only after success (line ~42)
 * - SetTransformText() - Validate one numeric field while preserving other axes (line ~81)
 * - Create() - Stage a new document and preview before commit (line ~161)
 * - Edit() - Apply edits to a clone and commit only after validation (line ~193)
 */
#include "editor/SceneSession.h"
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <atomic>
#include <string>

namespace
{
    std::atomic<uint64_t> gNextSceneSessionId{1};
}

SceneSession::SceneSession()
    : m_sessionId(gNextSceneSessionId.fetch_add(1))
{
}

bool SceneSession::AdvanceRevision()
{
    if (m_revision == std::numeric_limits<uint64_t>::max())
    {
        m_error = "Scene revision limit reached";
        return false;
    }
    ++m_revision;
    return true;
}

std::string SceneSession::GetDocumentIdentity() const
{
    if (!m_document.IsLoaded())
    {
        return {};
    }
    return std::to_string(m_sessionId) + ":" + std::to_string(m_documentId);
}

SceneSession::AuthoredSnapshot SceneSession::GetAuthoredSnapshot() const
{
    AuthoredSnapshot snapshot;
    snapshot.documentIdentity = GetDocumentIdentity();
    snapshot.revision = m_revision;
    snapshot.path = m_path;
    snapshot.dirty = IsDirty();
    snapshot.scene = m_document.Snapshot();
    return snapshot;
}

void SceneSession::ResetObjectHandles()
{
    ++m_documentId;
    m_nextHandleValue = 1;
    m_objectHandles.clear();
    m_handleObjects.clear();
    m_positionUndo.reset();
    SyncObjectHandles();
}

void SceneSession::SyncObjectHandles()
{
    const auto snapshot = m_document.Snapshot();
    std::set<std::string> currentNames;
    for (const auto &object : snapshot["scene"]["objects"])
    {
        currentNames.insert(object["name"].as<std::string>());
    }
    for (auto it = m_objectHandles.begin(); it != m_objectHandles.end();)
    {
        if (currentNames.find(it->first) == currentNames.end())
        {
            m_handleObjects.erase(it->second);
            it = m_objectHandles.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (const auto &name : currentNames)
    {
        if (m_objectHandles.find(name) == m_objectHandles.end())
        {
            const uint64_t handle = m_nextHandleValue++;
            m_objectHandles.emplace(name, handle);
            m_handleObjects.emplace(handle, name);
        }
    }
}

std::optional<AuthoredObjectHandle> SceneSession::GetObjectHandle(const std::string &name) const
{
    const auto it = m_objectHandles.find(name);
    if (!m_document.IsLoaded() || it == m_objectHandles.end())
    {
        return std::nullopt;
    }
    return AuthoredObjectHandle(m_sessionId, m_documentId, it->second);
}

std::vector<std::pair<std::string, AuthoredObjectHandle>> SceneSession::GetObjectHandles() const
{
    std::vector<std::pair<std::string, AuthoredObjectHandle>> handles;
    if (!m_document.IsLoaded())
    {
        return handles;
    }
    handles.reserve(m_objectHandles.size());
    for (const auto &object : m_objectHandles)
    {
        handles.emplace_back(object.first, AuthoredObjectHandle(m_sessionId, m_documentId, object.second));
    }
    return handles;
}

std::optional<std::string> SceneSession::ResolveObjectHandle(const AuthoredObjectHandle &handle) const
{
    if (handle.m_owner != m_sessionId || handle.m_document != m_documentId)
    {
        return std::nullopt;
    }
    const auto it = m_handleObjects.find(handle.m_value);
    if (it == m_handleObjects.end())
    {
        return std::nullopt;
    }
    return it->second;
}

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
    if (!AdvanceRevision())
    {
        return false;
    }
    m_document = std::move(candidate);
    m_invalidTransforms.clear();
    m_path = path;
    ResetObjectHandles();
    m_error.clear();
    return true;
}

bool SceneSession::Save(const std::string &path)
{
    if (!m_invalidTransforms.empty())
    {
        m_error = "Fix the invalid transform before saving.";
        return false;
    }
    const std::string destination = path.empty() ? m_path : path;
    if (destination.empty() || !m_document.Save(destination))
    {
        m_error = destination.empty() ? "Use Save As to choose a scene destination." : m_document.GetError();
        return false;
    }
    m_path = destination;
    m_positionUndo.reset();
    m_error.clear();
    return true;
}

bool SceneSession::SetPosition(const std::string &name, double x, double y, double z)
{
    auto candidate = m_document.Clone();
    if (!candidate.SetPosition(name, x, y, z))
    {
        m_error = candidate.GetError();
        return false;
    }
    if (!AdvanceRevision())
    {
        return false;
    }
    m_document = std::move(candidate);
    m_positionUndo.reset();
    SyncObjectHandles();
    m_error.clear();
    return true;
}

bool SceneSession::SetPosition(const AuthoredObjectHandle &handle, uint64_t expectedRevision,
                              double x, double y, double z)
{
    if (expectedRevision != m_revision)
    {
        m_error = "Scene revision conflict";
        return false;
    }
    if (!m_invalidTransforms.empty())
    {
        m_error = "Fix the invalid transform before editing the scene.";
        return false;
    }
    const auto name = ResolveObjectHandle(handle);
    if (!name)
    {
        m_error = "Authored object handle is stale or belongs to another session.";
        return false;
    }
    auto candidate = m_document.Clone();
    if (!candidate.SetPosition(*name, x, y, z))
    {
        m_error = candidate.GetError();
        return false;
    }
    if (!AdvanceRevision())
    {
        return false;
    }
    m_positionUndo = PositionUndo{handle.m_value, m_document.Clone()};
    m_document = std::move(candidate);
    SyncObjectHandles();
    m_error.clear();
    return true;
}

bool SceneSession::UndoPosition(const AuthoredObjectHandle &handle, uint64_t expectedRevision)
{
    if (expectedRevision != m_revision)
    {
        m_error = "Scene revision conflict";
        return false;
    }
    if (!m_invalidTransforms.empty())
    {
        m_error = "Fix the invalid transform before undoing.";
        return false;
    }
    if (!ResolveObjectHandle(handle) || !m_positionUndo || m_positionUndo->handleValue != handle.m_value)
    {
        m_error = "No undoable position edit exists for this object handle.";
        return false;
    }
    if (!AdvanceRevision())
    {
        return false;
    }
    m_document = std::move(m_positionUndo->before);
    m_positionUndo.reset();
    SyncObjectHandles();
    m_error.clear();
    return true;
}

bool SceneSession::SetPositionText(const std::string &name, size_t axis, const std::string &text)
{
    return SetTransformText(name, "pos", axis, text);
}

bool SceneSession::SetScaleText(const std::string &name, size_t axis, const std::string &text)
{
    return SetTransformText(name, "scale", axis, text);
}

bool SceneSession::SetTransformText(const std::string &name, const std::string &group, size_t axis, const std::string &text)
{
    const size_t draftAxis = axis + (group == "scale" ? 3 : 0);
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
        m_invalidTransforms.insert({name, draftAxis});
        m_error = "Transform value must be a finite number; the scene keeps its last valid value.";
        return false;
    }
    const char *axes[] = {"x", "y", "z"};
    auto candidate = m_document.Clone();
    const auto snapshot = candidate.Snapshot();
    for (const auto &object : snapshot["scene"]["objects"])
    {
        if (object["name"].as<std::string>() != name)
        {
            continue;
        }
        const auto transform = object["transform"];
        const auto position = transform ? transform[group] : YAML::Node();
        const double fallback = group == "scale" ? 1.0 : 0.0;
        double coordinates[3] = {fallback, fallback, fallback};
        for (size_t i = 0; i < 3; ++i)
        {
            if (position && position[axes[i]])
            {
                coordinates[i] = position[axes[i]].as<double>();
            }
        }
        coordinates[axis] = value;
        const bool edited = group == "scale" ? candidate.SetScale(name, coordinates[0], coordinates[1], coordinates[2])
                                             : candidate.SetPosition(name, coordinates[0], coordinates[1], coordinates[2]);
        if (!edited)
        {
            m_error = candidate.GetError();
            return false;
        }
        if (!AdvanceRevision())
        {
            return false;
        }
        m_document = std::move(candidate);
        m_positionUndo.reset();
        SyncObjectHandles();
        m_error.clear();
        m_invalidTransforms.erase({name, draftAxis});
        if (!m_invalidTransforms.empty())
        {
            m_error = "Another transform field is invalid; fix it before saving.";
        }
        return true;
    }
    m_error = "Authored object not found: " + name;
    return false;
}

void SceneSession::DiscardInvalidTransformDrafts(const std::string &name)
{
    for (auto it = m_invalidTransforms.begin(); it != m_invalidTransforms.end();)
    {
        if (it->first == name)
        {
            it = m_invalidTransforms.erase(it);
        }
        else
        {
            ++it;
        }
    }
    if (m_invalidTransforms.empty())
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
    if (!AdvanceRevision())
    {
        return false;
    }
    m_document = std::move(candidate);
    m_path.clear();
    m_invalidTransforms.clear();
    ResetObjectHandles();
    m_error.clear();
    return true;
}

bool SceneSession::Edit(const std::function<bool(SceneDocument &)> &operation, const PreparePreview &prepare)
{
    if (!m_invalidTransforms.empty())
    {
        m_error = "Fix the invalid transform before changing scene objects.";
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
    if (!AdvanceRevision())
    {
        return false;
    }
    m_document = std::move(candidate);
    m_positionUndo.reset();
    SyncObjectHandles();
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

bool SceneSession::SetFlyingPropsProperty(const std::string &name, const std::string &root,
                                           const std::string &property, const std::string &value)
{
    return Edit([&](SceneDocument &document) { return document.SetFlyingPropsProperty(name, root, property, value); }, {});
}
