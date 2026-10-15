/**
 * @file scene_session_bindings.cpp
 * @brief Python bindings for revision-checked authored SceneSession operations.
 * @lines ~66
 *
 * Key functions:
 * - PYBIND11_MODULE() - Expose handles, snapshots, edits, and undo to Python
 */
#include "editor/SceneSession.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <stdexcept>

namespace py = pybind11;

PYBIND11_MODULE(imhotep_scene_session, module)
{
    module.doc() = "Thin Python bindings for Imhotep's authored SceneSession";

    py::class_<AuthoredObjectHandle>(module, "AuthoredObjectHandle",
                                     "Opaque handle scoped to one SceneSession document.");
    py::class_<SceneSession>(module, "SceneSession",
                             "In-memory authored scene session with revision-checked edits.")
        .def(py::init<>(), "Create an empty scene session.")
        .def("open", [](SceneSession &session, const std::string &path)
        {
            if (!session.Open(path))
            {
                throw std::runtime_error(session.GetError());
            }
        }, py::arg("path"), "Open a saved scene. Raises RuntimeError if loading fails.")
        .def_property_readonly("revision", &SceneSession::GetRevision,
                               "Current document revision for optimistic edit checks.")
        .def_property_readonly("error", [](const SceneSession &session) { return session.GetError(); },
                               "Most recent operation error, or an empty string.")
        .def("object_handles", &SceneSession::GetObjectHandles,
             "Return current object names paired with document-scoped opaque handles.")
        .def("set_position", [](SceneSession &session, const AuthoredObjectHandle &handle,
                                uint64_t revision, double x, double y, double z)
        {
            if (!session.SetPosition(handle, revision, x, y, z))
            {
                throw std::runtime_error(session.GetError());
            }
        }, py::arg("handle"), py::arg("expected_revision"), py::arg("x"), py::arg("y"), py::arg("z"),
           "Set an object's position when handle and expected revision are current; raises RuntimeError on failure.")
        .def("undo_position", [](SceneSession &session, const AuthoredObjectHandle &handle, uint64_t revision)
        {
            if (!session.UndoPosition(handle, revision))
            {
                throw std::runtime_error(session.GetError());
            }
        }, py::arg("handle"), py::arg("expected_revision"),
           "Undo the latest position edit when handle and revision match; raises RuntimeError on failure.")
        .def("scene_yaml", [](const SceneSession &session)
        {
            YAML::Emitter output;
            output << session.Snapshot();
            if (!output.good())
            {
                throw std::runtime_error(output.GetLastError());
            }
            return std::string(output.c_str());
        }, "Return the current authored YAML as a string; raises RuntimeError if serialization fails.");
}
