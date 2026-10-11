"""Filesystem boundaries shared by the local MCP tools."""

import json
import os
from pathlib import Path


def contained_path(root: Path, relative: str) -> Path:
    """Resolve a nonempty relative path under a configured root.

    Args:
        root: Existing root directory.
        relative: Relative path to resolve.

    Returns:
        The canonical contained path.

    Raises:
        ValueError: If the path is missing, absolute, or escapes the root.
    """
    root = root.resolve(strict=True)
    candidate = Path(relative)
    if not relative or candidate.is_absolute():
        raise ValueError("path must be nonempty and relative to its configured root")
    try:
        resolved = (root / candidate).resolve(strict=True)
        resolved.relative_to(root)
    except (OSError, RuntimeError, ValueError) as exc:
        raise ValueError("path is missing or escapes its configured root") from exc
    return resolved


def list_resources(resource_root: Path, limit: int = 5000) -> str:
    """Return project resource paths as JSON, omitting hidden and escaping symlinks.

    Args:
        resource_root: Existing resource directory to enumerate.
        limit: Maximum number of paths to include.

    Returns:
        JSON with a ``resources`` list and a ``truncated`` flag.
    """
    resource_root = resource_root.resolve(strict=True)
    entries = []
    for current, dirs, files in os.walk(resource_root, followlinks=False):
        current_path = Path(current)
        dirs[:] = sorted(
            name for name in dirs
            if not name.startswith(".") and not (current_path / name).is_symlink()
        )
        for name in sorted(files):
            path = current_path / name
            try:
                resolved = path.resolve(strict=True)
                resolved.relative_to(resource_root)
                if not resolved.is_file() or name.startswith("."):
                    continue
            except (OSError, RuntimeError, ValueError):
                continue
            entries.append(resolved.relative_to(resource_root).as_posix())
            if len(entries) >= limit:
                return json.dumps({"resources": entries, "truncated": True}, indent=2)
    return json.dumps({"resources": entries, "truncated": False}, indent=2)
