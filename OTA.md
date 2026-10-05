# BLE OTA firmware update

How over-the-air firmware update (OTA DFU) was added to `bt_soc_thermometer_mock`, and how to build, flash and update.

**Status:** update v1 to v2 with Simplicity Connect passed on hardware. The own app (see [Own app](#own-app)) builds for Android and iOS but has not yet been run on a phone.

## Plan

Goal: update the application over BLE from a phone, as a development exercise.

| Topic | Decision | Why |
|-------|----------|-----|
| Method | In-place OTA DFU (Apploader) | 512 kB flash; no room needed for a second image slot |
| Security | Unsigned GBL, no secure boot, no bonding required | Bench exercise, not a product |
| Client | Simplicity Connect mobile app | Supports the Silicon Labs OTA service without client code |
| Project changes | Edit `.slcp`, regenerate with `slc` CLI | Reproducible; no hand edits in `autogen/` |
| Bootloader | SDK `bootloader-apploader` project, generated as sibling project | Same SDK version as the app |
| Flashing | Done manually with Simplicity Commander | |
| Proof of update | Version string in boot log on VCOM, and from v3 on as firmware revision (`2A26`) | Readable from the phone without a serial cable |

How in-place OTA works: the phone writes to the OTA control characteristic, the app reboots into the Apploader (part of the bootloader), which advertises as `OTA`, receives the GBL image, overwrites the application and reboots into it. The device has no working application while the transfer runs.

## Hardware and tools

- Module BGM220PC22HNA on BRD4314A, Simplicity SDK 2026.6.0, bare-metal.
- Tool paths used below:

```sh
SDK=/Users/mah/.silabs/slt/installs/conan/p/simpl508ee6c1a6569/p
SLC=/Users/mah/.silabs/slt/installs/archive/slc-cli-v6.0.25/slc_cli/slc
CMAKE=/Users/mah/.silabs/slt/installs/conan/p/cmake0491e58cd027f/p/CMake.app/Contents/bin/cmake
COMMANDER=/Users/mah/.silabs/slt/installs/archive/Commander.app/Contents/MacOS/commander
WS=/Users/mah/SimplicityStudio/v6_workspace
```

`commander` on `PATH` is an unrelated Ruby tool (rbenv shim). Always use the full `$COMMANDER` path.

## Flash layout

| Region | Start | Size | Content |
|--------|-------|------|---------|
| Bootloader + Apploader | `0x00000` | `0x12000` (72 kB) | `bt-bootloader-apploader` |
| Application | `0x12000` | `0x6c000` (432 kB) | `bt_soc_thermometer_mock` (about 212 kB used) |

The first 4 bytes of RAM (`0x20000000`) are reserved for the bootloader reset reason.

## What was changed

1. `bt_soc_thermometer_mock.slcp`
   - Component `in_place_ota_dfu` added. It pulls in `apploader`, `apploader_util` and `bootloader_interface`, adds the OTA GATT service, and registers its own event handler.
   - `post_build` entry pointing to `bt_soc_thermometer_mock.slpb`.
2. `bt_soc_thermometer_mock.slpb` (new): copy of SDK `bluetooth_le_app/postbuild_profile/bt_dfu_app_s2.slpb`. Converts the build output to `.s37` and creates the `.gbl`.
3. `app.h`: `APP_VERSION` define (default `0.0.0`, release builds set it from the git tag). `app.c` prints it at boot (`App version: vN`), and `sl_gatt_service_device_information_override.c` reports it as Device Information firmware revision (`2A26`, at most 8 characters).
4. Regenerated files (do not edit by hand): `autogen/linkerfile.ld`, `autogen/gatt_db.[ch]`, `autogen/sl_bluetooth.c`, `autogen/sl_event_handler.c`, `autogen/sl_component_catalog.h`, `cmake_gcc/bt_soc_thermometer_mock.cmake`, new config headers `config/sl_bt_in_place_ota_dfu_config.h`, `config/btl_interface_cfg.h`, `config/app_properties_config.h`, `config/btconf/in_place_ota_dfu.xml`, and copied SDK sources under `simplicity_sdk_2026.6.0/`.
5. Sibling project `../bt-bootloader-apploader/` (outside this git repository).
6. `.gitignore`: `cmake_gcc/build/`, `untracked/`, `artifact/`.

No change was needed in `sl_bt_on_event()`.

## Steps

### 1. Regenerate the application project

Needed after any change to the `.slcp`.

```sh
cd $WS/bt_soc_thermometer_mock
$SLC generate -p bt_soc_thermometer_mock.slcp -s $SDK -o cmake -tlcn gcc --tt
```

`--tt` trusts the SDK for this run only. Without it `slc` refuses with "Untrusted SDK". The permanent alternative is `slc signature trust --sdk $SDK`.

Check with `git diff`: `autogen/linkerfile.ld` must show `FLASH ORIGIN = 0x12000`.

### 2. Generate and build the bootloader (once)

```sh
$SLC generate -p $SDK/bluetooth_le_app/bootloader/bootloader-apploader.slcp -s $SDK \
  -d $WS/bt-bootloader-apploader -np -cp --with "BGM220PC22HNA,brd4314a" \
  -o cmake -tlcn gcc --tt

cd $WS/bt-bootloader-apploader/cmake_gcc
$CMAKE --preset project
$CMAKE --build --preset default_config
```

Output: `$WS/bt-bootloader-apploader/artifact/bt-bootloader-apploader.s37`

### 3. Build the application

```sh
cd $WS/bt_soc_thermometer_mock/cmake_gcc
$CMAKE --preset project
$CMAKE --build --preset default_config
```

Outputs in `cmake_gcc/build/base/`:

- `bt_soc_thermometer_mock.s37` for flashing by cable
- `bt_soc_thermometer_mock.gbl` for OTA

Each build overwrites these. Copies of the two images used for the test are kept in `artifact/`:

- `artifact/bt_soc_thermometer_mock_v1.s37`
- `artifact/bt_soc_thermometer_mock_v2.gbl`

### 4. Flash bootloader and application v1

The mass erase clears the whole chip, including bonding and NVM3 data.

```sh
cd $WS
$COMMANDER device masserase --device BGM220PC22HNA
$COMMANDER flash bt-bootloader-apploader/artifact/bt-bootloader-apploader.s37 \
  bt_soc_thermometer_mock/artifact/bt_soc_thermometer_mock_v1.s37 --device BGM220PC22HNA
```

With more than one debug adapter connected, add `--serialno 440199682` to both commands.

Expected on VCOM: `App version: v1`. The Health Thermometer service should work as before.

### 5. Update to v2 over the air

1. Copy `artifact/bt_soc_thermometer_mock_v2.gbl` to the phone.
2. Simplicity Connect: connect to the device, choose OTA Firmware, select the GBL, reliability mode.
3. The device reboots into the Apploader and advertises as `OTA`, receives the image, then reboots.

Expected on VCOM: `App version: v2`.

### 6. Making a new version later

Released versions come from git tags, see [Releases](#releases). For a local test image:

1. Build (step 3). Local builds report version `0.0.0`; to set another one, configure with `CFLAGS='-DAPP_VERSION=\"1.2.3\"'` in a fresh build directory.
2. Send `cmake_gcc/build/base/bt_soc_thermometer_mock.gbl` with Simplicity Connect (step 5) or the own app.

## Releases

The firmware is published at <https://github.com/mhaberler/bt_soc_thermometer_mock/releases>.

```sh
git tag v1.2.3
git push origin v1.2.3
```

The workflow `.github/workflows/release.yml` then builds the firmware with version `1.2.3` (reported in `2A26`) and creates a release with `bt_soc_thermometer_mock.gbl` (OTA) and `bt_soc_thermometer_mock.s37` (cable flash, needs the bootloader).

- The tag must be `vMAJOR.MINOR.PATCH`, version at most 8 characters.
- Running the workflow by hand ("Run workflow" on GitHub) only builds, as version `0.0.0`, and keeps the images as workflow artifact.
- The runner needs no Simplicity Studio: the SDK sources are in the repository, the compiler comes from Arm, Simplicity Commander (for the GBL) from the Silicon Labs update site.

## Own app

`../thermometer-ota-app/` is a Capacitor app (Vue 3, TypeScript, `@capacitor-community/bluetooth-le`) that scans for the thermometer, shows temperature and firmware version, and runs the update. On connect it looks up the latest release of this repository and offers it if it is newer than the firmware on the device; a GBL can also be loaded from any URL.

Images for testing it: `artifact/bt_soc_thermometer_mock_v3.gbl` and `_v4.gbl`. Both report their version in `2A26`; v2 and older report the Bluetooth stack version there instead.

1. Bring the device to v3 once, with Simplicity Connect (step 5) and `bt_soc_thermometer_mock_v3.gbl`.
2. Serve the images from the Mac:

   ```sh
   cd $WS/bt_soc_thermometer_mock/artifact
   python3 -m http.server 8000
   ```

3. Build and install the app:

   ```sh
   cd $WS/thermometer-ota-app
   bun install
   bun run build-prod
   bun run sync
   bun run run-on-galaxy-s24     # or run-on-i16
   ```

4. In the app: Scan, tap the thermometer. Temperature and `Firmware: v3` appear.
5. Enter `http://<mac-ip>:8000/bt_soc_thermometer_mock_v4.gbl`, tap Download, then Update device. Phone and Mac must be on the same network.
6. After the upload the app reconnects and shows `Firmware: v4 (was v3)`.

What the app does during the update: writes `0x00` to OTA control (`F7BF3564-FB6D-4E53-88A4-5E37E0326063`), waits for the device to reappear as Apploader, connects, writes `0x00` to OTA control, writes the GBL in chunks to OTA data (`984227F3-34FC-4045-A5D0-2C581F81A153`) with response, writes `0x03` to OTA control and disconnects.

A device stuck in Apploader mode shows up in the scan list marked "update mode" and can be updated directly.

The app allows plain HTTP on both platforms (`usesCleartextTraffic` on Android, `NSAllowsArbitraryLoads` on iOS). That is for development only.

Unit tests for the parts that need no phone: `bun run test`.

## Troubleshooting

- **`invalid command` from commander:** the Ruby `commander` was run. Use `$COMMANDER`.
- **Phone does not show the OTA service after flashing:** stale GATT cache. Forget the device or toggle Bluetooth.
- **Update aborted or failed:** the device stays in Apploader mode, advertising as `OTA`. Upload the GBL again, or reflash by cable (step 4).
- **Device does not boot after flashing only the application:** the application is linked at `0x12000` and needs the bootloader. Flash both.

## Not covered

- Signed or encrypted GBL, secure boot.
- Restricting who may start an update. `SL_BT_IN_PLACE_OTA_DFU_BONDING_REQUIRED` is 0 and the default `sl_bt_in_place_ota_dfu_security_status()` is used, so any connected client can trigger OTA.
