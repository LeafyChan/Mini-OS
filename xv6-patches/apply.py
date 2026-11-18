#!/usr/bin/env python3
import argparse
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PATCH_FILE = SCRIPT_DIR / "reference.patch"
DEFAULT_XV6 = SCRIPT_DIR.parent / "xv6"

def run(command, cwd=None):
    return subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

def fail(message):
    print(f"ERROR: {message}", file=sys.stderr)
    sys.exit(1)

def main():
    parser = argparse.ArgumentParser(
        description="Apply the MiniOS M5 patch to an xv6-riscv tree."
    )
    parser.add_argument(
        "--xv6",
        type=Path,
        default=DEFAULT_XV6,
        help="Path to the xv6 source directory",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="Build xv6 after applying or verifying the patch",
    )
    args = parser.parse_args()

    xv6_dir = args.xv6.resolve()

    if not PATCH_FILE.is_file():
        fail(f"Reference patch not found: {PATCH_FILE}")

    if not xv6_dir.is_dir():
        fail(f"xv6 directory does not exist: {xv6_dir}")

    check_repo = run(
        ["git", "-C", str(xv6_dir), "rev-parse", "--show-toplevel"]
    )
    is_repo = (
        check_repo.returncode == 0
        and Path(check_repo.stdout.strip()).resolve() == xv6_dir
    )

    def patch_cmd(*flags):
        if is_repo:
            return ["git", "-C", str(xv6_dir), "apply", *flags, str(PATCH_FILE)]
        mapped = {"--reverse": "-R", "--check": "--dry-run"}
        return ["patch", "-p1", "-s", "-f", "-d", str(xv6_dir),
                "-i", str(PATCH_FILE), *[mapped[f] for f in flags]]

    print(f"xv6 source: {xv6_dir}")
    print(f"Patch file: {PATCH_FILE}")

    reverse_check = run(patch_cmd("--reverse", "--check"))

    if reverse_check.returncode == 0:
        print("M5 patch is already applied. No changes needed.")

    else:
        forward_check = run(patch_cmd("--check"))

        if forward_check.returncode != 0:
            print(forward_check.stderr, file=sys.stderr)
            fail(
                "Patch cannot be applied cleanly. "
                "The xv6 source may differ from the expected baseline "
                "or may contain partial M5 changes."
            )

        apply_result = run(patch_cmd())

        if apply_result.returncode != 0:
            print(apply_result.stderr, file=sys.stderr)
            fail("Failed to apply the M5 patch.")

        print("M5 patch applied successfully.")

    if is_repo:
        whitespace_check = run([
            "git", "-C", str(xv6_dir),
            "diff", "--check"
        ])

        if whitespace_check.returncode != 0:
            print(whitespace_check.stderr, file=sys.stderr)
            fail("Whitespace validation failed.")

        print("Whitespace validation passed.")

    if args.build:
        print("Building xv6...")

        build = run(["make"], cwd=xv6_dir)

        if build.stdout:
            print(build.stdout)

        if build.returncode != 0:
            if build.stderr:
                print(build.stderr, file=sys.stderr)
            fail("xv6 build failed.")

        print("xv6 build completed successfully.")

    print("M5 patch operation completed.")

if __name__ == "__main__":
    main()
