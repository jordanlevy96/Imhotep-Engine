"""Exercise the saved-scene-only stdio MCP profile with the official SDK."""

import asyncio
import json
import pathlib
import tempfile
import sys

from mcp import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client


repository = pathlib.Path(__file__).resolve().parents[1]
helper = pathlib.Path(sys.argv[1]).resolve()
server = repository / "tools/mcp/imhotep_server.py"


async def main():
    with tempfile.TemporaryDirectory(prefix="imhotep-mcp-readonly-") as temporary:
        project = pathlib.Path(temporary)
        resources = project / "res"
        (resources / "scenes").mkdir(parents=True)
        (resources / "models").mkdir()
        (resources / "scenes/valid.yaml").write_text(
            "name: ReadOnlyTest\nscene:\n  objects:\n    - name: light\n",
            encoding="utf-8",
        )
        (resources / "scenes/invalid.yaml").write_text("scene: [\n", encoding="utf-8")
        (resources / "models/cube.obj").write_text("v 0 0 0\n", encoding="utf-8")
        saved_bytes = (resources / "scenes/valid.yaml").read_bytes()
        try:
            (resources / "escape.obj").symlink_to("/etc/passwd")
        except OSError:
            pass

        stderr = tempfile.TemporaryFile(mode="w+t")
        params = StdioServerParameters(
            command=sys.executable,
            args=[
                str(server), "--project-root", str(project),
                "--resource-root", str(resources), "--scene-helper", str(helper),
                "--read-only",
            ],
        )
        async with stdio_client(params, errlog=stderr) as (read, write):
            async with ClientSession(read, write) as client:
                initialized = await client.initialize()
                assert initialized.protocolVersion
                tools = await client.list_tools()
                tool_map = {tool.name: tool for tool in tools.tools}
                assert set(tool_map) == {"inspect_scene", "list_resources"}
                assert all(tool.annotations.readOnlyHint is True for tool in tool_map.values())

                inspected = await client.call_tool("inspect_scene", {"scene_path": "res/scenes/valid.yaml"})
                assert not inspected.isError
                assert "ReadOnlyTest" in inspected.content[0].text

                listing = await client.call_tool("list_resources", {})
                assert not listing.isError
                listed = json.loads(listing.content[0].text)
                assert "models/cube.obj" in listed["resources"]
                assert "scenes/valid.yaml" in listed["resources"]
                assert "escape.obj" not in listed["resources"]

                for path in ("res/scenes/missing.yaml", "res/scenes/invalid.yaml", "../../etc/passwd"):
                    result = await client.call_tool("inspect_scene", {"scene_path": path})
                    assert result.isError, path
                assert (await client.call_tool("inspect_scene", {})).isError
                assert (await client.call_tool("inspect_scene", {"scene_path": 7})).isError
                assert (resources / "scenes/valid.yaml").read_bytes() == saved_bytes

        stderr.seek(0)
        protocol_output = stderr.read()
        stderr.close()
        # MCP framing is on stdout; SDK only succeeds when stdout stays protocol-clean.
        assert "Traceback" not in protocol_output
        print(f"Read-only MCP protocol tests passed (negotiated {initialized.protocolVersion})")


asyncio.run(main())
