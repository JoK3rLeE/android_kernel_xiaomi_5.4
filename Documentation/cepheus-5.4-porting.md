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
| Goodix fingerprint/UDFPS | `fingerprint_goodix` node with reset/IRQ GPIOs | The `goodix,fingerprint` platform driver exists in `goodix_ta`, and the SM8150 common config enables `CONFIG_FINGERPRINT_GOODIX_TA` | Keep the existing driver; verify the Cepheus node, GPIO polarity and userspace HAL behavior on device |
| Smart amplifier | `CONFIG_SND_SOC_CS35L41=y` in the 4.14 Cepheus config | CS35L41 source, Kconfig, WM ADSP exports, techpack composite Kbuild integration and the Cepheus-specific QUAT MI2S DAI link have been added to 5.4; the Cepheus DT node is present | Audit the 4.14-to-5.4 ASoC API differences, firmware/DSP dependency, supplies and machine-card route; build and test before treating audio as working |
| Camera actuator | `CONFIG_VCM_AK7374=y` appears in the 4.14 config, but no matching Kconfig symbol or AK7374 source was found in the inspected 4.14 tree | The 4.14 camera stack uses generic `msm_actuator`; 5.4 uses the newer `techpack/camera-msmnile` `cam_actuator` framework | Reverse-map the Cepheus actuator's tuning/registration to the 5.4 camera framework and sensor DT. Do not port the unsupported `VCM_AK7374` symbol blindly |
| Wireless charging | `CONFIG_IDT_P9220=y` | IDT P9220 driver plus Kconfig/Makefile integration have been added | Audit driver API compatibility and verify the existing Cepheus I2C node, IRQ/GPIO wiring and wireless-charging userspace ABI |
| SMB1390 charge pump | Shared SM8150 config enables `CONFIG_SMB1390_CHARGE_PUMP=y` | Both class-based and PSY-oriented integrations exist in the 5.4 tree; Raphael has a device-specific PSY/IIO adaptation | Preserve the Cepheus charging topology and userspace ABI. Do not copy Raphael's PSY conversion or its voltage/charge-pump policy without checking Cepheus DT and userspace expectations |
| Shared SM8150 hardware | Fingerprint, haptics, IR, proximity, simtray, speaker ID, Trusty, NFC and QCACLD options in common config | Many shared drivers/config options already exist in the 5.4 common fragment | Reconcile each option against Cepheus DT and the ROM/vendor stack; only shared silicon/platform features should remain common |
| Display | EA8076 Samsung AMOLED family and Cepheus panel timings | Three Cepheus EA8076 panel descriptions and the board DTS are now present; common SDE panel selection and all panel variants still need validation | Compile the overlay and verify which panel variant is selected, reset/TE GPIOs, regulators, timings and screen-on/off behavior |
| Sensors and battery | Cepheus board-specific sensors, battery profile and charger topology | Raphael board data is not interchangeable | Port Cepheus sensor nodes, calibration, battery data and power-supply topology from Cepheus sources |

## Port order

1. Establish Cepheus board DTS/DTSI and overlay build integration.
2. Audit the imported FTS V521 driver against 5.4 APIs, preserve its gesture/FOD sysfs/input ABI, and validate the new DRM notifier compatibility bridge.
3. Compile-audit and validate the CS35L41 codec, Cepheus QUAT MI2S DAI link, firmware/DSP loading and speaker routing; do not add a separate CSPL symbol unless a real Kconfig option is found.
4. Reverse-map Cepheus actuator tuning and sensor registration into the 5.4 `cam_actuator` framework; do not assume a standalone AK7374 driver exists.
5. Audit the imported IDT P9220 driver and verify the Cepheus wireless-charging topology.
6. Reconcile SMB1390/SMB5/FG integration against Cepheus DT and the ROM's `hvdcp_opti` expectations.
7. Validate the added Cepheus EA8076 panel descriptions and board overlay; audit sensors, battery data and remaining board-specific nodes.
8. Build after each subsystem, then validate boot and hardware behavior on device.

## Current audit findings

- The imported FTS V521 source still contains the 4.14-era `linux/msm_drm_notify.h` dependency. The 5.4 display stack instead exposes `drm_panel_notifier`; a compatibility header, blocking notifier chain, and SDE event bridge have been added so the legacy touchscreen callback can receive blank events. This has not yet been compile-validated.
- The shared SM8150 NFC node defines `nfc_active` and `nfc_suspend` pinctrl states. The QTI I2C driver now retains these optional states and switches them during system suspend/resume, while leaving boards without those states supported. This PM change is source-reviewed but not runtime-tested.
- The Cepheus `fingerprint_goodix` node uses `compatible = "goodix,fingerprint"` and `goodix,gpio-reset`/`goodix,gpio-irq`, matching the existing Goodix TA platform driver. The common SM8150 config already enables that driver, so no duplicate fingerprint implementation or config symbol is needed.
- Raphael's 5.4 overlay adds PM8150B Type-C role/mode IIO channels to `&snd_934x`, although the 4.14 audio overlay does not. The equivalent channels have now been added to the Cepheus 5.4 overlay so the migrated audio machine has the same Type-C data inputs while preserving Cepheus's USB-C analog-audio routing. DTS compilation and headset testing remain outstanding.
- Do not copy Raphael's SMB1390 config override blindly: Raphael selects `CONFIG_SMB1390_CHARGE_PUMP_PSY`, while the original Cepheus 4.14 config inherits `CONFIG_SMB1390_CHARGE_PUMP`. The 5.4 Cepheus fragment currently preserves the legacy selection until the Cepheus userspace/IIO contract is proven to require the PSY variant.
- `CONFIG_TOUCHSCREEN_COMMON` already had a definition at the end of `drivers/input/touchscreen/Kconfig`. An earlier porting edit accidentally added a duplicate definition near the top; this has now been removed. The file has one definition, and the Cepheus config enables it.
- The CS35L41 codec sources, `SND_SOC_CS35L41` Kconfig entry, WM ADSP event exports, techpack composite-object integration and a Cepheus-only QUAT MI2S backend have been added. The 4.14 machine driver selected CS35L41 for Cepheus at runtime, while the 5.4 base only added Raphael's TFA backend; this missing machine-card link is now ported using the 5.4 DAI-link component API. The port is not build-validated; confirm Kconfig resolution, link symbols, firmware loading, and the actual Cepheus speaker route before calling audio complete. `CONFIG_MSM_CSPL` is not a real symbol in the inspected tree; CSPL mailbox handling is implemented within the codec driver. `CONFIG_VCM_AK7374` remains disabled because no matching standalone AK7374 source was found in the inspected 4.14 tree.

## Configuration safety

The `cepheus.config` fragment enables the FTS V521 touchscreen, P9220 wireless charging and CS35L41 codec integrations that now have source/build entries. The AK7374 standalone symbol remains disabled because no matching implementation was found. The fragment is not proof of a successful build or hardware operation; it must be merged with the intended SM8150 base config and validated.

## Lessons from Raphael's 5.4 history

- Raphael-specific changes often include device-tree wiring and userspace ABI compatibility, not only driver code.
- The SMB1390 PSY/IIO work was driven by what `hvdcp_opti` looks up in sysfs/IIO. For Cepheus, first determine the expected interface and preserve the existing charging topology rather than blindly adopting Raphael's choice.
- Board-specific battery and charging changes must be reviewed for battery chemistry, float voltage, charger IC wiring, and thermal behavior before reuse.
- Keep unrelated QCACLD/Wi-Fi history separate from board hardware porting unless a specific Wi-Fi regression or dependency is demonstrated.
