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
            "name: ProtocolTest\nmetadata: {keep: yes}\nscene:\n  objects:\n"
            "    - name: light\n      extra: preserve\n      transform:\n"
            "        pos: {x: 1, y: 2, z: 3, extension: keep}\n"
            "      components: [{type: FutureComponent, value: keep}]\n",
            encoding="utf-8",
        )
        (resources / "scenes/bad.yaml").write_text("scene: [\n", encoding="utf-8")
        saved_bytes = (resources / "scenes/valid.yaml").read_bytes()
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
                    "--session-module-dir", str(helper.parent),
            ],
        )
        async with stdio_client(params, errlog=stderr) as (read, write):
            async with ClientSession(read, write) as client:
                initialized = await client.initialize()
                assert initialized.protocolVersion
                tools = await client.list_tools()
                tool_map = {tool.name: tool for tool in tools.tools}
                assert set(tool_map) == {
                    "inspect_scene", "list_resources", "open_edit_session", "inspect_edit_session",
                    "set_scene_position", "undo_scene_position", "close_edit_session",
                }
                assert tool_map["inspect_scene"].annotations.readOnlyHint is True
                assert tool_map["inspect_edit_session"].annotations.readOnlyHint is True
                assert tool_map["set_scene_position"].annotations.readOnlyHint is False
                assert tool_map["undo_scene_position"].annotations.readOnlyHint is False
                assert tool_map["close_edit_session"].annotations.destructiveHint is True

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
                for path in ("res/scenes/missing.yaml", "res/scenes/bad.yaml", "../../etc/passwd"):
                    rejected_open = await client.call_tool("open_edit_session", {"scene_path": path})
                    assert rejected_open.isError, path

                opened = await client.call_tool("open_edit_session", {"scene_path": "res/scenes/valid.yaml"})
                assert not opened.isError
                opened_data = json.loads(opened.content[0].text)
                session_id = opened_data["session_id"]
                assert opened_data["revision"] == 1
                light_handle = next(item["handle"] for item in opened_data["objects"] if item["name"] == "light")

                state = await client.call_tool("inspect_edit_session", {"session_id": session_id})
                state_data = json.loads(state.content[0].text)
                assert state_data["revision"] == 1 and "extension: keep" in state_data["scene_yaml"]

                edited = await client.call_tool("set_scene_position", {
                    "session_id": session_id, "object_handle": light_handle,
                    "expected_revision": 1, "x": 4, "y": 5, "z": 6,
                })
                assert not edited.isError
                assert json.loads(edited.content[0].text)["revision"] == 2
                stale = await client.call_tool("set_scene_position", {
                    "session_id": session_id, "object_handle": light_handle,
                    "expected_revision": 1, "x": 7, "y": 8, "z": 9,
                })
                assert stale.isError

                state = await client.call_tool("inspect_edit_session", {"session_id": session_id})
                state_data = json.loads(state.content[0].text)
                assert state_data["revision"] == 2 and "x: 4" in state_data["scene_yaml"]
                assert "FutureComponent" in state_data["scene_yaml"]

                undone = await client.call_tool("undo_scene_position", {
                    "session_id": session_id, "object_handle": light_handle, "expected_revision": 2,
                })
                assert not undone.isError and json.loads(undone.content[0].text)["revision"] == 3
                restored = await client.call_tool("inspect_edit_session", {"session_id": session_id})
                restored_data = json.loads(restored.content[0].text)
                assert "x: 1" in restored_data["scene_yaml"] and "extension: keep" in restored_data["scene_yaml"]

                malformed_edit = await client.call_tool("set_scene_position", {
                    "session_id": session_id, "object_handle": light_handle, "expected_revision": True,
                    "x": 1, "y": 2, "z": 3,
                })
                assert malformed_edit.isError

                # Simultaneous writes using one revision serialize: exactly one commits.
                concurrent_calls = await asyncio.gather(*[
                    client.call_tool("set_scene_position", {
                        "session_id": session_id, "object_handle": light_handle,
                        "expected_revision": 3, "x": value, "y": 2, "z": 3,
                    }) for value in (20, 30)
                ])
                assert sum(not call.isError for call in concurrent_calls) == 1
                state = await client.call_tool("inspect_edit_session", {"session_id": session_id})
                assert json.loads(state.content[0].text)["revision"] == 4

                opened_foreign = await client.call_tool("open_edit_session", {"scene_path": "res/scenes/valid.yaml"})
                foreign_data = json.loads(opened_foreign.content[0].text)
                foreign = await client.call_tool("set_scene_position", {
                    "session_id": foreign_data["session_id"], "object_handle": light_handle,
                    "expected_revision": 1, "x": 9, "y": 9, "z": 9,
                })
                assert foreign.isError
                assert not (await client.call_tool("close_edit_session", {"session_id": foreign_data["session_id"]})).isError

                assert (resources / "scenes/valid.yaml").read_bytes() == saved_bytes
                assert not (await client.call_tool("close_edit_session", {"session_id": session_id})).isError
                assert (await client.call_tool("inspect_edit_session", {"session_id": session_id})).isError

        # Session identifiers are process-local and cannot be resumed after server restart.
        restart_params = StdioServerParameters(
            command=sys.executable,
            args=[str(server), "--project-root", str(project), "--resource-root", str(resources),
                  "--scene-helper", str(helper), "--session-module-dir", str(helper.parent)],
        )
        async with stdio_client(restart_params, errlog=stderr) as (read, write):
            async with ClientSession(read, write) as restarted_client:
                await restarted_client.initialize()
                expired = await restarted_client.call_tool("inspect_edit_session", {"session_id": session_id})
                assert expired.isError
        assert (resources / "scenes/valid.yaml").read_bytes() == saved_bytes

        # The SDK could only complete initialization and all requests if stdout contained
        # valid MCP framing exclusively; diagnostic output is captured separately.
        stderr.close()
        print(f"MCP protocol tests passed (negotiated {initialized.protocolVersion})")


asyncio.run(main())
