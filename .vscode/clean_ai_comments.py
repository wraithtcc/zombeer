from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent

# Matches things like:
# // ===== Main =====
# // ========== Initialization ==========
# // ===== Player System =====
# // =========================

pattern = re.compile(
    r'^\s*//\s*=+\s*.*?\s*=*\s*$'
)

extensions = {
    ".cpp",
    ".cc",
    ".cxx",
    ".h",
    ".hpp",
    ".hh",
    ".hxx"
}

files_changed = 0
lines_removed = 0

for path in ROOT.rglob("*"):
    if not path.is_file():
        continue

    if path.suffix.lower() not in extensions:
        continue

    # Don't touch .git or build directories
    if any(part in {".git", "build", "out", "bin"} for part in path.parts):
        continue

    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        continue

    lines = text.splitlines(keepends=True)

    new_lines = []
    removed = 0

    for line in lines:
        if pattern.match(line):
            removed += 1
        else:
            new_lines.append(line)

    if removed:
        path.write_text("".join(new_lines), encoding="utf-8")
        files_changed += 1
        lines_removed += removed

        print(f"Cleaned {removed} comment(s): {path}")

print()
print(f"Files changed: {files_changed}")
print(f"Comments removed: {lines_removed}")