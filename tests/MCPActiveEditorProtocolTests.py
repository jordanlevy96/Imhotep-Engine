"""Exercise the optional live Editor tool through the official MCP Python SDK."""

import asyncio
from contextlib import asynccontextmanager
import json
import os
from pathlib import Path
import socket
import sys
import tempfile
import threading
import time

from mcp import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client

repository = Path(__file__).resolve().parents[1]
scene_helper = Path(sys.argv[1]).resolve()
server_script = repository / "tools/mcp/imhotep_server.py"
scene_yaml = "name: UnsavedDraft\nscene:\n  objects:\n    - name: light\n      transform: {pos: {x: 99, y: 2, z: 3}}\n"


class TemporarySnapshotProvider:
    def __init__(self, root: Path, mode: str = "valid"):
        self.root = root
        self.mode = mode
        self.endpoint_dir = root / f"s-{mode}"
        self.endpoint_dir.mkdir(mode=0o700)
        os.chmod(self.endpoint_dir, 0o700)
        self.endpoint = self.endpoint_dir / "e"
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.listener.bind(str(self.endpoint))
        os.chmod(self.endpoint, 0o600)
        self.listener.listen(1)
        self.listener.settimeout(8)
        self.request_seen = threading.Event()
        self.failure = None
        self.worker = threading.Thread(target=self._serve, daemon=True)
        self.worker.start()

    def _serve(self):
        try:
            connection, _ = self.listener.accept()
            with connection:
                connection.settimeout(3)
                request = bytearray()
                while True:
                    chunk = connection.recv(64)
                    if not chunk:
                        break
                    request.extend(chunk)
                self.request_seen.set()
                if bytes(request) != b"snapshot\n":
                    raise AssertionError(f"unexpected Editor bridge request: {request!r}")
                if self.mode == "timeout":
                    time.sleep(5)
                    return
                if self.mode == "malformed":
                    connection.sendall(b"NOT_A_SNAPSHOT_FRAME\n")
                    return
                if self.mode == "oversized":
                    connection.sendall(b"IMHOTEP_SNAPSHOT/1 OK 18 42 1 10 2097153\n")
                    return
                identity = b"unsaved-document-7"
                scene_path = str(self.root / "res/scenes/unsaved.yaml").encode("utf-8")
                body = scene_yaml.encode("utf-8")
                response = (f"IMHOTEP_SNAPSHOT/1 OK {len(identity)} 42 1 {len(scene_path)} {len(body)}\n".encode("ascii") +
                            identity + scene_path + body)
                connection.sendall(response)
        except (OSError, TimeoutError) as exc:
            if not self.request_seen.is_set():
                self.failure = exc
        finally:
            self.listener.close()
            try:
                self.endpoint.unlink()
            except FileNotFoundError:
                pass

    def close(self):
        self.listener.close()
        self.worker.join(timeout=6)
        if self.worker.is_alive():
            raise AssertionError("temporary snapshot provider did not stop")
        if self.failure is not None:
            raise AssertionError(f"temporary snapshot provider failed: {self.failure}")


@asynccontextmanager
async def mcp_client(project: Path, endpoint: Path):
    stderr = tempfile.TemporaryFile(mode="w+t")
    params = StdioServerParameters(
        command=sys.executable,
        args=[
            str(server_script),
            "--project-root", str(project),
            "--resource-root", str(project / "res"),
            "--scene-helper", str(scene_helper),
            "--session-module-dir", str(scene_helper.parent),
            "--editor-snapshot-socket", str(endpoint),
        ],
    )
    try:
        async with stdio_client(params, errlog=stderr) as (read, write):
            async with ClientSession(read, write) as client:
                initialized = await client.initialize()
                assert initialized.protocolVersion
                yield client
    finally:
        stderr.close()


async def expect_error(client: ClientSession, expected: str):
    result = await client.call_tool("inspect_editor_scene", {})
    assert result.isError, result
    assert expected.lower() in result.content[0].text.lower(), result.content[0].text


async def main():
    with tempfile.TemporaryDirectory(prefix="im-m-") as temp:
        root = Path(temp).resolve()
        (root / "res/scenes").mkdir(parents=True)
        saved_file = root / "res/scenes/unsaved.yaml"
        saved_bytes = b"name: SavedVersion\nscene: {objects: []}\n"
        saved_file.write_bytes(saved_bytes)

        provider = TemporarySnapshotProvider(root)
        async with mcp_client(root, provider.endpoint) as client:
            tools = await client.list_tools()
            tool_map = {tool.name: tool for tool in tools.tools}
            assert "inspect_editor_scene" in tool_map
            assert tool_map["inspect_editor_scene"].annotations.readOnlyHint is True
            assert "live Editor" in tool_map["inspect_editor_scene"].description
            result = await client.call_tool("inspect_editor_scene", {})
            assert not result.isError, result
            snapshot = json.loads(result.content[0].text)
            expected_snapshot = {
                "source": "live_editor_authored_snapshot",
                "saved_file": False,
                "document_identity": "unsaved-document-7",
                "revision": 42,
                "dirty": True,
                "scene_path": str(saved_file),
                "scene_yaml": scene_yaml,
            }
            assert snapshot == expected_snapshot, f"snapshot mismatch: expected {expected_snapshot!r}, got {snapshot!r}"
            assert saved_file.read_bytes() == saved_bytes
        provider.close()

        missing_endpoint = root / "s-m/e"
        (root / "s-m").mkdir(mode=0o700)
        os.chmod(root / "s-m", 0o700)
        async with mcp_client(root, missing_endpoint) as client:
            await expect_error(client, "unavailable")

        shutdown_provider = TemporarySnapshotProvider(root, "shutdown")
        async with mcp_client(root, shutdown_provider.endpoint) as client:
            first = await client.call_tool("inspect_editor_scene", {})
            assert not first.isError, first
            shutdown_provider.worker.join(timeout=2)
            await expect_error(client, "unavailable")
        shutdown_provider.close()

        malformed_provider = TemporarySnapshotProvider(root, "malformed")
        async with mcp_client(root, malformed_provider.endpoint) as client:
            await expect_error(client, "malformed response header")
        malformed_provider.close()

        oversized_provider = TemporarySnapshotProvider(root, "oversized")
        async with mcp_client(root, oversized_provider.endpoint) as client:
            await expect_error(client, "exceeds configured size limits")
        oversized_provider.close()

        timeout_provider = TemporarySnapshotProvider(root, "timeout")
        async with mcp_client(root, timeout_provider.endpoint) as client:
            started = time.monotonic()
            await expect_error(client, "timed out")
            assert time.monotonic() - started < 5.0
        timeout_provider.close()
        assert saved_file.read_bytes() == saved_bytes
        print("Live Editor MCP protocol tests passed (unsaved snapshot, missing/shutdown socket, malformed/oversized response, timeout)")


asyncio.run(main())
