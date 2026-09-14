"""Check repository integrity without requiring ML dependencies."""

import ast
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]


def main():
    sources = sorted((ROOT / "training").rglob("*.py")) + sorted((ROOT / "tools").glob("*.py"))
    for path in sources:
        ast.parse(path.read_text(encoding="utf-8-sig"), filename=str(path))
    projects = sorted((ROOT / "firmware").rglob("*.uvprojx"))
    references = 0
    for project in projects:
        tree = ET.parse(project)
        assert tree.findtext(".//Device") == "GD32H759IM", project
        for node in tree.findall(".//FilePath"):
            target = (project.parent / node.text.replace("\\", "/")).resolve()
            assert target.is_relative_to(ROOT), (project, node.text)
            assert target.is_file(), f"Missing project file: {target}"
            references += 1
        for node in tree.findall(".//IncludePath"):
            for item in (node.text or "").split(";"):
                if item:
                    target = (project.parent / item.replace("\\", "/")).resolve()
                    assert target.is_dir(), f"Missing include directory: {target}"
    manifest = json.loads((ROOT / "docs" / "artifacts.sha256.json").read_text(encoding="utf-8-sig"))
    for item in manifest:
        path = ROOT / item["path"]
        assert path.stat().st_size == item["bytes"], path
        assert hashlib.sha256(path.read_bytes()).hexdigest() == item["sha256"], path
    assert "基于 GD32H759I" in (ROOT / "README.md").read_text(encoding="utf-8")
    print(f"PASS: {len(sources)} Python files, {len(projects)} Keil projects, "
          f"{references} file references, {len(manifest)} artifact hashes")


if __name__ == "__main__":
    main()
