# WrestleFest Android — Repository & Code Audit

**Repository:** `jfrnhugwrh/WrestleFest-Android`
**Commit audited:** `83b4099f8f3011a38d7d1e6a2199b89d62436475` (`initial_commit`, 2026-10-20)
**Branch audited:** `main` (working branch `arena/7659f254-wrestlefest-android` is identical)
**Audit date:** 2026-10-20
**Scope:** every tracked file — Android/Java, C++/NDK, Gradle, CMake, Python tooling, CI, and the complete 373 MiB asset payload.
**Context:** personal, private project (per the author). Assets are used locally and will be removed before any sharing, so licensing/distribution is out of scope here. Severities are judged for a private, sideloaded build.

---

## 0. TL;DR

This repository is a **porting scaffold plus an extracted asset dump**, not a playable game.

- The **asset and packaging pipeline is genuinely complete and works** — I compiled and ran the OBB reader against a generated archive, ran the OBB compiler, ran the GLSL upgrader, and compiled and ran the viewport unit tests. All passed.
- The **runtime app is a stub**: it opens a GLES 3.2 surface, clears the screen to a dark navy colour, and plays the `Attack01.wav` sound effect whenever you tap. That is the entire observable behaviour.
- **None of the game exists**: no sprite/SST renderer, no SML data-table parser, no bitmap-font renderer, no menus, no wrestler select, no match logic, no AI, no save/profile system, no music playback, no localization, no controller support.
- The main technical risks are a **release build that can break its own JNI bridge under R8** (debug is unaffected), a **hard GLES 3.2 / arm64-only requirement with no fallback**, and a **351 MiB APK that makes every build, install and update slow** — workable privately, but the OBB path that already exists should be wired in.
- Rough completeness of a playable port: **~10–15%**. What exists is the foundation (platform boots, assets are readable, screen geometry is solved). What is missing is the game.

**Headline numbers**

| Metric | Value |
|---|---|
| Tracked files | 1,064 (1,033 are assets) |
| Hand-written code | ≈ 3,065 lines (C++ 1,760 · Python 847 · Java 283 · Gradle/XML 127 · CMake 48) |
| Source assets | 373.1 MiB · 5 top-level categories |
| Estimated APK payload | **351.2 MiB** (measured by re-zipping the assets) |
| Git object store | 350 MiB (single commit, no history to speak of) |
| Native test coverage | 5 viewport scenarios — **passing** |
| Gameplay implemented | **none** |

---

## 1. How this audit was performed

Static review of every source file, plus executable verification where the environment allowed it.

**Actually executed:**

| Check | Result |
|---|---|
| Compile & run `viewport_manager_test.cpp` on host (`g++ -std=c++17 -Wall -Wextra -pedantic`) | Built clean, all 5 test functions passed |
| `compile_obb.py` on a 3-file subset | Succeeded, produced valid ZIP/OBB, verification phase passed |
| Custom C++ harness linking the real `obb_archive.cpp` against that OBB | 3/3 entries read byte-identical; deflate, CRC32, case-insensitive lookup and missing-key behaviour all correct |
| `upgrade_glsl.py --check` on the repo | Reports all 8 shaders need upgrading |
| `upgrade_glsl.py` on a copy of `graphics/Shaders` | Correct GLSL ES 3.00 output |
| WAV format survey of all 135 files | 135/135 within the player's supported subset |
| Asset inventory, hashing, duplicate analysis, APK payload re-zip | Completed (numbers in §2) |
| `git ls-tree`, workflow action tag existence, dependency version existence | Checked |

**Not executed (impossible in this sandbox):** Gradle/AGP/NDK build of the APK (no JDK or Android SDK installed), on-device runtime, Play Console upload, and instrumentation/UI tests (none exist). Findings about release-build behaviour are derived from configuration and code, and are labelled as such.

---

## 2. Repository at a glance

### 2.1 Layout

```
.
├── .github/workflows/build-debug-apk.yml   # manual debug-APK build
├── .gitignore
├── CODE_AUDIT.md                           # ← this report (added by the audit)
├── android_project/                        # Gradle root (not at repo root)
│   ├── build.gradle.kts                    # AGP 8.7.3
│   ├── settings.gradle.kts
│   ├── gradle.properties
│   ├── gradle/wrapper/…                    # Gradle 8.9
│   ├── gradlew / gradlew.bat
│   └── app/
│       ├── build.gradle.kts                # SDK 35, NDK 26.1, arm64-v8a
│       └── src/main/
│           ├── AndroidManifest.xml
│           ├── asset_extraction_report.json
│           ├── java/com/mfm/wrestlefest/
│           │   ├── MainActivity.java       # GameActivity + OBB discovery
│           │   └── CrashLogHandler.java    # crash capture
│           ├── cpp/
│           │   ├── CMakeLists.txt
│           │   ├── main.cpp                # EGL/GLES loop, input, audio
│           │   ├── asset_loader.{h,cpp}    # OBB → APK fallback, JNI
│           │   ├── obb_archive.{h,cpp}     # ZIP reader
│           │   ├── audio_player.{h,cpp}    # OpenSL ES PCM WAV
│           │   ├── viewport_manager.{h,cpp}# letterboxing math
│           │   └── viewport_manager_test.cpp
│           ├── res/values/themes.xml       # AppCompat theme (only resource)
│           └── assets/                     # 373 MiB, see §2.2
├── compile_obb.py                          # asset dir → .obb
├── extract_assets.py                       # IPA → asset tree + JSON report
├── upgrade_glsl.py                         # GLSL ES 1.0 → 3.00
└── scripts/convert-audio.{bat,ps1}         # ffmpeg audio conversion (Windows)
```

### 2.2 Size and composition

| Category | Files | Size | What it contains |
|---|---:|---:|---|
| `assets/asset_bundles` | 53 | 214.0 MiB | 38 nested `wrestler.zip` (SST sprite frames) + 15 crowd bundles |
| `assets/graphics` | 392 | 92.0 MiB | 370 PNG (arenas, items, EFX, avatars), 10 SML, 8 shaders (3 `.vsh` + 5 `.fsh`), 1 `.tyFX`, 2 `.tyF` fonts, 22 MiB ARIALUNI.TTF |
| `assets/audio` | 146 | 37.9 MiB | 135 WAV SFX + 11 AAC music tracks |
| `assets/ui` | 171 | 24.1 MiB | 171 PNG UI atlases (menus, HUD, energy bars, virtual pad) |
| `assets/data` | 271 | 5.1 MiB | 263 SML game-data tables, config, credits, localization |
| **Total** | **1,033** | **373.1 MiB** | |

Extension breakdown: `.png` 541 · `.sml` 273 · `.wav` 135 · `.zip` 53 · `.aac` 11 · `.fsh` 5 · `.vsh` 3 · `.txt` 3 · `.tyF`/`.tyFX` 3 · `.plist`, `.ini`, `.crc`, `.ttf`, `PkgInfo`/`FAULTYCLONES` 1 each.

**Asset redundancy is low** — only 36 duplicate groups totalling 2.08 MiB. The 214 MiB of sprite bundles and the 92 MiB of graphics are *not* duplicates of each other: the bundles hold proprietary `.sst` animation frames, while `graphics/Sprites/Character/<name>/` contains only `avatar.png` and `taunt.png` per wrestler.

For a private project, keeping the assets in the tree is a reasonable convenience; the costs are the 350 MiB history, slower clones, and the 351 MiB APK. Before sharing, they need to be removed from both the working tree and history.

### 2.3 Build configuration

| Item | Value | Assessment |
|---|---|---|
| AGP / Gradle / JDK | 8.7.3 / 8.9 / 17 | Compatible, current-ish for the pinned AGP |
| `compileSdk` / `targetSdk` / `minSdk` | 35 / 35 / 23 | `minSdk 23` is inconsistent with the GLES 3.2 requirement |
| NDK / CMake | 26.1.10909125 / 3.22.1 | Matches CI-installed packages |
| ABI | `arm64-v8a` only | No x86_64 emulator, no armeabi-v7a devices |
| Language level | C++17, Java 17 | Fine |
| GameActivity | `androidx.games:games-activity:3.0.5` | Exists (Aug 2024); 4.x is now available |
| `prefab` | enabled | Required for GameActivity's static lib — correct |
| Release | `minifyEnabled` + `shrinkResources`, no custom keep rules, **no signing config** | Risky / not shippable as-is |
| Assets | all under `src/main/assets`, `noCompress += "zip"` | Every asset ships inside the APK |

### 2.4 Git & CI

- **One commit** (`initial_commit`) on both `main` and the working branch. There is no development history to review.
- `.git` is **350 MiB** because the assets were committed once; removing them later will not shrink existing clones without a history rewrite.
- `gradlew` is committed with mode **100644 (not executable)**. The CI workflow works around it with `chmod +x`, and its comment claims the opposite ("The wrapper is committed with the executable bit") — an inconsistency worth fixing.
- `gradle-wrapper.properties` has **no `distributionSha256Sum`**, so the wrapper download is not verified.
- CI (`.github/workflows/build-debug-apk.yml`) is **manual dispatch only**, builds `:app:assembleDebug`, uploads the APK. Action tags `actions/checkout@v7`, `actions/setup-java@v6`, `actions/upload-artifact@v7`, `gradle/actions@v5` all exist. It does **not** run the native tests, lint, a release build, or sign anything.

---

## 3. Runtime architecture

### 3.1 Boot sequence

```
Launcher
  └─ MainActivity.onCreate()                         MainActivity.java:24
       ├─ CrashLogHandler.collectPreviousCrashReports()   (API 30+)
       ├─ CrashLogHandler.install()
       ├─ super.onCreate()  → GameActivity
       │     ├─ static { System.loadLibrary("wrestlefest") }
       │     └─ GameActivity.initializeNativeCode() → libwrestlefest.so
       │           └─ android_main()                main.cpp:180
       │                ├─ AssetLoader::initialize() → OBB via JNI / filesystem
       │                ├─ AudioPlayer::prepareWav("audio/sfx/Attack01.wav")
       │                └─ poll loop
       │                     ├─ APP_CMD_INIT_WINDOW → Renderer::initialize()
       │                     ├─ processInput() → any ACTION_DOWN → audio.play()
       │                     └─ render() → eglQuerySurface → glViewport → glClear → swap
       └─ if findObbFile().isEmpty() → "Game data not found" dialog
```

The Java layer does almost nothing except locate an OBB and install crash handling; all real work is native.

### 3.2 Asset resolution

There are **three independent OBB discovery implementations**:

1. **Java** (`MainActivity.findObbFile`, `MainActivity.java:54-120`): `getObbDirs()`, `Environment.getExternalStorageDirectory()/Android/obb/<pkg>`, `/sdcard/...`, `/storage/emulated/0/...`, app-specific `files/obb/<pkg>`. Returns the first readable `main.1.<pkg>.obb` or *any* `*.obb`.
2. **Native via JNI** (`asset_loader.cpp:51-92`): attaches the game thread, reflectively calls `findObbFile()` on the activity, returns the string.
3. **Native filesystem scan** (`asset_loader.cpp:94-145`): the same hard-coded `/sdcard` and `/data/data/<pkg>` paths, plus a `readdir()` scan.

Then:

```
AssetLoader::readAsset(path)          asset_loader.cpp:217
  ├─ OBB archive lookup (if open)     obb_archive.cpp:310
  └─ APK assets via AAssetManager     (fallback, 64 MiB cap)
```

OBB entries win over APK assets. If no OBB is present the app still runs from APK assets — which contradicts the startup dialog, discussed in §6.

### 3.3 Native module map

| Module | Responsibility | Lines |
|---|---|---:|
| `main.cpp` | EGL/GLES 3.2 context, loop, input, lifecycle commands | 207 |
| `asset_loader.cpp` | OBB discovery + APK fallback + JNI bridge | 278 |
| `obb_archive.cpp` | ZIP central-directory parser, stored/deflate reads, CRC32 | 439 |
| `audio_player.cpp` | WAV parser + OpenSL ES buffer-queue playback | 299 |
| `viewport_manager.cpp` | 480×320 logical canvas → letterboxed surface, GL/touch mapping | 199 |
| `viewport_manager_test.cpp` | Host-side assertions for the viewport math | 129 |

---

## 4. Feature audit

Legend: **Complete** = implemented and verified · **Partial** = works but limited · **Stub** = present in name only · **Missing** = absent.

| # | Feature | Status | Where |
|---:|---|---|---|
| 1 | App shell / activity / manifest | **Complete (minimal)** | `MainActivity.java`, `AndroidManifest.xml` |
| 2 | GameActivity/JNI bootstrap | **Complete** | `MainActivity.java:19-21`, `CMakeLists.txt:22-25` |
| 3 | EGL + GLES 3.2 context | **Complete** | `main.cpp:19-68` |
| 4 | Frame loop & presentation | **Stub** | `main.cpp:190-203` |
| 5 | Sprite / tile / text rendering | **Missing** | — |
| 6 | Viewport / letterbox / coordinate mapping | **Complete (tested)** | `viewport_manager.cpp` |
| 7 | Touch / gesture input | **Stub** | `main.cpp:133-150` |
| 8 | Gamepad / keyboard input | **Missing** | — |
| 9 | PCM WAV audio playback | **Partial** | `audio_player.cpp` |
| 10 | Music / AAC / streaming | **Missing** | — |
| 11 | OBB discovery (3 implementations) | **Complete** | Java + `asset_loader.cpp` |
| 12 | OBB/ZIP reader | **Complete (tested)** | `obb_archive.cpp` |
| 13 | Asset load + APK fallback | **Complete** | `asset_loader.cpp:199-278` |
| 14 | SML game-data parser | **Missing** | 263 tables unused |
| 15 | SST sprite decoder | **Missing** | 23,696 frames unused |
| 16 | `.tyF` bitmap-font renderer | **Missing** | 2 fonts unused |
| 17 | Localization loader | **Missing** | `LanguageTable*.sml`, `text.txt` unused |
| 18 | Menus / HUD / UI screens | **Missing** | 171 UI PNGs unused |
| 19 | Match simulation / AI / controls | **Missing** | — |
| 20 | Save / profile | **Missing** | — |
| 21 | Crash reporting | **Partial** | `CrashLogHandler.java` |
| 22 | IPA asset extractor | **Complete (tested)** | `extract_assets.py` |
| 23 | OBB compiler | **Complete (tested)** | `compile_obb.py` |
| 24 | GLSL ES 3.00 upgrader | **Complete (tested, unapplied)** | `upgrade_glsl.py` |
| 25 | Audio format converter | **Complete (Windows only)** | `scripts/convert-audio.ps1` |
| 26 | CI build | **Partial** | `.github/workflows/build-debug-apk.yml` |
| 27 | Automated tests | **Partial** | native viewport only, not in CI |
| 28 | Documentation | **Missing** | no README or architecture notes (LICENSE only needed if shared) |

---

### 4.1 Android application shell — Complete (minimal)

`MainActivity` (`MainActivity.java:15-121`) extends `com.google.androidgamesdk.GameActivity`, loads `libwrestlefest.so` in a static block, installs crash handling, and shows an `AlertDialog` if no OBB is found.

Manifest (`AndroidManifest.xml`):

- Landscape-locked activity with `configChanges` covering orientation, screen size, density, locale, etc. (`:19-21`).
- `android:glEsVersion="0x00030002"` declared **required** (`:7-9`) — see §6 H2.
- `READ_EXTERNAL_STORAGE` with `maxSdkVersion="32"` (`:3-5`), but **no runtime permission request** anywhere in Java.
- `android:label="WrestleFest"` hardcoded, **no `android:icon`**, no adaptive icon, no `strings.xml` (`:14`).
- `android:hasCode="true"` is redundant.
- The theme is `Theme.WrestleFest` extending `Theme.AppCompat.Light.NoActionBar` (`themes.xml`), which is genuinely required because GameActivity extends `AppCompatActivity`. This is correctly documented in a comment.

### 4.2 GameActivity / JNI bootstrap — Complete

The CMake target is named `wrestlefest`, matching `android.app.lib_name` in the manifest, so `System.loadLibrary("wrestlefest")` resolves `libwrestlefest.so`. The linker flags

```cmake
-Wl,-u,GameActivity_onCreate
-Wl,-u,Java_com_google_androidgamesdk_GameActivity_initializeNativeCode
```

(`CMakeLists.txt:22-25`) are the correct workaround for the well-known `--gc-sections` / `UnsatisfiedLinkError` issue, and the accompanying comment points at the right upstream bug. This is one of the better-engineered parts of the repo.

### 4.3 EGL / GLES context — Complete, but strict

`Renderer::initialize` (`main.cpp:19-68`) requests an **OpenGL ES 3.2** context via `EGL_CONTEXT_MAJOR_VERSION_KHR`/`MINOR`. If the driver cannot supply 3.2, `eglCreateContext` fails, `release()` runs, and the app shows a **black screen with only a logcat error** — no fallback to 3.0/2.0 and no user-visible message.

Configuration is 8/8/8/8 colour + 16-bit depth, ES3 renderable, no stencil, no MSAA. On success it logs the GL version string.

### 4.4 Frame loop — Stub

`android_main` (`main.cpp:190-203`) polls with a 16 ms timeout and renders every iteration:

```cpp
const int timeout = app->window == nullptr ? -1 : 16;
ALooper_pollOnce(timeout, …);
processInput(app, &audio);
if (app->window != nullptr) renderer.render(app->window);
```

`render()` (`main.cpp:93-102`) only:

1. re-queries the surface size and sets `glViewport` to the letterboxed 480×320 content rect,
2. `glClearColor(0.035, 0.045, 0.075, 1.0)` + `glClear`,
3. `eglSwapBuffers`.

There is no shader program, no VBO, no texture, no draw call, no frame clock and no `eglSwapInterval` call. **No asset is ever rendered.**

### 4.5 Viewport manager — Complete and tested

`ViewportManager` (`viewport_manager.cpp`) is the strongest piece of code in the repository:

- Fits a **480×320** logical canvas into the surface with aspect preserved (`scale_ = min(w/480, h/320)`) and centres it (`:46-68`).
- Tracks the content rectangle for letterbox/pillarbox bars.
- `mapTouchToLogical` (`:95-106`) maps top-left-origin touch coordinates into logical space and rejects touches in the bars.
- `mapRectToPixels` / `mapHitboxToPixels` (`:108-129`) map logical rectangles to pixels.
- `mapGlRect` (`:131-161`) converts to **bottom-left-origin** GL viewport/scissor coordinates, with rounding and overflow checks.
- `forwardGlViewport` / `forwardGlScissor` (`:163-197`) are drop-in wrappers that map or pass through.

The 129-line test suite covers a 2400×1080 phone, a 960×640 canvas with dynamic resize, touch/hitbox edges, pillarboxing, GL hook forwarding, and invalid/overflow inputs. I compiled and ran it on the host: **all assertions pass**, no warnings under `-Wall -Wextra -pedantic`.

**Caveat:** outside the tests, only `setSurfaceSize` and `mapGlRect` are used (`main.cpp:83-89`). The touch, hitbox and scissor helpers are dead code until input and rendering are implemented.

### 4.6 Input — Stub

`processInput` (`main.cpp:133-150`):

```cpp
touch_down = touch_down || action == AMOTION_EVENT_ACTION_DOWN;
android_app_clear_motion_events(input);
android_app_clear_key_events(input);
if (touch_down) audio->play();
```

- Any finger down, anywhere on the surface, retriggers the same sound effect.
- Touch **coordinates are discarded**; `ViewportManager::mapTouchToLogical` is never called.
- Multi-touch, move/up events, pointer IDs, pressure, gestures — ignored.
- Key events are cleared without ever being read (`keyEventsCount` is never inspected): **no controller or keyboard support**.
- The exported virtual-pad artwork (`graphics/Sprites/virtual_pad/*.png`, 8 files) is unused.

### 4.7 Audio — Partial

`AudioPlayer` (`audio_player.cpp`) is a real, careful single-voice WAV player:

- `parsePcmWav` (`:34-91`) walks RIFF chunks, handles arbitrary chunk order/padding, requires PCM (`audio_format == 1`), 1–2 channels, 8/16/24-bit, 8–192 kHz, ≤ 32 MiB, and validates the block alignment.
- Converts 8-bit and 24-bit samples to signed 16-bit (`:196-221`).
- Creates an OpenSL ES engine, output mix, buffer-queue player at the source sample rate (`:132-253`).
- `play()` (`:256-270`) stops, clears the queue, enqueues the **entire** PCM buffer and starts playback.

Verified on the asset set: all **135 WAV files** fall inside the supported subset (52× mono 8-bit/48 kHz, 45× mono 16-bit/48 kHz, 23× mono 8-bit/44.1 kHz, 8× 24-bit, 4× 16-bit/44.1 kHz, 2× 8-bit/32 kHz, 1× stereo 16-bit).

Limitations:

- **WAV only.** The 11 music tracks are AAC/ADTS (`ff f1 …`) and cannot be played. The bundled `scripts/convert-audio.ps1` can transcode them to WAV, but nothing loads music anyway.
- One global player, one sound: `prepareWav` is called exactly once for `audio/sfx/Attack01.wav` (`main.cpp:186`). No mixing, volume, panning, looping, streaming, or per-event voices.
- The whole PCM buffer is enqueued up front — fine for a 10 KB SFX, unsuitable for minutes-long music.
- OpenSL ES is deprecated; AAudio/Oboe (API 26+) would be the modern choice.
- Audio is never paused on `APP_CMD_PAUSE`/`APP_CMD_LOST_FOCUS`; the engine keeps running in the background.

### 4.8 OBB / ZIP archive reader — Complete, verified, with caveats

`ObbArchive` (`obb_archive.cpp`) implements a ZIP reader from scratch:

- Locates End-of-Central-Directory by scanning the last 65,558 bytes (`:206-238`).
- Reads the central directory (`:240-301`), then each local header to compute the data offset (`:331-355`).
- Supports **stored (0)** and **deflate (8)**, raw inflate via `inflateInit2(-MAX_WBITS)` (`:377-415`).
- Verifies **CRC32** when the stored CRC is non-zero (`:426-436`).
- `findKey` provides exact, suffix and case-insensitive matching (`:135-174`), with a linear scan fallback.

I built a 3-entry OBB with `compile_obb.py` and read it with the real parser: `Attack01.wav` (deflated), `sub/arena.sml` and `random.bin` (200 KB) all came back **byte-identical**, CRC validation passed, `ATTACK01.WAV` resolved case-insensitively, and a missing key correctly returned false.

Caveats (§6 M1): no Zip64, no cap on the central-directory allocation, empty archives report `isOpen() == false`, the `fd_` member is abused as a boolean flag (`:81-90`, `:304`), the file is reopened for every read, and fallback lookups are O(n) per miss.

### 4.9 Asset loader — Complete

`AssetLoader` (`asset_loader.cpp`) tries the OBB first and falls back to APK assets:

- OBB lookup order: JNI → filesystem probe → directory scan (`:147-181`).
- `readAsset` (`:217-278`): OBB read, else `AAssetManager_open` with `AASSET_MODE_BUFFER`, chunked reads, **64 MiB hard cap** (`:19`, `:242`).
- `hasAsset` checks OBB then APK (`:199-215`).
- Instrumentation logs every successful and failed load.

The cap is a silent failure mode: an asset larger than 64 MiB simply reports "not found". None of the current assets exceed it (the largest is the 22 MiB TTF), but the 6.9 MiB sprite bundles are close enough that future content could trip it.

### 4.10 Crash reporting — Partial

`CrashLogHandler` (`CrashLogHandler.java`) does two things:

1. **Java uncaught exceptions** — installs a `Thread.UncaughtExceptionHandler` (`:39-48`) and appends a detailed report (timestamp, PID, thread, package, Android version, device, fingerprint, stack trace) to `files/crash-logcat.txt` with an `fsync` (`:136-161`), then delegates to the previous handler.
2. **Historical process exits (API 30+)** — reads up to 10 `ApplicationExitInfo` records, filters `REASON_CRASH`, `REASON_CRASH_NATIVE` and `REASON_ANR`, and copies the system trace (`:50-84`, `:108-134`). A SharedPreferences timestamp prevents duplicate reporting.

Gaps: native crashes on API < 30 are invisible (the Java handler never runs for SIGSEGV); the file grows without bound or rotation; there is **no in-app way to view, export or upload** the log (you need `adb`); and `getHistoricalProcessExitReasons` is called unguarded, so an OEM-thrown `RuntimeException` would crash the app during startup.

### 4.11 IPA asset extractor — Complete, verified

`extract_assets.py` (363 lines) is a proper, defensive tool:

- Finds the single `.app` bundle in an IPA via `Info.plist` (`:61-81`), optionally selected with `--app`.
- **Path-safety hardening** (`safe_relative_path`, `:41-58`): rejects absolute paths, `..`, Windows-reserved names, trailing dots/spaces, illegal characters.
- Refuses unsafe output directories and symlinks; requires `--clean` to overwrite a non-empty directory (`:251-272`).
- Streams files, classifies them by signature and path into `audio` / `graphics` / `ui` / `data` (`:96-135`), and preserves nested ZIP bundles under `asset_bundles/` **without unpacking them** (`:218-248`).
- Emits `asset_extraction_report.json` next to the assets (`:322-344`).

Cross-check: the committed report claims 980 extracted files + 53 bundles = 1,033, which **exactly matches the tree**. Categories in the report (`audio` 146, `data` 271, `graphics` 392, `ui` 171) also match my inventory.

Minor issues: `SUPPORT_EXTENSIONS` is defined but unused (`:37`); the report embeds a **Windows absolute source path** (`C:\Users\MaxDaBoy\Downloads\…\my dumped copy.ipa`) which leaks workstation details and makes the file non-reproducible; there is no zip-bomb/size guard.

### 4.12 OBB compiler — Complete, verified

`compile_obb.py` (198 lines):

- Walks the asset directory, sorts entries, reports counts and uncompressed size (`:40-54`).
- Writes to a temp file, then **verifies** the archive: entry count, per-entry uncompressed size, and decompression of 5 sampled files (`:75-110`).
- Atomically renames into place and optionally deletes the uncompressed assets (`:111-130`).
- Default output `obb/com.mfm.wrestlefest/main.1.com.mfm.wrestlefest.obb` and a convenience copy in the repo root (`:171-194`).

I ran it on a subset: **succeeded**, and the resulting OBB was readable by the native parser. Caveats in §6 M5.

### 4.13 GLSL ES 3.00 upgrader — Complete, verified, not applied

`upgrade_glsl.py` (286 lines) converts legacy shaders:

- `attribute` → `layout(location = N) in` with stable, name-based location assignment (`tyPosition` 0, `tyColor` 1, `tyTexcoord` 2).
- `varying` → `out` (vertex) / `in` (fragment), with matching locations.
- `gl_FragColor` → declared `layout(location = 0) out vec4 fragColor`.
- `texture2D` → `texture`; prepends/rewrites `#version 300 es`.
- Can also rewrite shader source embedded in C/C++/Java/Kotlin/JS/TS/Python string literals (`:185-221`), handling escaping.
- `--check` reports without writing and exits non-zero if changes are needed.

Verified by copying `graphics/Shaders` and running it: all 8 files converted correctly (e.g. `Sprite.vsh` now has `#version 300 es`, `layout(location = 0) in vec4 tyPosition`, matching `out` varyings).

**However, the repository still ships the original GLSL ES 1.00 shaders** (`--check` flags all 8), and no code loads shaders anyway. The tool is ready for a renderer that does not yet exist. It also rewrites files in place with no backup.

### 4.14 Audio converter — Complete (Windows only)

`scripts/convert-audio.ps1` uses ffmpeg to transcode all audio under `assets/audio` into `converted-audio/`:

- Validates input/output roots to prevent overwriting source (`:15-21`).
- Requires ffmpeg on `PATH`.
- Outputs 44.1 kHz stereo `pcm_s16le` WAV or Ogg Vorbis quality 4.
- `-Force` to overwrite, otherwise refuses.

Two issues: Ogg output is not playable by `AudioPlayer` (WAV only), and the script is Windows/PowerShell-only in an otherwise cross-platform Python toolchain.

### 4.15 CI — Partial

`.github/workflows/build-debug-apk.yml`:

- Manual `workflow_dispatch` only; `permissions: contents: read`; 60-minute timeout.
- JDK 17 (Temurin), Gradle cache via `gradle/actions/setup-gradle@v5`, wrapper validation.
- Installs `platforms;android-35`, `build-tools;35.0.0`, `ndk;26.1.10909125`, `cmake;3.22.1`; accepts licences defensively.
- Builds `:app:assembleDebug` and uploads the APK artifact.

Missing: unit tests, lint, release build, signing, dependency/security scanning, APK size report, and SHA-pinned actions. It also builds a ~351 MiB APK with no size gate (irrelevant privately, relevant if the project is ever published).

### 4.16 Asset library — Present, almost entirely unused

This is the most valuable content in the repo, and the code uses **one file** from it (`audio/sfx/Attack01.wav`).

**Characters — 38 wrestlers** (28 in the `General` tables: 8 base + 4×5 DLC; 38 in the `XBOX360` tables: 18 + 4×5). Each has:

- A 5-file data set (`BaseInfo.sml`, `EntranceInfo.sml`, `SFXTable.sml`, `SuperStarWSTable.sml`, `wrestlerData.sml`),
- `avatar.png` + `taunt.png`,
- A `wrestler.zip` containing ~600 `.sst` animation frames (e.g. John Cena: 618).

Also `Announcer/Robert` and `Ref` sprite sets.

**Arenas — 11** (`ARENA02/03/04/06/07/08/09/10/11/12/13`) plus an `entrance/` set with three stage backdrops (`Basic`, `Modern Raw`, `Modern SmackDown`, 7 PNGs). Each arena has ~17–20 PNG layers (`arena0.png`, `ring_left (1..4).png`, `ropes…`, `scene.png`, `shadow.png`, `text.png`) and an `arena.sml` descriptor. The data tables add 9 (General) or 11 (XBOX360) arena entries with entrance-scene IDs; `graphics/Sprites/Character/Announcer|Ref` add 43 files and `Item`/`EFX` 33 files.

**Sprite frames — 23,696 `.sst` files** inside 53 nested ZIPs: 38 wrestler bundles + 15 crowd bundles (24 particles each).

**Game data — 263 SML tables** under `data/Data/`: roster, arena, animation (`WrestlerAT`, `RefereeAT`, `AnnouncerAT`, `ItemAT`), wrestle-style/force/hit-target/airborne/fall-away tables, damage, EFX/particles, sound tables, stage/entrance tables, base info, version info, localization tables (7 languages in `LanguageTable_Android.sml`), plus `_SpriteEditor/` developer tables.

**Audio** — 135 WAV SFX: attack/release/struggle/pain/effort, crowd swells and entrance reactions by level, ring bell, 19 referee count-outs, wrestler-specific announcer calls (`RA_JR_SSN_*`), winner stingers. 11 AAC music tracks: menu, main title, player select, entrances, story segments, end credits, crowd loops.

**Config / implied features** — `data/Global/wrestlefest.ini` carries `isOnline`, `isServer`, `hostname` and `port 5001`, i.e. the original build had LAN multiplayer. This project has **no networking code at all** and the manifest does not request `INTERNET`. `data/Data/Localization/text.txt` is a UTF-16LE string table with 10 language columns (the `LanguageTable_Android.sml` descriptor lists 7); nothing loads either.

**UI** — 171 PNGs in 18 groups: main menu (30), report (17), title menu (16), selector (14), countdown (12), energy bars (12), profile (12), intro (12), stage (10), progress (7), manual (6), hint (5), pause (5), statistics (3), check boxes (2), promotion (2), skip (2), plus backgrounds.

**Fonts / shaders** — 2 `.tyF` bitmap font descriptors with PNG atlases (`Arial Rounded MT Bold`, `Century Gothic`) and a 22 MiB `ARIALUNI.TTF`; 8 GLSL ES 1.00 shaders (`Sprite`, `SpritePalette`, `FontTT`, `Blit`, `BasicLine`, `Clear`) declared in `master.tyFX`.

**The formats are all proprietary and undocumented in this repo:** SML is a plain-text tag format (`<SML V1.1>` + typed fields, e.g. `<U32|Id> 0`), `.sst` is a little-endian binary sprite container (header `00 74 73 73` = `\0tss`), `.tyF` is an SML font descriptor. **No parser for any of them exists in this codebase.**

**Leftovers / provenance notes:** `data/FAULTYCLONES` contains `Cracked by FAULTYCLONES (2012-05-13)`; the extraction report points at a personal dumped IPA and names the iOS bundle `Payload/WrestleFest Premium.app` / `com.thq.wrestlefest02`; `PkgInfo`, `ResourceRules.plist`, `.crc` are iOS packaging artefacts. Harmless in a private tree, but they should be removed together with `assets/` before the repository is shared — which is already the plan.

---

## 5. What actually happens when you run it

1. The launcher starts `MainActivity`; the theme is fullscreen black, landscape.
2. The class initializer loads `libwrestlefest.so`; `onCreate` then collects/installs crash handling and calls `GameActivity.onCreate`, which starts the native thread.
3. `android_main` starts. `AssetLoader` searches for an OBB (JNI → filesystem). If one is found and parses, it is mounted; otherwise APK assets are used.
4. `AudioPlayer` allocates an OpenSL ES engine and preloads `audio/sfx/Attack01.wav` (~10 KB, 3529 compressed bytes).
5. If **no** OBB was found, a modal dialog says the OBB is "required" and tells the user where to copy it.
6. When the window arrives, a GLES 3.2 context is created. If the device cannot do 3.2, the screen stays black.
7. The loop clears the surface to `#090C13`-ish navy and swaps buffers ~60 times per second.
8. Tapping anywhere plays the attack sound (overlapping taps restart it).
9. Nothing else happens: no menu, no logo, no sprites, no music, no gameplay.

That is the full observable feature set of the app.

---

## 6. Findings register

Severity is judged for a private, sideloaded build. **Critical** blocks the port; **High** will bite during development or on-device use; **Medium** is debt/robustness; **Low** is polish. Store-compliance items are mentioned only where they would matter if the project were ever published.

**Note on assets (private project).** The 373 MiB payload and the source markers are intentional and acceptable for local development. Before any sharing, remove `assets/` from both the working tree and Git history, delete the provenance/leftover files, keep `extract_assets.py` so the tree can be rebuilt from a legally obtained IPA, and add a "bring your own IPA/OBB" note to the README. The build-time cost of the payload is covered in H7.

### Critical

**C1 — The game is not implemented.**
`main.cpp:93-102` clears a colour; `main.cpp:133-150` plays one sound. There is no renderer, scene graph, data parser, UI, or simulation. Everything else in the repo is infrastructure. Any expectation that this repository "runs WrestleFest" is unmet.

**C2 — The proprietary asset formats have no decoder.**
23,696 `.sst` sprite frames, 273 `.sml` files (263 game tables + 10 arena descriptors), 2 `.tyF` fonts, and 171 UI PNGs are inert. The iOS engine that consumed them is not in this repo. Porting requires reverse-engineering SST (at minimum) and implementing an SML reader — the single largest work item in the project.

### High

**H1 — Release minification can break the OBB JNI bridge.**
`asset_loader.cpp:73` looks up `findObbFile` by literal name:

```cpp
GetMethodID(clazz, "findObbFile", "()Ljava/lang/String;")
```

The release build enables R8 (`build.gradle.kts:33-37`) with only the default ProGuard file and **no keep rules for this method**. Debug builds are unaffected, but if you ever ship a release APK, R8 can rename the method, the lookup fails, and the app silently falls back to hard-coded `/sdcard` probing — which may miss the OBB on modern Android. Fix: add

```proguard
-keepclassmembers class com.mfm.wrestlefest.MainActivity {
    public java.lang.String findObbFile();
}
```

and preferably cache the `jmethodID` once, with a non-reflective `native` callback as a fallback.

**H2 — Hard GLES 3.2 requirement, arm64 only, minSdk 23.**
`AndroidManifest.xml:7-9` declares ES 3.2 **required** (Play Store filtering), and `main.cpp:45-54` refuses anything lower, with no fallback and no error UI. Many devices — especially those at the stated `minSdk 23` — only expose ES 3.0/3.1. Combined with `abiFilters += "arm64-v8a"` (`build.gradle.kts:16-18`), the installable audience is narrow and x86_64 emulators cannot run it. Fix: try 3.2 → 3.1 → 3.0 → 2.0 in order, target the lowest level the port actually needs (the shipped shaders are GLSL ES 1.00, i.e. ES 2.0-class content), add `x86_64` for emulators, and raise `minSdk` if 3.2 is truly mandatory.

**H3 — The audio subsystem cannot play the game's music.**
11 AAC tracks and 146 sounds are shipped, but only one WAV is loaded and played. There is no music path, no loop/streaming, no mixing, no volume, and no pause on backgrounding (`handleAppCommand`, `main.cpp:152-176`, ignores `APP_CMD_PAUSE`/`LOST_FOCUS`). Fix: move to Oboe/AAudio with a mixer and streaming decode (or pre-convert music to WAV/PCM and stream it).

**H4 — Input is a single global tap.**
Any `ACTION_DOWN` plays a sound (`main.cpp:139-149`); coordinates, pointer IDs, move/up, multi-touch, keys and controllers are ignored, and the viewport's touch mapping is unused. There is no way to express a d-pad, punch, grapple or pause. Fix: build an input layer that maps touches through `ViewportManager` into logical coordinates and feeds a virtual pad; read key events.

**H5 — Crash reports cannot leave the device.**
`CrashLogHandler` writes to the app's private files dir with no rotation, no size cap, no viewer, and no share/upload. Native crashes before API 30 are never captured. Fix: cap/rotate the file, expose a "share crash log" action, and add NDK crash capture (e.g. `ndk-stack`/Crashpad/Breakpad) if native stability matters.

**H6 — OBB discovery is over-broad and under-permissioned.**
`MainActivity.findObbFile` (`:54-120`) scans deprecated external-storage paths and returns **any** `*.obb` in the package directory, with no version/CRC/applicability check. On API 29–32 the `READ_EXTERNAL_STORAGE` permission is declared but never requested at runtime, so raw-path scans can fail silently; the app then contradicts itself by showing a "required OBB" dialog while still running from APK assets. Fix: prefer `getObbDirs()`/`StorageManager` only, validate the archive (and its version code), request permission if legacy paths are truly needed, and make the dialog actionable (or remove it).

**H7 — 351 MiB APK slows every build and install.**
The measured payload is **351.2 MiB** (I re-zipped the asset tree with the same compression policy). Every `assembleDebug`, `adb install`, and update pays for it, and incremental installs can time out on low-end hardware. The fix already exists in the repo but is not wired in: exclude `assets/` from the APK for normal builds, generate the OBB once with `compile_obb.py`, push it with `adb push …/Android/obb/com.mfm.wrestlefest/`, and rebuild it only when content changes. If the project is ever published, Play's 100 MB APK / 200 MB AAB base limits apply.

### Medium

**M1 — OBB parser hardening.**
No Zip64 support (`obb_archive.cpp:241-243` reads 16/32-bit fields only), no upper bound on `cd_size` before `std::vector` allocation (`:252`), `isOpen()` returns false for a valid empty archive (`:92-95`), `fd_` is a fake validity flag (`:81-90`, `:304`), the file is reopened per entry (`:324`), and `findKey` scans linearly twice on misses (`:135-174`). Fix: cap `cd_size` against file size, use a real file handle/`mmap`, add Zip64 or reject archives > 4 GiB explicitly, and index normalized keys.

**M2 — EGL lifecycle leaks and missing app-state handling.**
`Renderer::initialize` (`main.cpp:19-68`) does not release an existing context before creating a new one, so a second `APP_CMD_INIT_WINDOW` leaks a surface/context. `handleAppCommand` (`:152-176`) handles only INIT/TERM/RESIZE/CONTENT_RECT; `APP_CMD_PAUSE`, `RESUME`, `START`, `STOP`, `SAVE_STATE`, `LOW_MEMORY`, `GAINED/LOST_FOCUS` are ignored. No `eglSwapInterval` is set. Fix: guard re-init, handle pause/resume (pause audio, stop rendering), save state, and set swap interval explicitly.

**M3 — 64 MiB asset cap is silent.**
`asset_loader.cpp:19`, `:242` refuses larger files and logs only "Asset not found". The 22 MiB TTF is within budget, but future bundles could exceed it. Fix: log the size rejection distinctly, raise/parameterise the cap, or mmap large assets instead of copying them into a `std::vector`.

**M4 — JNI hygiene.**
`findObbViaJni` (`asset_loader.cpp:51-92`) never calls `DetachCurrentThread`, never checks `ExceptionCheck` after `CallObjectMethod`, and calls `ReleaseStringUTFChars(j_str, chars)` even when `GetStringUTFChars` returned null (`:84`). Fix: check exceptions, guard the release, and detach (or document that the thread is already attached).

**M5 — `compile_obb.py` polish.**
It deflates already-compressed PNG/ZIP/AAC content (`:64`) instead of storing it (slower, negligible gain); the success message "100% of N files verified intact" (`:115`) overstates a check that validates all sizes but decompresses only 5 samples; it always writes a convenience copy to the repo root (`:186-194`), a surprising side effect; `--delete-uncompressed` (`:119-130`) deletes the entire asset tree with no confirmation or dry-run. Fix: `ZIP_STORED` for `.zip/.png/.aac`, report exactly what was verified, make the root copy opt-in, and confirm destructive deletions.

**M6 — Extractor polish.**
Unused `SUPPORT_EXTENSIONS` (`extract_assets.py:37`); the committed report leaks a Windows user path and the source IPA name (`asset_extraction_report.json`); no decompression-size limits. Fix: remove dead code, store relative/normalised provenance, and add a size cap.

**M7 — The GLSL upgrade is not applied.**
All 8 shipped shaders are still GLSL ES 1.00 (`--check` flags all), while the app requests an ES 3.2 context. Also, `upgrade_glsl.py` rewrites files in place without a backup. Fix: apply it as part of the renderer work, or gate it in CI once shaders are loaded.

**M8 — CI is debug-only.**
No native tests, lint, release build, signing, size check, or SHA-pinned actions. The 351 MiB artifact will also be slow to upload. Fix: add a test job (host-compile `viewport_manager_test`), a release job with a keystore secret, `--warning-mode all`, and pin actions by SHA.

**M9 — Startup crash risk in crash reporting.**
`collectPreviousCrashReports` (`CrashLogHandler.java:50-84`) calls `getHistoricalProcessExitReasons` outside a try; an OEM `RuntimeException` here would crash `onCreate`. Wrap it.

**M10 — Build hygiene.**
`kotlin-bom` is declared but no Kotlin plugin is applied (`build.gradle.kts:64`) — dead dependency. Release has no signing config and no mapping upload. `android.enableJetifier=false` is unnecessary. No `nonTransitiveRClass`. Fix accordingly.

**M11 — Viewport work is only half-used.**
The touch/hitbox/scissor helpers and the GL forwarding wrappers are exercised only by tests. Until the renderer and input layers exist, this is untested-in-production code.

**M12 — Audio engine choice.**
OpenSL ES is deprecated; `SLAndroidSimpleBufferQueue` with a single static buffer cannot support overlapping sounds (a new `play()` clears the queue), music, or low-latency response. Plan a migration to Oboe/AAudio.

**M13 — The wrapper is not executable and is not integrity-checked.**
`git ls-tree` shows `gradlew` at mode `100644`; the CI comment at `.github/workflows/build-debug-apk.yml:55-57` claims the opposite. Local `./gradlew` fails with "Permission denied" on a fresh clone. Also `gradle-wrapper.properties` lacks `distributionSha256Sum`. Fix: `git update-index --chmod=+x android_project/gradlew`, correct the comment, and add the checksum.

**M14 — No README or architecture documentation.**
A 373 MiB repo with a non-obvious Gradle subdirectory, three OBB search paths, proprietary formats and manual CI has **no documentation at all**. `CODE_AUDIT.md` is now the only document. Add a README (what this is, how to build, where the OBB goes, how to regenerate assets) and format notes for SML/SST/tyF. A licence matters only if you later share the code.

### Low

- **L1** — 36 duplicate asset groups (2.08 MiB), including five copies of `shadow.png`; trivial to dedupe.
- **L2** — `android:windowFullscreen` is deprecated; on API 35 edge-to-edge is enforced. Use `WindowInsetsController`/`WindowCompat` and consume insets for the GL viewport.
- **L3** — No `.editorconfig`, `.clang-format`, or lint config; C++ brace style is inconsistent between files (`obb_archive.cpp` vs `main.cpp`).
- **L4** — iOS leftovers in the APK: `PkgInfo`, `ResourceRules.plist`, `.crc`, plus the `FAULTYCLONES` marker and `_SpriteEditor` developer tables.
- **L5** — `android:hasCode="true"` is redundant.
- **L6** — `android:screenOrientation="landscape"` is ignored on large screens from Android 16 onward; plan a responsive layout.
- **L7** — Package name is duplicated in three places: `MainActivity.PACKAGE_NAME`, `asset_loader.cpp:kPackageName`, and `compile_obb.py:DEFAULT_PACKAGE_NAME`. Single-source it.
- **L8** — Test target is correctly disabled on Android (`CMakeLists.txt:37-44`), but the host tests are not run in CI.
- **L9** — No app icon: `AndroidManifest.xml` has no `android:icon`/`roundIcon` and `res/` contains only `themes.xml`, so the app installs with the generic Android icon and a hardcoded English label. Add adaptive icons and `strings.xml` when convenient.

---

## 7. Verification log

Commands and outcomes from this audit (host Linux, g++ 12+, Python 3.11):

```
# Native viewport tests
g++ -std=c++17 -Wall -Wextra -pedantic -O2 \
    android_project/app/src/main/cpp/viewport_manager.cpp \
    android_project/app/src/main/cpp/viewport_manager_test.cpp -I… -o /tmp/vptest/vp_test
→ build exit 0, no warnings; ./vp_test exit 0 (5/5 scenarios)

# OBB compiler
python3 compile_obb.py --assets-dir /tmp/obbtest/assets --output /tmp/obbtest/main.1.com.mfm.wrestlefest.obb
→ 3 files packaged, "Verification successful", exit 0
→ also wrote a convenience copy at repo root (side effect, cleaned up afterwards)

# Native OBB reader against that archive
g++ -std=c++17 harness.cpp obb_archive.cpp -lz   # host zlib (headers from the local Node install)
→ entries=3; Attack01.wav / sub/arena.sml / random.bin byte-identical;
  ATTACK01.WAV resolves case-insensitively; missing key returns false; RESULT=PASS

# Shaders
python3 upgrade_glsl.py --check .
→ 8 files "needs upgrade" (all still GLSL ES 1.00)
python3 upgrade_glsl.py . (on a copy)
→ correct #version 300 es, layout(location=…) in/out, fragColor, texture()

# Asset survey
135/135 WAV files supported by AudioPlayer's parser
11 AAC files (ADTS) unsupported
measured APK payload: 1033 entries → 351.2 MiB
373.1 MiB raw assets, 2.08 MiB duplicates, 963 unique hashes
extraction report (980 + 53 bundles) matches the tree exactly
```

---

## 8. Gap analysis and suggested roadmap

The distance to a playable port, in dependency order:

### Phase 0 — Repository & build hygiene (days)
1. Keep the assets where they are while developing. Before sharing, strip `assets/` from the tree *and* history, remove the provenance markers, and add a "bring your own IPA/OBB" note.
2. Add a README (build steps, OBB placement, asset regeneration) and format notes for SML/SST/tyF.
3. `chmod +x gradlew`, add the wrapper checksum, add app icons, fix the release keep rule (§6 H1).
4. Move assets to the OBB for day-to-day builds (§6 H7) to cut build/install time.

### Phase 1 — Content pipeline (weeks)
5. Write an **SML parser** (the format is simple and text-based) and validate it against all 273 tables.
6. Reverse-engineer **SST** (`00 74 73 73` container): header, frame table, per-frame RLE/palette data. This is the critical unknown.
7. Write a **tyF font** loader (SML descriptor + PNG atlas).
8. Build a sprite batcher implementing the existing `Sprite`/`SpritePalette`/`FontTT` shaders (after running `upgrade_glsl.py`), with a texture atlas and palette support.

### Phase 2 — The game (months)
9. Scene/state machine: boot logo → title → main menu → mode select → wrestler/arena select → match → results.
10. Rendering of arenas (multi-layer PNGs), wrestlers (SST animations), crowd (particle bundles), UI (171 atlases).
11. Input: virtual pad from `virtual_pad` art, mapped through `ViewportManager`; controller support.
12. Audio: music playback, per-event SFX voices, announcer lines, crowd reactions.
13. Match simulation: animation-table state machine (`WrestlerAT`, `StateTable`, `WSTable/*`), hitbox/hurtbox logic, damage, pins/submissions, referee counting, AI.
14. Save/profile, options, localization (7 languages already in the data).
15. Performance pass (the original targets 480×320; scaling to 1080p+ needs batching and texture management).

### Phase 3 — Packaging & polish
16. Signed release APK + one-step OBB install script, native crash symbolication, CI for tests/lint, and a quick device matrix.

**Rough effort:** a faithful port is a multi-month effort even for an experienced solo developer. The current repo has completed perhaps the first 10–15%: the build boots, assets are extractable and readable, and the coordinate system is solved.

---

## 9. Quick wins (concrete, low-risk)

```diff
# .github/workflows/build-debug-apk.yml — correct the stale comment
-        # The wrapper is committed with the executable bit; this is a
-        # defensive fallback for checkouts that lose file modes.
+        # The wrapper is tracked as 100644 (not executable), so this is
+        # required for fresh checkouts, not just a defensive fallback.
         run: chmod +x ./gradlew
```

```bash
# Make the wrapper executable in Git
git update-index --chmod=+x android_project/gradlew
```

```proguard
# app/proguard-rules.pro (and reference it from build.gradle.kts)
-keepclassmembers class com.mfm.wrestlefest.MainActivity {
    public java.lang.String findObbFile();
}
-keep class com.google.androidgamesdk.** { *; }
```

```kotlin
// build.gradle.kts — drop the unused Kotlin BOM
-implementation(platform("org.jetbrains.kotlin:kotlin-bom:1.8.22"))
```

```xml
<!-- AndroidManifest.xml -->
<application android:icon="@mipmap/ic_launcher"
             android:roundIcon="@mipmap/ic_launcher_round" …>
```

```python
# compile_obb.py — don't waste time deflating already-compressed data
comp = zipfile.ZIP_STORED if path.suffix.lower() in {".zip", ".png", ".aac"} else zipfile.ZIP_DEFLATED
zip_out.write(path, rel, compress_type=comp)
```

Also worth doing immediately: add a README with the OBB placement instructions (`Android/obb/com.mfm.wrestlefest/main.1.com.mfm.wrestlefest.obb`), add a CI job that compiles and runs `viewport_manager_test.cpp`, and wrap `collectPreviousCrashReports` in a try/catch.

Before sharing the repo, one command removes the assets from history:

```bash
git filter-repo --path android_project/app/src/main/assets --invert-paths
```

---

## Appendix A — Tracked file inventory (non-asset)

| Path | Lines | Purpose |
|---|---:|---|
| `android_project/app/src/main/java/com/mfm/wrestlefest/MainActivity.java` | 121 | Activity, OBB discovery, missing-data dialog |
| `android_project/app/src/main/java/com/mfm/wrestlefest/CrashLogHandler.java` | 162 | Java + system crash capture |
| `android_project/app/src/main/cpp/main.cpp` | 207 | EGL/GLES loop, input, lifecycle |
| `android_project/app/src/main/cpp/asset_loader.cpp` / `.h` | 278 / 40 | OBB discovery, APK fallback, JNI |
| `android_project/app/src/main/cpp/obb_archive.cpp` / `.h` | 439 / 47 | ZIP/OBB reader |
| `android_project/app/src/main/cpp/audio_player.cpp` / `.h` | 299 / 41 | WAV → OpenSL ES |
| `android_project/app/src/main/cpp/viewport_manager.cpp` / `.h` | 199 / 81 | Letterbox/touch/GL mapping |
| `android_project/app/src/main/cpp/viewport_manager_test.cpp` | 129 | Host tests |
| `android_project/app/src/main/cpp/CMakeLists.txt` | 48 | Native build + test target |
| `android_project/app/build.gradle.kts` | 65 | Android/NDK config |
| `android_project/{build,settings}.gradle.kts` | 21 | Gradle setup (`gradle.properties` adds 5 lines of config) |
| `android_project/app/src/main/AndroidManifest.xml` | 31 | Manifest |
| `android_project/app/src/main/res/values/themes.xml` | 10 | AppCompat theme |
| `compile_obb.py` | 198 | OBB compiler |
| `extract_assets.py` | 363 | IPA extractor |
| `upgrade_glsl.py` | 286 | GLSL ES 3.00 upgrader |
| `scripts/convert-audio.ps1` / `.bat` | 75 | ffmpeg audio conversion |
| `.github/workflows/build-debug-apk.yml` | 67 | Debug APK CI |

## Appendix B — Asset formats observed

| Format | Signature / shape | Used by runtime code? |
|---|---|---|
| SML | Text: `<SML V1.1>` + `<TYPE\|name> value` + `<ARRAY\|name> N { … }` | No |
| SST | Binary, starts `00 74 73 73`, little-endian frame data | No |
| tyF | SML `<Font>` descriptor pointing at a PNG atlas | No |
| tyFX | SML `<Effect>` with technique → shader list | No |
| PNG | Standard, 541 files (UI, arenas, avatar/taunt, font atlases) | No |
| WAV | RIFF/PCM, 135 files, 8/16/24-bit, 32–48 kHz, mono (one stereo) | Yes — 1 file |
| AAC | ADTS (`ff f1/f9`), 11 music tracks | No |
| ZIP | Nested bundles containing `.sst` only | Read as raw assets; contents not parsed |
| OBB | ZIP created by `compile_obb.py`, named `main.1.com.mfm.wrestlefest.obb` | Yes |

## Appendix C — Glossary

- **GameActivity** — Android Game Development Kit activity that replaces `NativeActivity`, forwarding lifecycle/input to native code.
- **native_app_glue** — C++ glue that runs `android_main` on its own thread with an `ALooper` and input buffers.
- **OBB** — Android APK expansion file: a ZIP (up to 2 GB) stored under `Android/obb/<package>/`.
- **SML** — the original engine's plain-text serialized data format (tables, descriptors, config).
- **SST** — the original engine's binary sprite container, one file per animation frame.
- **tyF / tyFX** — bitmap-font and shader-effect descriptors in SML.
- **R8** — Android's code shrinker/obfuscator used by `minifyEnabled`.
- **Prefab** — Gradle/CMake mechanism used to consume GameActivity's native static library.

---

*End of report. The repository's code was not modified during this audit; only this document was added.*
