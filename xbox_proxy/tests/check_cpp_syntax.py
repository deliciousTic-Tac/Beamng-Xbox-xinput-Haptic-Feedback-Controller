"""Supplemental grammar check only, not an MSVC compiler or SDK/ABI check.
Requires: pip install tree-sitter tree-sitter-cpp
"""
from pathlib import Path
import re
import sys

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / ".test-deps"))
from tree_sitter import Language, Parser
import tree_sitter_cpp

parser = Parser(Language(tree_sitter_cpp.language()))
failures = []
for file in (PROJECT / "xinput_proxy.cpp", PROJECT / "tests/native_tests.cpp", PROJECT / "load_test.cpp"):
    source = file.read_text(encoding="utf-8-sig")
    # The grammar does not expand SDK calling-convention/declaration macros.
    source = re.sub(r"\b(?:WINAPI|CALLBACK)\b", "", source)
    source = source.replace("__declspec(dllexport)", "")
    root = parser.parse(source.encode()).root_node
    def visit(node):
        if node.type == "ERROR" or node.is_missing:
            failures.append(f"{file.name}:{node.start_point.row + 1}: {node.type}")
        for child in node.children:
            visit(child)
    visit(root)
if failures:
    raise SystemExit("\n".join(failures))
print("PASS: C++ grammar, 3 files (SDK semantics and MSVC compilation NOT checked)")
