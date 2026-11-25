# Shadow Recompiled UWP

An unofficial port of **Shadow Recompiled** for **Xbox Series X|S** in Developer Mode. Version 1.0.9.0 was tested on an Xbox with the game on an external drive, including the **Options** and **Achievements** tabs in the pause menu.

[Download the latest release](https://github.com/mateusmol-bruh/ShadowRecomp-UWP/releases/latest) · [Install on Xbox](#install-on-xbox) · [Use internal storage](#use-internal-storage) · [Prepare the external drive](#prepare-the-external-drive) · [Build from source](#build-from-source)

## Requirements

- An Xbox Series X or Series S with Developer Mode activated. [Activate Xbox Developer Mode](https://learn.microsoft.com/en-us/previous-versions/windows/uwp/xbox-apps/devkit-activation)
- A PC on the same local network as the console to install the app through the Xbox Device Portal.
- Your own copy of **Shadow the Hedgehog (2005) for the original Xbox**. Use the extracted disc with `default.xbe` and its companion files, or a compatible ISO image.
- For external storage, an **NTFS-formatted** USB hard drive, SSD, or flash drive that Xbox apps can write to.

Keep `default.xbe` with the rest of the extracted disc files.

## Download the files

Open **Assets** on the [latest release page](https://github.com/mateusmol-bruh/ShadowRecomp-UWP/releases/latest) to download the files below.

| File | Purpose |
| --- | --- |
| `ShadowRecomp.UWP.x64.appx` | App package to install on the Xbox. |
| `Microsoft.VCLibs.x64.14.00.appx` | Microsoft x64 runtime dependency. Add it when the portal prompts for dependencies. |
| `ShadowRecomp-UWP.cer` | Public signing certificate. Use it if the portal prompts for a certificate. |
| `storage-id.txt` | Sample USB marker with the ID `shadow-usb`. Place it inside `shadow05/` on the drive or create your own. |

The main APPX already includes SDL3 and DirectX Shader Compiler.

## Install on Xbox

1. In Xbox Developer Mode, open **Dev Home**. Under **Remote Access Settings**, enable **Xbox Device Portal**, then open the address shown on the console in your PC's browser.
2. On the portal's **Home** tab, select **Add** under **My games & apps**, then choose `ShadowRecomp.UWP.x64.appx`.
3. Add `Microsoft.VCLibs.x64.14.00.appx` when the portal prompts for dependencies. If it asks for a certificate, select `ShadowRecomp-UWP.cer`. Finish the installation.
4. Continue with the steps for [internal storage](#use-internal-storage) or [an external drive](#prepare-the-external-drive).

[Manually sideload an APPX on Xbox](https://learn.microsoft.com/en-us/windows/uwp/apps-for-xbox/development-options)

## Use internal storage

1. Close **Shadow Recompiled** if it is running and disconnect any USB drive prepared for it. Launch the app once from **Dev Home** to create `LocalState/original/`, `LocalState/save/`, and `LocalState/mods/`. If the setup wizard opens before you have transferred the game files, close the app and continue.
2. On your PC, open **File Explorer → LocalAppData → Shadow Recompiled → LocalState → original** in Xbox Device Portal.
3. Upload a compatible original Xbox `.iso` from your own disc, or transfer your complete extracted disc. Keep `default.xbe` alongside the rest of the disc files and preserve their folder structure. Place them directly in `original/` or together in one immediate subfolder.
4. Launch **Shadow Recompiled** again. It opens a verified extracted disc directly. For an ISO, select it in the setup wizard and follow the prompts. Keep enough space for both the ISO and the extracted files in `LocalState/game/`.

```text
LocalState/
├── storage.txt
├── original/
│   ├── default.xbe
│   └── remaining extracted disc files and folders
├── save/
└── mods/
```

When no valid USB marker is found, the app records `local` in `LocalState/storage.txt` automatically. To bring over existing saves or mods from USB, copy the contents of `shadow05/save/` or `shadow05/mods/` into the corresponding `LocalState` folders while the app is closed.

[Device Portal File Explorer](https://learn.microsoft.com/en-us/windows/uwp/debug-test-perf/device-portal-api-core#app-file-explorer)

## Prepare the external drive

1. On your PC, create a **`shadow05`** folder at the root of the NTFS drive. In the folder's security properties, grant **Modify** permission to **All Application Packages** for the folder, its subfolders, and files.
2. Create `shadow05/storage-id.txt` with a single line such as `shadow-usb`. Use up to 64 ASCII letters, numbers, hyphens, or underscores.
3. Create `shadow05/original/` and copy the entire extracted disc into it. Place `default.xbe` and the other disc files directly inside `original/` or together in one immediate subfolder.
4. Connect the drive to the Xbox. If the console asks how to use the drive, choose **Media**. Launch **Shadow Recompiled** from Dev Home. The app selects a single prepared drive on startup and creates or updates `LocalState/storage.txt` with its ID, such as `usb:shadow-usb`.

```text
X:/
└── shadow05/
    ├── storage-id.txt
    ├── original/
    │   ├── default.xbe
    │   ├── common/
    │   ├── sound/
    │   └── remaining disc files and folders
    ├── save/
    └── mods/
```

The app creates `save/` and `mods/` on the selected storage. You can prepare the folder and set permissions in PowerShell on your PC. Replace `X:` with your drive letter.

```powershell
New-Item -ItemType Directory -Path 'X:\shadow05\original' -Force
icacls 'X:\shadow05' /grant '*S-1-15-2-1:(OI)(CI)M' /T
Set-Content -Encoding ascii -LiteralPath 'X:\shadow05\storage-id.txt' -Value 'shadow-usb'
```

If you're using an **ISO**, copy it to `shadow05/original/` and follow the setup wizard when you launch the app. The wizard extracts the game to `shadow05/game/` and requires additional space. The app can launch directly from an extracted disc after verifying it.

## Saves, mods, and switching storage

| Folder on the selected storage | Contents |
| --- | --- |
| `original/` | Extracted disc or ISO from your copy of the game. |
| `game/` | Game files installed by the setup wizard, if used. |
| `save/` | Saved games and progress. |
| `mods/` | Mods and profiles. Include `.modloader/profiles/` when importing an SA Mod Manager profile. |
| `cache/` | Rendering cache. |

The app, `config.toml`, `storage.txt`, and `startup.log` stay on the console. The app checks for an existing installation in `game/` on the selected storage before trying to launch from the extracted disc.

If several prepared drives are connected, the app uses the ID in `LocalState/storage.txt` when it matches exactly one drive. Give each drive a unique ID. If the app can't resolve the selection, close it and leave only the drive you want to use connected, or set `storage.txt` to its `usb:<storage-id>` value through the Xbox Device Portal before relaunching.

To migrate, close the app and copy any existing `original/`, `game/`, `save/`, and `mods/` folders from `LocalState` into `shadow05/` on the external drive. Launch the game with the drive connected and check that your save appears. To open the setup wizard manually, create an empty `install.txt` in `LocalState` and relaunch the app.

## Updates and troubleshooting

To update, close the game and install the new APPX over the existing version through the Xbox Device Portal. Keep using the same external drive and `shadow05` folder. The update uses the same package identity and signing certificate as the previous test build.

- **The setup wizard opens.** Check that `default.xbe` and the rest of the extracted disc files are in `LocalState/original/` for internal storage or `shadow05/original/` for USB storage. The log shows `Media: verified extracted disc. Launching in place.` when the disc is ready to launch directly.
- **The app doesn't select the external drive.** Check the folder permissions, the contents of `shadow05/storage-id.txt`, and the `Storage` entries in `startup.log`.
- **Black screen or return to the Xbox dashboard.** Download `startup.log` from **File Explorer → LocalAppData → Shadow Recompiled → LocalState** before launching the game again. When reporting an issue, include the app version, your Xbox model, what you were doing when it happened, and the log. Remove personal information before sharing it.

## Build from source

The [UWP build guide (Portuguese)](docs/BUILDING-UWP.md#compilar-e-empacotar) lists the required Windows SDK and LLVM tools and shows how to build and sign the package. Building requires `default.xbe` from your own copy of the game.

## Credits and licenses

The port's code is licensed under the [GNU General Public License v3](LICENSE). It is based on [FoxWorkshop's ShadowRecomp](https://github.com/FoxWorkshop/ShadowRecomp) and uses [UnleashedRecomp-uwp](https://github.com/YTReviveMe/UnleashedRecomp-uwp) as a reference for Xbox integration. See the [upstream README](https://github.com/FoxWorkshop/ShadowRecomp/blob/d2be1d4105a83853a3bf1954d3eac2047912d87e/README.md) for Windows and Linux information and the upstream project credits.

See the [third-party notices (Portuguese)](docs/THIRD-PARTY-UWP.md) for package dependencies. Shadow the Hedgehog belongs to SEGA.
