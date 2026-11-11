"""The 32-bit ARM app's fixups of the guest's unaligned accesses
(port/android/host/host_alignment.c), run on the build machine.

host_alignment.c decodes the faulting instruction and performs it on the
registers a signal saved. Its code is plain C on those saved values, so it is
built here as a freestanding 32-bit x86 program (32-bit pointers, as on the
device), with stand-in headers for Android's (tools/host_alignment/include),
and fed the ARM assembler's encodings of the instructions it handles and of
some it must refuse (tools/host_alignment/test.c).
"""

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
TEST_DIR = ROOT / "tools" / "host_alignment"


def test_unaligned_accesses_are_fixed_up(tmp_path):
    clang = shutil.which("clang")
    if not clang:
        pytest.skip("no clang")
    # (a copy beside nothing else: the source includes "host.h", which must
    # be the stand-in's, not port/android/host's)
    shutil.copy(ROOT / "port" / "android" / "host" / "host_alignment.c", tmp_path / "host_alignment.c")
    resource = subprocess.run([clang, "-print-resource-dir"], capture_output=True, text=True, check=True).stdout.strip()
    program = tmp_path / "test"
    build = subprocess.run(
        [clang, "-m32", "-ffreestanding", "-nostdlib", "-nostdinc", "-isystem", f"{resource}/include",
         f"-I{TEST_DIR / 'include'}", "-D__arm__", "-O1", "-w", "-static", "-fuse-ld=lld",
         "-o", str(program), str(TEST_DIR / "test.c"), str(tmp_path / "host_alignment.c")],
        capture_output=True, text=True)
    if build.returncode != 0:
        pytest.skip(f"cannot build a freestanding 32-bit x86 program: {build.stderr[-300:]}")
    result = subprocess.run([str(program)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout
    assert "all passed" in result.stdout
