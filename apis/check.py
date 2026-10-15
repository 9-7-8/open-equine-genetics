"""Builds and runs every port it finds a compiler for, and compares each one's
conformance report with the Python one.

    python check.py

Build output goes to a temporary folder. A port whose compiler is not found is
skipped and said so. For MSVC, run this from a Developer Command Prompt.
Exit 0 if every port that ran matched.
"""

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def run(cmd, cwd):
    return subprocess.run(cmd, cwd=cwd, capture_output=True)


def exe(path):
    return path + (".exe" if os.name == "nt" else "")


def java(tmp):
    if not (shutil.which("javac") and shutil.which("java")):
        return None
    d = os.path.join(HERE, "java")
    built = run(["javac", "-d", tmp, "OpenEquineGenetics.java", "Conformance.java"], d)
    if built.returncode:
        return built
    return run(["java", "-cp", tmp, "Conformance", ROOT], d)


def rust(tmp):
    if not shutil.which("cargo"):
        return None
    env = dict(os.environ, CARGO_TARGET_DIR=os.path.join(tmp, "target"))
    return subprocess.run(["cargo", "run", "--quiet", "--example", "conformance", "--", ROOT],
                          cwd=os.path.join(HERE, "rust"), capture_output=True, env=env)


def native(tmp, folder, sources, gnu, msvc_flags):
    d = os.path.join(HERE, folder)
    out = exe(os.path.join(tmp, "conformance"))
    for compiler in gnu:
        if shutil.which(compiler[0]):
            built = run(compiler + sources + ["-o", out], d)
            break
    else:
        if not shutil.which("cl"):
            return None
        built = run(["cl", "/nologo"] + msvc_flags + sources + ["/Fe" + out, "/Fo" + tmp + os.sep], d)
    if built.returncode:
        return built
    return run([out, ROOT], d)


PORTS = {
    "java": java,
    "rust": rust,
    "cpp": lambda tmp: native(tmp, "cpp", ["conformance.cpp"],
                              [["g++", "-std=c++17", "-O2"], ["clang++", "-std=c++17", "-O2"]],
                              ["/std:c++17", "/EHsc", "/O2"]),
    "c": lambda tmp: native(tmp, "c", ["conformance.c", "open_equine_genetics.c"],
                            [["gcc", "-std=c99", "-O2"], ["clang", "-std=c99", "-O2"]],
                            ["/O2"]),
}


def main():
    expected = run([sys.executable, "conformance.py", ROOT], os.path.join(HERE, "python")).stdout
    failed = False
    for name, port in PORTS.items():
        with tempfile.TemporaryDirectory() as tmp:
            result = port(tmp)
        if result is None:
            print(f"{name:5} skipped: no compiler found")
        elif result.returncode:
            failed = True
            print(f"{name:5} FAILED to build or run:\n{(result.stdout + result.stderr).decode(errors='replace')}")
        elif result.stdout != expected:
            failed = True
            got, want = result.stdout.decode(errors="replace").split("\n"), expected.decode().split("\n")
            line = next((i for i, (a, b) in enumerate(zip(got, want)) if a != b), min(len(got), len(want)))
            print(f"{name:5} DIFFERS from python at line {line + 1}:\n  python: {want[line] if line < len(want) else '(end)'}"
                  f"\n  {name}: {got[line] if line < len(got) else '(end)'}")
        else:
            print(f"{name:5} matches python ({len(expected.splitlines())} lines)")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
