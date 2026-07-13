# Linux support for the Zotac 4A GPU controller (RM API i2c backend)

Companion patch to MR !2625 ("Zotac Trinity/Twin Edge"). That MR is
Windows-only because the Spectra MCU (addr 0x4A) only responds at 10 kHz
and on Linux the kernel's `/dev/i2c-*` adapters for NVIDIA GPUs run at a
fixed 100 kHz — `i2c_set_max_speed()` has nothing to act on there.

## What this adds

`i2c_smbus/Linux/i2c_smbus_nvrm.{h,cpp}` + `nvrm_api.h`:

- A Linux i2c bus implementation that issues transactions through the
  NVIDIA Resource Manager API (`/dev/nvidiactl` + `/dev/nvidiaN`), using
  `NV402C_CTRL_CMD_I2C_TRANSACTION`. This control accepts a per-transaction
  speed-mode flag (3/10/33/100/200/300/400 kHz) — it is the exact same RM
  control NvAPI's i2c functions wrap on Windows.
- `i2c_set_max_speed()` maps the requested Hz to the RM speed flag, so the
  existing `DetectZotac4AGPUControllers()` works unmodified on Linux.
- One bus is registered per NVIDIA GPU (enumerated from
  `/proc/driver/nvidia/gpus`), carrying the GPU's PCI IDs so
  `REGISTER_I2C_PCI_DETECTOR` matching works as usual. Port is fixed to 1,
  mirroring `i2c_smbus_nvapi.cpp` ("Always use GPU port 1").

## Duplicate-detection guard

The RM bus refuses all transfers until a detector has called
`i2c_set_max_speed()` with a value below 100 kHz. Devices reachable at
standard speed are already served by the `/dev/i2c-*` buses; without this
guard every GPU controller would be detected twice.

## ABI / driver compatibility notes

- `nvrm_api.h` defines only the needed subset of the RM ABI (struct
  layouts verified against NVIDIA/open-gpu-kernel-modules, MIT). The
  NV402C params struct is 96 bytes with the data union at offset 16;
  there's a `_Static_assert` guarding this.
- The RM version handshake uses `NV_RM_API_VERSION_CMD_RELAXED`.
- Works with both the proprietary and open kernel modules (the escape
  ioctl interface is identical). Needs R/W access to `/dev/nvidiactl`
  and `/dev/nvidia0`, which are 0666 by default.
- No root required.

## Tested

- ZOTAC GAMING RTX 4070 Ti SUPER Trinity Black Edition (10de:2705,
  19da:4710, controller `N702E-1002`), driver 610.43.03 open kernel
  modules, CachyOS, Qt 5.15: detected as "ZOTAC GAMING GeForce RTX 4070
  Ti Super Trinity", all modes and colors apply correctly, settings
  persist. DRAM/motherboard devices unaffected, no duplicates.
