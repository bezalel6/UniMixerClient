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

    url = 'http://rndev.local:3000/api/upload'

    try:
        with open(firmware_path, 'rb') as f:
            response = requests.post(url, files={'firmware': f}, timeout=5)
            print(f"[Upload] HTTP {response.status_code}: {response.text}")
    except requests.exceptions.RequestException as e:
        print(f"[Upload] HTTP upload failed: {e}")

# Attach to the firmware.bin build target specifically
env.AddPostAction("$BUILD_DIR/firmware.bin", after_firmware_build)
