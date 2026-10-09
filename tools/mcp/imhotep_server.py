#!/usr/bin/env python3
"""Read-only local MCP tools for authored Imhotep files."""

import argparse
from pathlib import Path
import subprocess
import sys

from mcp.server.fastmcp import FastMCP
from project_files import contained_path, list_resources as enumerate_resources


def make_server(project_root: Path, resource_root: Path, helper: Path) -> FastMCP:
    server = FastMCP("Imhotep read-only inspection")

    @server.tool()
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

    @server.tool()
    def list_resources() -> str:
        """List regular project resource files under the configured resource root; symlink escapes are omitted."""
        return enumerate_resources(resource_root)

    return server


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", required=True, type=Path)
    parser.add_argument("--resource-root", required=True, type=Path)
    parser.add_argument("--scene-helper", required=True, type=Path)
    args = parser.parse_args()
    try:
        project_root = args.project_root.resolve(strict=True)
        resource_root = args.resource_root.resolve(strict=True)
        helper = args.scene_helper.resolve(strict=True)
        if not project_root.is_dir() or not resource_root.is_dir() or not helper.is_file():
            raise ValueError("project/resource roots must be directories and scene helper must be a file")
        # The resource root must itself be inside the selected project root.
        resource_root.relative_to(project_root)
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"invalid MCP roots/helper: {exc}", file=sys.stderr)
        raise SystemExit(2)
    make_server(project_root, resource_root, helper).run(transport="stdio")


if __name__ == "__main__":
    main()
