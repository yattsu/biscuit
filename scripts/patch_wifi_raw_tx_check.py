"""
PlatformIO pre-build script: weaken ieee80211_raw_frame_sanity_check in the
precompiled Espressif WiFi library so our own definition (WifiRawFrameBypass.cpp)
overrides it at link time.

Why not a linker --wrap: esp_wifi_80211_tx() and ieee80211_raw_frame_sanity_check()
are compiled into the same object file inside libnet80211.a. GNU ld's --wrap only
intercepts undefined references resolved *across* object/archive boundaries; a
call resolved within the same object file never goes through that substitution,
so --wrap=ieee80211_raw_frame_sanity_check silently never fires no matter what
flags accompany it (confirmed both by disassembling the linked firmware and by a
minimal reproduction outside this project).

`objcopy --weaken-symbol` sidesteps that entirely: it demotes the symbol's ELF
binding from GLOBAL to WEAK in the archive itself, so when our own strong
definition of ieee80211_raw_frame_sanity_check (in WifiRawFrameBypass.cpp) is
linked in, normal ELF symbol resolution (strong beats weak) picks ours instead --
for every caller, regardless of which object file it lives in. This is the
technique used by ESP32Marauder and other ESP32 deauth tooling for the same
underlying binutils limitation.

This targets the global PlatformIO framework-arduinoespressif32-libs package
(shared across projects on this machine, like patch_networkmanager.py), not a
per-project copy -- idempotent, safe to run every build.
"""

Import("env")  # noqa: F821
import glob
import os
import subprocess
import sys

SYMBOL = "ieee80211_raw_frame_sanity_check"

RISCV_MCUS = {"esp32c2", "esp32c3", "esp32c5", "esp32c6", "esp32h2", "esp32p4"}
TOOLCHAIN_BY_ARCH = {
    "riscv": "toolchain-riscv32-esp",
    "xtensa": "toolchain-xtensa-esp-elf",
}


def _find_objcopy(toolchain_dir):
    pattern = os.path.join(toolchain_dir, "bin", "*-objcopy*")
    matches = [
        p for p in glob.glob(pattern)
        if not p.endswith((".dll", ".py"))
    ]
    if not matches:
        raise SystemExit(
            f"patch_wifi_raw_tx_check.py: no objcopy found under {toolchain_dir}"
        )
    return matches[0]


def _symbol_is_weak(nm_path, lib_path):
    out = subprocess.run(
        [nm_path, lib_path], capture_output=True, text=True, check=True
    ).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-1] == SYMBOL:
            return parts[-2] in ("w", "W")
    return False


def patch_wifi_raw_tx_check(env):
    mcu = env.BoardConfig().get("build.mcu", "")
    if not mcu:
        print("patch_wifi_raw_tx_check.py: no board.build.mcu, skipping")
        return

    arch = "riscv" if mcu in RISCV_MCUS else "xtensa"
    toolchain_pkg = TOOLCHAIN_BY_ARCH[arch]
    toolchain_dir = env.PioPlatform().get_package_dir(toolchain_pkg)
    if not toolchain_dir:
        raise SystemExit(
            f"patch_wifi_raw_tx_check.py: package {toolchain_pkg} not installed"
        )

    libs_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs")
    if not libs_dir:
        raise SystemExit(
            "patch_wifi_raw_tx_check.py: framework-arduinoespressif32-libs not installed"
        )
    lib_path = os.path.join(libs_dir, mcu, "lib", "libnet80211.a")
    if not os.path.isfile(lib_path):
        raise SystemExit(
            f"patch_wifi_raw_tx_check.py: {lib_path} not found -- "
            "framework layout changed, update this script"
        )

    objcopy = _find_objcopy(toolchain_dir)
    nm_path = objcopy.replace("objcopy", "nm")

    if os.path.isfile(nm_path) and _symbol_is_weak(nm_path, lib_path):
        return  # already patched

    subprocess.run(
        [objcopy, f"--weaken-symbol={SYMBOL}", lib_path],
        check=True,
    )
    print(f"Weakened {SYMBOL} in {lib_path}")


patch_wifi_raw_tx_check(env)  # noqa: F821
