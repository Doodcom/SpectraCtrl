# SpectraCtrl

Control Zotac SPECTRA RGB lighting on GeForce 40-series "4A" cards from
Linux. No Windows, no FireStorm, no kernel patches, no reboot.

Written for the ZOTAC GAMING RTX 4070 Ti SUPER Trinity Black Edition
(controller `N702E-1002`), but should work on any card using the same MCU:

- RTX 4060 Ti Twin Edge, RTX 4070 Twin Edge
- RTX 4070 Super / 4070 Ti Super / 4080 Super Trinity
- RTX 5070 Solid (`N764E-1001`)

## Why this exists

The Spectra RGB MCU sits on the GPU's i2c bus at address `0x4A`, but it only
talks at **10 kHz**. The kernel's `/dev/i2c-*` adapters for NVIDIA GPUs run at
a fixed 100 kHz, so the MCU never ACKs — which is why OpenRGB can't see it on
Linux (its Zotac "4A" support is Windows-only, via NvAPI's per-transaction
speed control).

SpectraCtrl skips `/dev/i2c` entirely and talks to the NVIDIA Resource
Manager through `/dev/nvidiactl`, issuing `NV402C_CTRL_CMD_I2C_TRANSACTION`
with `SPEED_MODE_10KHZ` — the exact same path FireStorm uses on Windows.
Works with the open kernel modules (tested on 610.43.03, CachyOS).

## Build

```
make
sudo make install    # -> /usr/local/bin/spectractl
```

No dependencies beyond a C compiler; the needed NVIDIA SDK headers (MIT
licensed) are vendored in `vendor/nvidia/`.

## Use

```
spectractl status                  # controller ID + current settings
spectractl color FF6600            # static color
spectractl mode breathe 28FF03 200 # effect [color] [speed 1-253]
spectractl mode fade               # modes: static breathe fade wink
spectractl mode random             #        spectrum random
spectractl off                     # lights out (settings kept)
spectractl on
spectractl reset                   # controller factory defaults
```

Settings are stored in the MCU itself and persist across reboots — set it
once and forget it. No daemon needed.

## GUI

There's an optional Electron front-end in `gui/`:

```
cd gui && npm install && npm start
```

Color picker, effect buttons, speed slider, live status. It just shells
out to `spectractl`, so install the CLI first.

## Notes

- The MCU wants ~200 ms between transactions and silently drops anything
  faster; spectractl paces and retries automatically.
- The i2c port is auto-detected and cached in `~/.cache/spectractl.port`.
- Runs as regular user (whoever can open `/dev/nvidiactl`).

## Credits

- Protocol reverse-engineered by Peter Berendi in
  [OpenRGB MR !2625](https://gitlab.com/CalcProgrammer1/OpenRGB/-/merge_requests/2625)
  (Windows-only there; this is an independent Linux implementation).
- Vendored headers from
  [NVIDIA/open-gpu-kernel-modules](https://github.com/NVIDIA/open-gpu-kernel-modules) (MIT).
