#include "editor/SceneSession.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <stdexcept>

namespace py = pybind11;

PYBIND11_MODULE(imhotep_scene_session, module)
{
    module.doc() = "Thin Python bindings for Imhotep's authored SceneSession";

    py::class_<AuthoredObjectHandle>(module, "AuthoredObjectHandle");
    py::class_<SceneSession>(module, "SceneSession")
        .def(py::init<>())
        .def("open", [](SceneSession &session, const std::string &path)
        {
            if (!session.Open(path))
            {
                throw std::runtime_error(session.GetError());
            }
        })
        .def_property_readonly("revision", &SceneSession::GetRevision)
        .def_property_readonly("error", [](const SceneSession &session) { return session.GetError(); })
        .def("object_handles", &SceneSession::GetObjectHandles)
        .def("set_position", [](SceneSession &session, const AuthoredObjectHandle &handle,
                                uint64_t revision, double x, double y, double z)
        {
            if (!session.SetPosition(handle, revision, x, y, z))
            {
                throw std::runtime_error(session.GetError());
            }
        })
        .def("undo_position", [](SceneSession &session, const AuthoredObjectHandle &handle, uint64_t revision)
        {
            if (!session.UndoPosition(handle, revision))
            {
                throw std::runtime_error(session.GetError());
            }
        })
        .def("scene_yaml", [](const SceneSession &session)
        {
            YAML::Emitter output;
            output << session.Snapshot();
            if (!output.good())
            {
                throw std::runtime_error(output.GetLastError());
            }
            return std::string(output.c_str());
        });
}
