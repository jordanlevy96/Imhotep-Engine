"""Bounded client for the Editor-owned Unix snapshot socket."""

import os
from pathlib import Path
import re
import socket
import stat
import time

MAX_HEADER_BYTES = 256
MAX_ERROR_BYTES = 512
MAX_IDENTITY_BYTES = 64
MAX_PATH_BYTES = 4096
MAX_SCENE_BYTES = 2 * 1024 * 1024
REQUEST_TIMEOUT_SECONDS = 4.0


class SnapshotError(ValueError):
    """A live Editor snapshot could not be read safely."""


def _validate_endpoint(endpoint: Path) -> None:
    if not endpoint.is_absolute() or endpoint != Path(os.path.normpath(endpoint)):
        raise SnapshotError("Live Editor socket path must be absolute and normalized.")
    try:
        resolved = endpoint.resolve(strict=True)
        if resolved != endpoint:
            raise SnapshotError("Live Editor socket path must not traverse symlinks.")
        parent = endpoint.parent
        parent_info = os.stat(parent, follow_symlinks=False)
        socket_info = os.stat(endpoint, follow_symlinks=False)
    except FileNotFoundError as exc:
        raise SnapshotError("Live Editor snapshot socket is unavailable; start the Editor with its explicit socket option.") from exc
    except OSError as exc:
        raise SnapshotError(f"Live Editor snapshot socket cannot be checked: {exc.strerror or exc}.") from exc
    if (not stat.S_ISDIR(parent_info.st_mode) or parent_info.st_uid != os.geteuid() or
            parent_info.st_mode & 0o077 or parent_info.st_mode & 0o700 != 0o700):
        raise SnapshotError("Live Editor socket directory must be owner-only and owned by this user.")
    if (not stat.S_ISSOCK(socket_info.st_mode) or socket_info.st_uid != os.geteuid() or
            socket_info.st_mode & 0o077):
        raise SnapshotError("Live Editor endpoint is not an owner-only socket owned by this user.")


def _recv_exact(connection: socket.socket, length: int, deadline: float) -> bytes:
    chunks = bytearray()
    while len(chunks) < length:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise SnapshotError("Live Editor snapshot request timed out.")
        connection.settimeout(remaining)
        try:
            chunk = connection.recv(min(length - len(chunks), 65536))
        except TimeoutError as exc:
            raise SnapshotError("Live Editor snapshot request timed out.") from exc
        except OSError as exc:
            raise SnapshotError(f"Live Editor stopped while sending its snapshot: {exc.strerror or exc}.") from exc
        if not chunk:
            raise SnapshotError("Live Editor snapshot bridge closed before the full response arrived.")
        chunks.extend(chunk)
    return bytes(chunks)


def _read_line(connection: socket.socket, deadline: float) -> bytes:
    line = bytearray()
    while len(line) <= MAX_HEADER_BYTES:
        char = _recv_exact(connection, 1, deadline)
        if char == b"\n":
            return bytes(line)
        line.extend(char)
    raise SnapshotError("Live Editor snapshot bridge returned an oversized response header.")


def _expect_eof(connection: socket.socket, deadline: float) -> None:
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise SnapshotError("Live Editor snapshot request timed out.")
    connection.settimeout(remaining)
    try:
        extra = connection.recv(1)
    except TimeoutError as exc:
        raise SnapshotError("Live Editor snapshot request timed out waiting for response close.") from exc
    except OSError as exc:
        raise SnapshotError(f"Live Editor stopped before closing its snapshot response: {exc.strerror or exc}.") from exc
    if extra:
        raise SnapshotError("Live Editor snapshot bridge sent trailing response bytes.")


def inspect_editor_scene(endpoint: Path) -> dict:
    """Fetch the current authored-scene snapshot from the configured Editor socket.

    Args:
        endpoint: Absolute normalized path to the owner-only Editor socket.

    Returns:
        Snapshot metadata and authored YAML, including unsaved Editor state.

    Raises:
        SnapshotError: If the endpoint is unsafe, unavailable, times out, or returns an invalid response.
    """
    if not hasattr(socket, "AF_UNIX") or not hasattr(os, "geteuid"):
        raise SnapshotError("Live Editor snapshots require Unix-domain socket support.")
    _validate_endpoint(endpoint)
    deadline = time.monotonic() + REQUEST_TIMEOUT_SECONDS
    try:
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    except OSError as exc:
        raise SnapshotError(f"Unix-domain sockets are unavailable: {exc.strerror or exc}.") from exc
    try:
        remaining = deadline - time.monotonic()
        connection.settimeout(remaining)
        try:
            connection.connect(str(endpoint))
        except FileNotFoundError as exc:
            raise SnapshotError("Live Editor snapshot socket is unavailable; the Editor may be stopped.") from exc
        except (ConnectionRefusedError, TimeoutError) as exc:
            raise SnapshotError("Live Editor is stopped or not accepting snapshot requests.") from exc
        except OSError as exc:
            raise SnapshotError(f"Could not connect to the live Editor snapshot socket: {exc.strerror or exc}.") from exc
        connection.sendall(b"snapshot\n")
        connection.shutdown(socket.SHUT_WR)
        header = _read_line(connection, deadline).decode("ascii", errors="strict")
        if header.startswith("IMHOTEP_SNAPSHOT/1 ERROR "):
            match = re.fullmatch(r"IMHOTEP_SNAPSHOT/1 ERROR (\d+)", header)
            if not match:
                raise SnapshotError("Live Editor snapshot bridge returned a malformed error header.")
            error_length = int(match.group(1))
            if error_length > MAX_ERROR_BYTES:
                raise SnapshotError("Live Editor snapshot bridge returned an oversized error.")
            error_bytes = _recv_exact(connection, error_length, deadline)
            _expect_eof(connection, deadline)
            try:
                error = error_bytes.decode("utf-8")
            except UnicodeDecodeError as exc:
                raise SnapshotError("Live Editor snapshot bridge returned non-UTF-8 error text.") from exc
            raise SnapshotError(f"Live Editor could not provide a snapshot: {error}")

        match = re.fullmatch(r"IMHOTEP_SNAPSHOT/1 OK (\d+) (\d+) ([01]) (\d+) (\d+)", header)
        if not match:
            raise SnapshotError("Live Editor snapshot bridge returned a malformed response header.")
        identity_length, revision, dirty, path_length, scene_length = map(int, match.groups())
        if (identity_length < 1 or identity_length > MAX_IDENTITY_BYTES or revision > 0xFFFFFFFFFFFFFFFF or
                path_length > MAX_PATH_BYTES or
                scene_length > MAX_SCENE_BYTES):
            raise SnapshotError("Live Editor snapshot bridge response exceeds configured size limits.")
        identity = _recv_exact(connection, identity_length, deadline).decode("utf-8", errors="strict")
        scene_path = _recv_exact(connection, path_length, deadline).decode("utf-8", errors="strict")
        scene_yaml = _recv_exact(connection, scene_length, deadline).decode("utf-8", errors="strict")
        _expect_eof(connection, deadline)
        return {
            "source": "live_editor_authored_snapshot",
            "saved_file": False,
            "document_identity": identity,
            "revision": revision,
            "dirty": dirty == 1,
            "scene_path": scene_path,
            "scene_yaml": scene_yaml,
        }
    except UnicodeDecodeError as exc:
        raise SnapshotError("Live Editor snapshot bridge returned non-UTF-8 response data.") from exc
    except TimeoutError as exc:
        raise SnapshotError("Live Editor snapshot request timed out.") from exc
    except OSError as exc:
        raise SnapshotError(f"Live Editor snapshot connection failed: {exc.strerror or exc}.") from exc
    finally:
        connection.close()
