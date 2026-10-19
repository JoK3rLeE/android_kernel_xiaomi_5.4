# Cepheus hardware porting to Linux 5.4

Target branch: `5.4-port`
Reference branch: `derp-17-qcacld-up`
Device: Xiaomi Mi 9 (Cepheus), Qualcomm SM8150

## Porting rule

Port the hardware integration, not just the 4.14 config symbols. For each device feature, carry over the driver implementation, Kconfig/Makefile integration, device-tree bindings/nodes, pinctrl/regulators/interrupts, and any userspace ABI that the ROM expects. Keep the existing firmware blobs untouched unless a separate, explicit firmware task is agreed.

## Cepheus-specific driver inventory

| Feature | 4.14 Cepheus config / implementation clue | 5.4 baseline status | Required action |
|---|---|---|---|
| Platform identity | `CONFIG_MACH_XIAOMI_CEPHEUS=y` | Raphael-oriented device tree/config is present; Cepheus device integration is not | Port Cepheus board DTS/DTSI and overlays; don't rename Raphael nodes to Cepheus mechanically |
| Touchscreen | `CONFIG_TOUCHSCREEN_ST_FTS_V521`, FOD area report, touchscreen-common | FTS V521 driver and its Kconfig/Makefile integration are now present. The 5.4 DRM stack uses `drm_panel_notifier`, while the imported driver expects the legacy MSM notifier ABI | Audit FTS API compatibility and build. A legacy notifier shim now bridges SDE blank events; verify suspend/resume and gesture behavior on device |
| Smart amplifier | `CONFIG_SND_SOC_CS35L41=y` in the 4.14 Cepheus config | The 4.14 codec lives under `techpack/audio/asoc/codecs/cs35l41`; the 5.4 target lacks that Qualcomm audio techpack path and has no CS35L41 symbol in `sound/soc/codecs/Kconfig`. `CONFIG_MSM_CSPL` was not found as a Kconfig definition in the inspected 4.14 paths | Port/adapt the CS35L41 codec into the 5.4 ASoC build and trace DSP, firmware, machine-card and DT dependencies. Verify whether `CONFIG_MSM_CSPL` is a stale config symbol before implementing it |
| Camera actuator | `CONFIG_VCM_AK7374=y` appears in the 4.14 config, but no matching Kconfig symbol or AK7374 source was found in the inspected 4.14 tree | The 4.14 camera stack uses generic `msm_actuator`; 5.4 uses the newer `techpack/camera-msmnile` `cam_actuator` framework | Reverse-map the Cepheus actuator's tuning/registration to the 5.4 camera framework and sensor DT. Do not port the unsupported `VCM_AK7374` symbol blindly |
| Wireless charging | `CONFIG_IDT_P9220=y` | IDT P9220 driver plus Kconfig/Makefile integration have been added | Audit driver API compatibility and add/verify the Cepheus I2C node, IRQ/GPIO wiring and wireless-charging userspace ABI |
| SMB1390 charge pump | Shared SM8150 config enables `CONFIG_SMB1390_CHARGE_PUMP=y` | Both class-based and PSY-oriented integrations exist in the 5.4 tree; Raphael has a device-specific PSY/IIO adaptation | Preserve the Cepheus charging topology and userspace ABI. Do not copy Raphael's PSY conversion or its voltage/charge-pump policy without checking Cepheus DT and userspace expectations |
| Shared SM8150 hardware | Fingerprint, haptics, IR, proximity, simtray, speaker ID, Trusty, NFC and QCACLD options in common config | Many shared drivers/config options already exist in the 5.4 common fragment | Reconcile each option against Cepheus DT and the ROM/vendor stack; only shared silicon/platform features should remain common |
| Display | EA8076 Samsung AMOLED family and Cepheus panel timings | Target DTS tree is Raphael-oriented | Port Cepheus panel description, command sets, reset/TE GPIOs, regulators, timing and display integration as a separate validated unit |
| Sensors and battery | Cepheus board-specific sensors, battery profile and charger topology | Raphael board data is not interchangeable | Port Cepheus sensor nodes, calibration, battery data and power-supply topology from Cepheus sources |

## Port order

1. Establish Cepheus board DTS/DTSI and overlay build integration.
2. Audit the imported FTS V521 driver against 5.4 APIs, preserve its gesture/FOD sysfs/input ABI, and validate the new DRM notifier compatibility bridge.
3. Port the CS35L41 + CSPL audio stack and machine-card integration.
4. Port AK7374 actuator integration with Cepheus camera sensor descriptions.
5. Audit the imported IDT P9220 driver and verify the Cepheus wireless-charging topology.
6. Reconcile SMB1390/SMB5/FG integration against Cepheus DT and the ROM's `hvdcp_opti` expectations.
7. Port display, sensors, battery data, NFC and remaining board-specific nodes.
8. Build after each subsystem, then validate boot and hardware behavior on device.

## Current audit findings

- The imported FTS V521 source still contains the 4.14-era `linux/msm_drm_notify.h` dependency. The 5.4 display stack instead exposes `drm_panel_notifier`; a compatibility header, blocking notifier chain, and SDE event bridge have been added so the legacy touchscreen callback can receive blank events. This has not yet been compile-validated.
- `CONFIG_TOUCHSCREEN_COMMON` was referenced by the Cepheus config and touchscreen Makefile but had no Kconfig definition. A boolean Kconfig entry has now been added so the common touch sysfs interface can actually be enabled.
- The CS35L41 codec remains unported to the 5.4 ASoC build. `CONFIG_MSM_CSPL` and `CONFIG_VCM_AK7374` are not yet validated as real Kconfig symbols in the source tree; the latter has no matching AK7374 source in the inspected 4.14 tree. Both remain disabled until the actual 5.4 integration is understood.

## Configuration safety

The initial `cepheus.config` intentionally leaves unsupported driver symbols disabled. This avoids a misleading config that appears to enable hardware when the driver or DT integration is absent. Enable each symbol only after the implementation and build integration are present. The current config is a tracking baseline, not a claim that Cepheus is boot-ready on 5.4.

## Lessons from Raphael's 5.4 history

- Raphael-specific changes often include device-tree wiring and userspace ABI compatibility, not only driver code.
- The SMB1390 PSY/IIO work was driven by what `hvdcp_opti` looks up in sysfs/IIO. For Cepheus, first determine the expected interface and preserve the existing charging topology rather than blindly adopting Raphael's choice.
- Board-specific battery and charging changes must be reviewed for battery chemistry, float voltage, charger IC wiring, and thermal behavior before reuse.
- Keep unrelated QCACLD/Wi-Fi history separate from board hardware porting unless a specific Wi-Fi regression or dependency is demonstrated.
