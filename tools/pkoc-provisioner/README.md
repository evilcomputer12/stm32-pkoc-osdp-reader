# PKOC provisioner (Android NFC)

An Android app that installs the open-source PKOC applet on a blank JavaCard over the phone's NFC, so the card can be read by a PKOC reader such as the one in this repository. No PC/SC reader is needed.

The applet generates its P-256 key pair on the card when it is installed; the private key never leaves the card. Enrolling the card's credential with the access controller is still a separate step.

Tested with an NXP JCOP4 J3R180 ("unfused" JavaCard, GlobalPlatform 2.3, SCP02) and a Samsung Galaxy S23+. The card needs JavaCard 3.0.4 or later with ECC P-256 and ISO 14443-A.

This tool is derived from [Cred-Bench](https://github.com/Z-bit-Systems-LLC/Cred-Bench) by Z-bit Systems, LLC and stays under the Apache License 2.0 (see [LICENSE](LICENSE) and [Changes from Cred-Bench](#changes-from-cred-bench)).

## Using the app

1. Open **Cred-Bench PKOC** and choose the GlobalPlatform key:
   - **Default GP key** (`404142…4F`) — only if the supplier says the card still uses it.
   - **Custom key** — one 16-byte key for ENC, MAC and DEK.
   - **Separate ENC / MAC / DEK (KEK) keys** — when the supplier gives three SCP02 keys (e.g. `CDKenc`, `CDKmac`, `CDKkek`).
2. Tap **Analyze card** or **Install PKOC applet**, then hold the card flat against the back of the phone and keep it still until the result appears. A card already lying on the phone is used directly; there is no need to lift and re-tap it.
3. **Analyze** authenticates to the card manager and lists the installed applets and packages, including whether PKOC is present.
4. **Install** loads the embedded `pkoc.cap` and installs applet `A000000898000001` (package `A000000898000002`). If PKOC is already installed, nothing is changed unless **Replace existing PKOC applet** is ticked and confirmed. Replacing destroys the card's PKOC key pair, so its credential number changes.

The log on screen (and `adb logcat -s CredBench`) shows each step with the card's UID.

### About GlobalPlatform keys

- The key is the card manager (ISD) key from the card supplier. It is **not** the PKOC credential key.
- Keys are typed in each time and are not saved.
- A wrong ENC key is detected from the card cryptogram before `EXTERNAL AUTHENTICATE` is sent, so it does not use up an authentication attempt. A wrong MAC key can only be rejected by the card, and cards lock the card manager after a limited number of failed attempts. Don't guess keys.
- If an install was interrupted after the package was loaded, the next install removes the orphaned package automatically. An installed applet is never deleted without the replace option.

## Building

Requires the .NET 10 SDK with the Android workload, an Android SDK and JDK 17. Run from this folder:

```sh
dotnet workload install android

# One time: download the Android SDK and JDK into the given folders
dotnet build src/UI/Android/Android.csproj -f net10.0-android -t:InstallAndroidDependencies \
  -p:AndroidSdkDirectory=<sdk> -p:JavaSdkDirectory=<jdk> -p:AcceptAndroidSDKLicenses=True

dotnet publish src/UI/Android/Android.csproj -f net10.0-android -c Release \
  -p:AndroidSdkDirectory=<sdk> -p:JavaSdkDirectory=<jdk>
```

Install the signed APK with USB debugging enabled on the phone:

```sh
adb install -r src/UI/Android/bin/Release/net10.0-android/publish/com.zbitsystems.credbench-Signed.apk
```

The Release APK is signed with the local Android debug key, which is fine for sideloading but not for a store release. No APK is committed.

On Windows, keep the checkout path short (for example `C:\src\stm32-pkoc-osdp-reader`). In deeply nested folders the Release AOT step fails with ``Assertion ... condition `corlib' not met``.

### The PKOC applet

`src/Applet/build/pkoc.cap` is the prebuilt applet, embedded into the app at build time. It is built from `src/Applet/src/com/zbitsystems/pkoc/PkocApplet.java`; see [src/Applet/README.md](src/Applet/README.md) to rebuild and test it with Ant and the JavaCard SDK. The CAP file contains no keys.

## Layout

| Path | Purpose |
|---|---|
| `src/UI/Android/` | Android app: NFC reader mode, `IsoDepConnection` (ISO-DEP as an `ICardConnection`), UI |
| `src/Core/Services/GlobalPlatform/` | SCP02 secure channel, CAP loading, `GpKeySet` |
| `src/Core/Services/Pkoc/PkocCardProgrammer.cs` | Install / replace / card-content flow |
| `src/Core/Services/CardDetectors/PKOCDetector.cs` | PKOC `SELECT` check used after installing |
| `src/Applet/` | PKOC JavaCard applet source, tests and prebuilt CAP |

`src/Core` is Cred-Bench's shared library and also contains the Windows app's detectors, view models and PC/SC code; the Windows UI itself is not included here.

## Changes from Cred-Bench

This folder copies `src/Core` and `src/Applet` from Cred-Bench and adds the Android app. The applet is unchanged. Changed or added files:

- `src/UI/Android/` — new Android NFC app.
- `src/Core/Services/GlobalPlatform/GpKeySet.cs` — new: separate SCP02 ENC / MAC / DEK keys.
- `src/Core/Services/GlobalPlatform/GlobalPlatformService.cs`, `AppletLoader.cs` — report a wrong key before `EXTERNAL AUTHENTICATE` is sent and an empty card response (no status word) as clear errors, without echoing keys.
- `src/Core/Services/Pkoc/PkocCardProgrammer.cs` — 16-byte key validation, separate keys, refuses to overwrite an installed PKOC applet unless replacement is requested, removes an orphaned package left by an interrupted install, card-content query.
- `src/Core/Services/CardDetectors/PKOCDetector.cs`, `src/Core/Services/CardDetectionService.cs`, `src/Core/Models/DetectionResult.cs` — record why PKOC was not detected (for example `SW=6A82`, applet not installed).
- `src/Core/ViewModels/PkocViewModel.cs`, `src/Core/ViewModels/MainViewModel.cs`, `src/Core/Resources/Resources.resx`, `src/Core/Services/GlobalPlatform/GlobalPlatformProClient.cs` (new) — Windows app: installer for blank ISO 14443 cards, replacement confirmation, separate keys and an optional GlobalPlatformPro engine.
- `src/Core/Core.csproj` — adds `Microsoft.Build.Tasks.Git`.

## License

Apache License 2.0 — see [LICENSE](LICENSE). Copyright Z-bit Systems, LLC; modifications by evilcomputer12, 2026. The rest of this repository is GPL-3.0-or-later; see the repository [NOTICE](../../NOTICE).
