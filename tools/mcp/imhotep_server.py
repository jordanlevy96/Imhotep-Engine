#!/usr/bin/env python3
"""Local MCP tools for saved authored files and in-memory scene edit sessions."""

import argparse
import importlib
import json
import numbers
from pathlib import Path
import subprocess
import sys
import threading
import uuid

from mcp.server.fastmcp import FastMCP
from mcp.types import ToolAnnotations
from project_files import contained_path, list_resources as enumerate_resources


def make_server(project_root: Path, resource_root: Path, helper: Path, session_module) -> FastMCP:
    server = FastMCP("Imhotep authored-scene tools")
    sessions = {}
    sessions_lock = threading.RLock()
    read_only = ToolAnnotations(readOnlyHint=True, destructiveHint=False, idempotentHint=True, openWorldHint=False)
    in_memory = ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=False, openWorldHint=False)
    close_session = ToolAnnotations(readOnlyHint=False, destructiveHint=True, idempotentHint=False, openWorldHint=False)

    def lookup_session(session_id: str):
        entry = sessions.get(session_id)
        if entry is None:
            raise ValueError("editing session is unknown, closed, or expired after server restart")
        return entry

    @server.tool(annotations=read_only)
    def inspect_scene(scene_path: str) -> str:
        """Inspect a saved authored YAML scene under the project root. This does not read unsaved Editor state or execute Lua."""
        scene = contained_path(project_root, scene_path)
        if not scene.is_file() or scene.suffix.lower() not in {".yaml", ".yml"}:
            raise ValueError("scene path must be a regular YAML scene file")
        result = subprocess.run(
            [str(helper), str(project_root), str(scene.relative_to(project_root))],
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
        )
        if result.returncode:
            raise ValueError(result.stderr.strip() or "scene inspection failed")
        return result.stdout

    @server.tool(annotations=read_only)
    def list_resources() -> str:
        """List regular project resource files under the configured resource root; symlink escapes are omitted."""
        return enumerate_resources(resource_root)

    @server.tool(annotations=in_memory)
    def open_edit_session(scene_path: str) -> str:
        """Open a saved YAML scene as a headless in-memory editing session. This does not attach to the live Editor or write files."""
        scene = contained_path(project_root, scene_path)
        if not scene.is_file() or scene.suffix.lower() not in {".yaml", ".yml"}:
            raise ValueError("scene path must be a regular YAML scene file")
        with sessions_lock:
            if len(sessions) >= 16:
                raise ValueError("editing session limit reached; close an unused session")
            session = session_module.SceneSession()
            session.open(str(scene))
            session_id = uuid.uuid4().hex
            handles = {
                uuid.uuid4().hex: (name, handle)
                for name, handle in session.object_handles()
            }
            sessions[session_id] = {"session": session, "handles": handles}
            return json.dumps({
                "session_id": session_id,
                "revision": session.revision,
                "scene_path": scene.relative_to(project_root).as_posix(),
                "objects": [{"name": name, "handle": token} for token, (name, _) in handles.items()],
                "persistence": "in-memory only; close or server restart discards edits",
            }, indent=2)

    @server.tool(annotations=read_only)
    def inspect_edit_session(session_id: str) -> str:
        """Inspect the current revision, opaque object handles, and authored YAML in an open headless edit session."""
        with sessions_lock:
            entry = lookup_session(session_id)
            session = entry["session"]
            return json.dumps({
                "session_id": session_id,
                "revision": session.revision,
                "objects": [{"name": name, "handle": token} for token, (name, _) in entry["handles"].items()],
                "scene_yaml": session.scene_yaml(),
            }, indent=2)

    @server.tool(annotations=in_memory)
    def set_scene_position(session_id: str, object_handle: str, expected_revision: int,
                           x: float, y: float, z: float) -> str:
        """Apply one expected-revision position edit to the session's in-memory authored scene; never saves or plays it."""
        if isinstance(expected_revision, bool) or not isinstance(expected_revision, int) or expected_revision < 0:
            raise ValueError("expected_revision must be a nonnegative integer")
        coordinates = (x, y, z)
        if any(isinstance(value, bool) or not isinstance(value, numbers.Real) for value in coordinates):
            raise ValueError("position coordinates must be numbers")
        with sessions_lock:
            entry = lookup_session(session_id)
            target = entry["handles"].get(object_handle)
            if target is None:
                raise ValueError("object handle is invalid for this session; handles are session-local")
            session = entry["session"]
            session.set_position(target[1], expected_revision, float(x), float(y), float(z))
            return json.dumps({"session_id": session_id, "revision": session.revision}, indent=2)

    @server.tool(annotations=in_memory)
    def undo_scene_position(session_id: str, object_handle: str, expected_revision: int) -> str:
        """Undo the most recent position edit for this handle in the in-memory session; no disk write occurs."""
        if isinstance(expected_revision, bool) or not isinstance(expected_revision, int) or expected_revision < 0:
            raise ValueError("expected_revision must be a nonnegative integer")
        with sessions_lock:
            entry = lookup_session(session_id)
            target = entry["handles"].get(object_handle)
            if target is None:
                raise ValueError("object handle is invalid for this session; handles are session-local")
            session = entry["session"]
            session.undo_position(target[1], expected_revision)
            return json.dumps({"session_id": session_id, "revision": session.revision}, indent=2)

    @server.tool(annotations=close_session)
    def close_edit_session(session_id: str) -> str:
        """Close a headless in-memory editing session and discard its unsaved edits."""
        with sessions_lock:
            if session_id not in sessions:
                raise ValueError("editing session is unknown, closed, or expired after server restart")
            del sessions[session_id]
        return json.dumps({"session_id": session_id, "closed": True}, indent=2)

    return server


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", required=True, type=Path)
    parser.add_argument("--resource-root", required=True, type=Path)
    parser.add_argument("--scene-helper", required=True, type=Path)
    parser.add_argument("--session-module-dir", required=True, type=Path)
    args = parser.parse_args()
    try:
        project_root = args.project_root.resolve(strict=True)
        resource_root = args.resource_root.resolve(strict=True)
        helper = args.scene_helper.resolve(strict=True)
        session_module_dir = args.session_module_dir.resolve(strict=True)
        if (not project_root.is_dir() or not resource_root.is_dir() or not helper.is_file() or
                not session_module_dir.is_dir()):
            raise ValueError("project/resource/module roots must be directories and scene helper must be a file")
        # The resource root must itself be inside the selected project root.
        resource_root.relative_to(project_root)
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"invalid MCP roots/helper: {exc}", file=sys.stderr)
        raise SystemExit(2)
    sys.path.insert(0, str(session_module_dir))
    try:
        session_module = importlib.import_module("imhotep_scene_session")
    except ImportError as exc:
        print(f"could not load C++ SceneSession binding: {exc}", file=sys.stderr)
        raise SystemExit(2)
    make_server(project_root, resource_root, helper, session_module).run(transport="stdio")


if __name__ == "__main__":
    main()
