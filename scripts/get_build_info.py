#!/usr/bin/env python3
"""
Build information script for PlatformIO
Inserts or replaces build flags in ../PIOConfig.h before the final #endif
"""

import subprocess
import os
from datetime import datetime

PIO_CONFIG_PATH = os.path.join(os.path.dirname(__file__), "../PIOConfig.h")
BEGIN_MARK = "// >>> AUTO-GENERATED BUILD INFO BEGIN"
END_MARK = "// <<< AUTO-GENERATED BUILD INFO END"

def get_git_commit_hash():
    try:
        result = subprocess.run(
            ['git', 'rev-parse', '--short', 'HEAD'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=5
        )
        if result.returncode == 0:
            return result.stdout.strip()
    except:
        pass
    return "dev"

def get_git_branch():
    try:
        result = subprocess.run(
            ['git', 'rev-parse', '--abbrev-ref', 'HEAD'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=5
        )
        if result.returncode == 0:
            branch = result.stdout.strip()
            return "detached" if branch == "HEAD" else branch
    except:
        pass
    return "unknown"

def main():
    commit_hash = get_git_commit_hash()
    branch = get_git_branch()
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    generated_block = [
        BEGIN_MARK,
        f'#define FIRMWARE_BUILD_NUMBER "{commit_hash}"',
        f'#define GIT_BRANCH "{branch}"',
        f'#define BUILD_TIMESTAMP_NUM "{timestamp}"',
        END_MARK
    ]

    with open(PIO_CONFIG_PATH, "r", encoding="utf-8") as f:
        lines = f.readlines()

    # Remove existing auto-generated block if it exists
    start, end = None, None
    for i, line in enumerate(lines):
        if line.strip() == BEGIN_MARK:
            start = i
        elif line.strip() == END_MARK:
            end = i
            break

    if start is not None and end is not None:
        del lines[start:end+1]

    # Find the final #endif
    for i in reversed(range(len(lines))):
        if lines[i].strip() == "#endif":
            insert_index = i
            break
    else:
        raise RuntimeError("No #endif found in PIOConfig.h")

    # Insert block before #endif
    insert_lines = [line + "\n" for line in generated_block]
    updated_lines = lines[:insert_index] + insert_lines + lines[insert_index:]

    with open(PIO_CONFIG_PATH, "w", encoding="utf-8") as f:
        f.writelines(updated_lines)

if __name__ == "__main__":
    main()
