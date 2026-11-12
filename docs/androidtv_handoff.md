# OpenCE-AndroidTV: handoff notes

State of the 32-bit Android (armeabi-v7a) port in the fork
`murderbymodem/OpenCE-AndroidTV`, branch `armeabi-v7a` (the default branch;
`main` is kept as a plain mirror of upstream `OpenCommunityEdition/OpenCE`).
Test device: Onn 4K Pro Gen 2 (2026), Amlogic S905X5M (4x Cortex-A55), Mali-G310,
OpenGL ES 3.2, Android 14, 32-bit userland, 3 GB RAM.

## What works

The game runs on the box: menus, server browser, campaign, internet multiplayer
(same network version as upstream), sound, controller. Releases build and
publish from GitHub Actions; the in-app updater installs them.

## The port (how the 32-bit app differs from upstream's 64-bit one)

- The game is still a "guest" image loaded by the app ("host"), but compiled as
  32-bit ARM ELF (`--target=armv7a-unknown-linux-musleabi -march=armv7-a
  -mfpu=neon -mfloat-abi=softfp -marm -fsigned-char`), not arm64_32/Mach-O.
  Image `halo_guest_armv7.elf`, linked at 0x88000000 like the arm64 one.
- `configure.py --android-abis armeabi-v7a` (or `arm64-v8a,armeabi-v7a`);
  build dirs `build/android/armv7/...`, jniLibs per ABI; Gradle gets
  `-Phalo.abis`. `--android-guest-cflags` adds flags for diagnostic builds.
- Defines: `HALO_ANDROID_GUEST` (both ABIs, generic guest code),
  `HALO_ARM64_GUEST` (arm64 only), `HALO_ARM32_GUEST` (armv7 only).
- Guest musl keeps AArch64's syscall numbers/structures
  (`guest/libc/arch/arm64_32`); the 32-bit host translates them
  (`host/host_syscall.c` `arm_passthrough`, `host/host_guest_syscalls.h`
  generated from the arch's syscall.h.in, static-asserted on arm64).
  Host compiled with `-D_FILE_OFFSET_BITS=64`.
- Import stubs: `tools/android_imports.py --arch arm` (`ldr pc` through the
  8-byte table). GL stubs: `tools/android_gl_stubs.py --arch arm` pass every
  argument unchanged (the 64-bit widening broke rendering on 32-bit).
- Host loader takes ELF32/EM_ARM; crash/sampler register dumps via macros in
  `host/host.h`.

## Bugs found and fixed (in order)

1. GL stubs widened GLsizeiptr / shader-source pointers for a 64-bit host:
   black screen. Fixed with `--arch arm`.
2. SIGBUS BUS_ADRALN: 32-bit ARM traps unaligned VLDR/VSTR/LDRD/STRD/LDM/STM.
   `host/host_alignment.c` emulates them in the SIGBUS handler (test:
   `tools/test_host_alignment.py`, 54 checks). Logs first fixup per site and a
   10 s summary of the busiest sites.
3. `char` is unsigned on ARM Linux; the game stores NONE (-1) in chars:
   crash in units animation. Fixed with `-fsigned-char`.
4. Black walls / black spots on characters: NORMPACKED3 unpack in
   `port/linux/src/nv2a_vsh.c` relied on `int(uint)` keeping bits; Mali-G310
   did not. Rewritten without the conversion (same results elsewhere).
   Worth offering upstream.

## Performance work (Slayer went from ~20-30 fps to median ~43 fps)

- Hot unaligned sites fixed at source with `HALO_UNALIGNED`
  (`halo_linux_prefix.h`): animation readers (`model_animations.c`,
  `points_interpolate`), network messages copied aligned in
  `network_distributed_handle_message`. Unaligned fixups are now ~0.
- Measured with simpleperf (app is `<profileable android:shell>`) and the new
  `debug.gl_profile` (32-bit only: per-GL-function calls/time every 120 frames;
  host import `host_gl_clock`, `guest/runtime/guest_gl_profile.c`).
  Main thread ~88% busy, about half of it inside the Mali driver.
- Biggest single cost: glBindFramebuffer ~130 us each, ~55/frame, from object
  shadows (3-4 target switches per shadow-casting object). New setting
  `display.object_shadows` (`port/linux/game/object_shadows.c`); default
  false on HALO_ARM32_GUEST, true elsewhere.
- Streaming vertex buffers 16 MB -> 8 MB on armv7 (saves 24 MB GPU memory).
- 32-bit app defaults (port_config.c, HALO_ARM32_GUEST): `display.vsync =
  false`, `display.max_fps = 30` (a steady 30 played better than 30-55 with
  dips), `display.object_shadows = false`. Every other build keeps upstream's.
  Video settings' V-SYNC / FRAME RATE LIMIT rows still change them.
- `debug.gpu_stats` now also logs shader link time and texture upload time.
- Other diagnostic settings added: `debug.gl_refresh_state`,
  `debug.halo.render_chip` system property (0-3, try another chip policy).

Remaining costs per busy frame: ~200-350 draws at ~14 us each, ~400
glUniform4fv, ~400 buffer writes; shader links of 50-90 ms on first use of an
effect. Memory: game process ~563 MB PSS, 343 MB of it GL (textures etc.).

## Branch `claude/androidtv-optimizations` (not yet tested on the box)

Two changes, each its own commit, built here (`ninja android`, armeabi-v7a
release, clang 18 without PGO) but not run on a device. Test the branch's
build (Actions, Android TV, Run workflow, branch
`claude/androidtv-optimizations`: the run's artifacts, no release) before
merging it into `armeabi-v7a`, whose builds the app's updater installs.

- Shader cache (next step 1, done): `display.shader_cache`, the desktop's
  program cache on disk, now runs in the Android app, default true in the
  32-bit app. First run links and stores each program; later runs load it
  (`shader_cache/<driver hash>.bin` under the save root). `debug.gpu_stats`
  logs "programs: N from the shader cache, M linked ...". To check: play a
  map twice with `debug.gpu_stats = true`; the second run should link
  almost nothing and lose the first-use stutters. If rendering is wrong,
  set `display.shader_cache = false` (and say so: the driver's binaries
  would not be trustworthy).
- Link-time optimisation of the guest: `configure.py --android-guest-lto`
  (`CI_ANDROID_GUEST_LTO`, set by the workflow's release build). The image
  has about half as many functions left (the rest inlined). Expected: less
  CPU time in the game's own code (the half of the main thread that is not
  the driver). To check: fps and frame times against the last release in
  the same Slayer game with `display.max_fps = 0`; the log's unaligned
  fixup summary (inlining can make new unaligned sites: fix the busy ones
  with `HALO_UNALIGNED`); a campaign level and a network game for crashes
  (deeper inlining means larger stack frames). To build without it, remove
  `CI_ANDROID_GUEST_LTO` from the workflow.

## Next steps

1. ~~Shader program binary cache~~ done (above). Prelinking at map load is
   still open, for the first run's stutters.
2. ARMv8 (AArch32) guest image: set aside (2026-11-12). The test boxes'
   Cortex-A55 could run it (it is an ARMv8.2 processor under a 32-bit
   Android), but the gain is small (hardware divide, vrint) and it would be
   a second image to build and test. Look at it again only if a profile
   shows time in `__aeabi_idiv`/`__aeabi_uidiv` or the float-to-int helpers.
3. DXT3/DXT5 -> ASTC (DXT1 already goes to ASTC:
   `display.dxt1_astc`): biggest memory win.
4. Merge consecutive small "immediate" draws (HUD/effects, 100-150/frame).
5. Optional: batch object shadows into one atlas to bring shadows back cheaply.
6. Worth measuring after LTO: the softfp calling convention's cost inside
   the guest (floats cross every call in core registers). The guest could
   use the hard-float convention internally, with only its host imports
   declared softfp.
Multiplayer rule: change nothing in the simulation or network messages; keep
`HALO_PORT_NETWORK_VERSION` as upstream's.

## Builds and releases

- CI: `.github/workflows/androidtv.yml`. Push to `armeabi-v7a` -> clang 22
  (apt.llvm.org) + PGO (`pgo/halo_linux.profdata`), release `build-<run+100>`
  with `halo-android-release.zip` and `halo-android-debug.zip`, latest, last five kept. Daily 07:41 UTC
  sync merges upstream main (issue on conflict). Upstream's Build and Server
  image workflows are disabled in the fork. Secrets: `ANDROID_KEYSTORE_BASE64`,
  `ANDROID_KEYSTORE_PASSWORD`, optional `SYNC_TOKEN` (fine-grained PAT,
  Contents + Workflows write) for merges that touch workflows.
- Repository: default branch `armeabi-v7a`; `main` mirrors upstream and
  builds nothing; Pages off; issues off (the sync reports problems as a failed
  run's error, and as an issue only if issues are turned on). Upstream's
  `build.yml` and `server-image.yml` stay in the tree (so merges do not
  conflict) but are disabled in the Actions settings. Auto-update verified on
  the device (build 14 -> CI build).
- The updater (`Updater.java`) checks `BuildConfig.HALO_UPDATE_REPOSITORY`
  (HALO_UPDATE_REPOSITORY, else GITHUB_REPOSITORY, else upstream).
- Signing key: the user holds `opence-androidtv-signing-key.zip` (keystore,
  base64, password; alias `halo`). Never commit it. Builds 1-14 were made by
  hand with this key; CI builds start at 101.
- Local build (cloud session): Android SDK in `~/android-sdk`, NDK 29
  (`ANDROID_NDK_HOME` pinned to 29; NDK 30 also installed, its clang is 21).
  PGO needs clang 22, not available locally:
  `CI_ANDROID_ABIS=armeabi-v7a HALO_BUILD_NUMBER=<n> HALO_ANDROID_KEYSTORE=...
  HALO_ANDROID_KEYSTORE_PASSWORD=... python3 tools/ci_build.py android release`.
  `tools/ci_build.py` also takes `CI_ANDROID_GUEST_CC`.

## Testing on the box (commands for the user, Windows PowerShell)

- Install: `adb install -r <apk>`
- Settings: `adb pull /sdcard/Android/data/com.halo.decomp/files/config.toml`,
  edit existing keys (duplicates are skipped), `adb push` it back, restart.
- Logs: `adb logcat -c`, play, `adb logcat -d -s halo > halo.txt`;
  crash: `adb logcat -d -s halo DEBUG libc crash_dump32 AndroidRuntime > crash.txt`.
  PowerShell may write UTF-16 (convert with iconv before reading).
- Screenshot: `adb shell screencap -p /sdcard/spots.png` then `adb pull`
  (`exec-out ... >` corrupts the PNG in PowerShell).
- Memory: `adb shell dumpsys meminfo com.halo.decomp:game` (the game process).
- Profile: `adb shell simpleperf record --app com.halo.decomp -e cpu-clock -f 1000
  --duration 30 -o /data/local/tmp/perf.data`, pull, zip before uploading.
  Guest samples show as "unknown" with absolute addresses: symbolize against
  the matching `halo_guest_armv7.elf` (extract from the APK's assets).
