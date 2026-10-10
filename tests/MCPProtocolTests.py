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
    with tempfile.TemporaryDirectory(prefix="imhotep-mcp-protocol-") as temp:
        project = pathlib.Path(temp)
        resources = project / "res"
        (resources / "scenes").mkdir(parents=True)
        (resources / "models").mkdir()
        (resources / "models/cube.obj").write_text("v 0 0 0\n", encoding="utf-8")
        (resources / "scenes/valid.yaml").write_text(
            "name: ProtocolTest\nscene:\n  objects: []\n", encoding="utf-8"
        )
        (resources / "scenes/bad.yaml").write_text("scene: [\n", encoding="utf-8")
        try:
            (resources / "outside.yaml").symlink_to("/etc/passwd")
        except OSError:
            pass

        stderr = tempfile.TemporaryFile(mode="w+t")
        params = StdioServerParameters(
            command=sys.executable,
            args=[
                str(server), "--project-root", str(project),
                "--resource-root", str(resources), "--scene-helper", str(helper),
            ],
        )
        async with stdio_client(params, errlog=stderr) as (read, write):
            async with ClientSession(read, write) as client:
                initialized = await client.initialize()
                assert initialized.protocolVersion
                tools = await client.list_tools()
                assert {tool.name for tool in tools.tools} == {"inspect_scene", "list_resources"}

                valid = await client.call_tool("inspect_scene", {"scene_path": "res/scenes/valid.yaml"})
                assert not valid.isError
                assert "ProtocolTest" in valid.content[0].text

                listing = await client.call_tool("list_resources", {})
                assert not listing.isError
                resources_result = json.loads(listing.content[0].text)
                assert "models/cube.obj" in resources_result["resources"]
                assert "scenes/valid.yaml" in resources_result["resources"]
                assert "outside.yaml" not in resources_result["resources"]

                for path in ("res/scenes/missing.yaml", "res/scenes/bad.yaml", "../../etc/passwd"):
                    invalid = await client.call_tool("inspect_scene", {"scene_path": path})
                    assert invalid.isError, path
                malformed = await client.call_tool("inspect_scene", {})
                assert malformed.isError
                wrong_type = await client.call_tool("inspect_scene", {"scene_path": 7})
                assert wrong_type.isError

        # The SDK could only complete initialization and all requests if stdout contained
        # valid MCP framing exclusively; diagnostic output is captured separately.
        stderr.close()
        print(f"MCP protocol tests passed (negotiated {initialized.protocolVersion})")


asyncio.run(main())
