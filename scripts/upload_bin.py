import os
import requests
from SCons.Script import Import

Import("env")

def after_firmware_build(source, target, env):
    print("[Upload] Post-processing: Firmware built, attempting upload...")

    # The target should be the firmware.bin file
    if target and len(target) > 0:
        firmware_path = str(target[0])
    else:
        # Fallback to standard path
        target_dir = env.subst("$BUILD_DIR")
        firmware_path = os.path.join(target_dir, "firmware.bin")

    print(f"[Upload] Using firmware path: {firmware_path}")

    if not os.path.exists(firmware_path):
        print(f"[Upload] No firmware found at {firmware_path} - skipping upload")
        return

        # Find the corresponding .elf file for exception decoding
    elf_path = firmware_path.replace('.bin', '.elf')

    # Create debug files directory for exception decoding
    debug_dir = "../UniMixerServer/debug_files"
    os.makedirs(debug_dir, exist_ok=True)

    try:
        # Copy firmware.bin to debug directory
        import shutil
        target_bin = os.path.join(debug_dir, "firmware.bin")
        shutil.copy2(firmware_path, target_bin)
        print(f"[Upload] Copied firmware.bin to {target_bin}")

        # Copy firmware.elf if it exists (for exception decoding)
        if os.path.exists(elf_path):
            target_elf = os.path.join(debug_dir, "firmware.elf")
            shutil.copy2(elf_path, target_elf)
            print(f"[Upload] Copied firmware.elf to {target_elf}")
            print(f"[Upload] Debug files ready for exception decoding")
        else:
            print(f"[Upload] ELF file not found at {elf_path}, firmware.bin only")

        # Also try HTTP upload for compatibility (if server is running)
        url = 'http://rndev.local:3000/api/upload'
        try:
            with open(firmware_path, 'rb') as f:
                response = requests.post(url, files={'firmware': f}, timeout=5)
                print(f"[Upload] HTTP {response.status_code}: {response.text}")
        except requests.exceptions.RequestException:
            print(f"[Upload] HTTP upload skipped (server not running)")

    except Exception as e:
        print(f"[Upload] File copy failed: {e}")

# Attach to the firmware.bin build target specifically
env.AddPostAction("$BUILD_DIR/firmware.bin", after_firmware_build)
