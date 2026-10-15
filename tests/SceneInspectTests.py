import pathlib
import json
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools/mcp"))
from project_files import contained_path, list_resources


helper = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="imhotep-mcp-test-") as temporary:
    root = pathlib.Path(temporary)
    (root / "res/scenes").mkdir(parents=True)
    valid = root / "res/scenes/valid.yaml"
    valid.write_text("name: Test\nscene:\n  objects: []\n", encoding="utf-8")
    malformed = root / "res/scenes/bad.yaml"
    malformed.write_text("scene: [\n", encoding="utf-8")

    def run(path):
        return subprocess.run([str(helper), str(root), path], text=True, capture_output=True)

    result = run("res/scenes/valid.yaml")
    assert result.returncode == 0, result.stderr
    assert "objects: []" in result.stdout
    assert result.stderr == ""
    assert run("res/scenes/missing.yaml").returncode != 0
    assert run("res/scenes/bad.yaml").returncode != 0
    assert run("../../etc/passwd").returncode != 0
    assert contained_path(root, "res/scenes/valid.yaml") == valid.resolve()
    try:
        contained_path(root, "../../etc/passwd")
    except ValueError:
        pass
    else:
        raise AssertionError("traversal should be rejected")
    resources = root / "res"
    (resources / "models").mkdir()
    (resources / "models/cube.obj").write_text("v 0 0 0\n", encoding="utf-8")
    try:
        (root / "escape.yaml").symlink_to("/etc/passwd")
    except OSError:
        pass
    else:
        assert run("escape.yaml").returncode != 0
        (resources / "escape.yaml").symlink_to("/etc/passwd")
        (resources / "models/escape.obj").symlink_to("/etc/passwd")
    listed = json.loads(list_resources(resources))
    assert "models/cube.obj" in listed["resources"]
    assert "escape.yaml" not in listed["resources"]
    assert "models/escape.obj" not in listed["resources"]

print("scene inspection boundary tests passed")
