#!/usr/bin/env python3
"""
Build information script for PlatformIO
Generates build flags with git commit hash and other build info
"""

import subprocess
import sys
import os
from datetime import datetime

def get_git_commit_hash():
    """Get the short git commit hash, return 'dev' if not available"""
    try:
        # Try to get git commit hash
        result = subprocess.run(
            ['git', 'rev-parse', '--short', 'HEAD'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=5
        )
        if result.returncode == 0:
            return result.stdout.strip()
    except (subprocess.SubprocessError, FileNotFoundError, subprocess.TimeoutExpired):
        pass
    
    return "dev"

def get_git_branch():
    """Get the current git branch name, return 'unknown' if not available"""
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
            # Handle detached HEAD state
            if branch == "HEAD":
                return "detached"
            return branch
    except (subprocess.SubprocessError, FileNotFoundError, subprocess.TimeoutExpired):
        pass
    
    return "unknown"

def main():
    """Generate build flags for PlatformIO"""
    
    # Get build information
    commit_hash = get_git_commit_hash()
    branch = get_git_branch()
    
    # Generate build flags
    flags = []
    
    # Build number (git commit hash)
    flags.append(f'-DFIRMWARE_BUILD_NUMBER=\\"{commit_hash}\\"')
    
    # Git branch (optional)
    flags.append(f'-DGIT_BRANCH=\\"{branch}\\"')
    
    # Build timestamp (when this script runs)
    build_timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    flags.append(f'-DBUILD_TIMESTAMP_NUM=\\"{build_timestamp}\\"')
    
    # Output all flags on separate lines for PlatformIO
    for flag in flags:
        print(flag)

if __name__ == "__main__":
    main() 
