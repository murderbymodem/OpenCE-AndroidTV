# OpenCE-AndroidTV: handoff notes

State of the 32-bit Android (armeabi-v7a) port in the fork
`murderbymodem/OpenCE-AndroidTV`, branch `armeabi-v7a` (the default branch;
`main` is kept as a plain mirror of upstream `OpenCommunityEdition/OpenCE`).
Test device: Onn 4K Pro Gen 2 (2026), Amlogic S905X5M (4x Cortex-A55), Mali-G310,
OpenGL ES 3.2, Android 14, 32-bit userland, 3 GB RAM; and the Onn 4K Plus
(2026), the same but 2 GB RAM.

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

## The optimisation branch (merged 2026-11-12)

Five changes from `claude/androidtv-optimizations`, each its own commit,
merged into `armeabi-v7a` after the user played a multiplayer match with
the branch's build on the Onn 4K Plus without problems (frame drops remain
with many players and explosions on screen, less often than before). Not
yet measured: fps against the previous release, ARMv7 against ARMv8
(`debug.halo.guest_image`), the second run's shader cache counts, and
whether the log's `processor:` line says ARMv8 on both boxes. Goal agreed
with the user: run on any 32-bit Android TV hardware, tuned for the Onn 4K
Pro and Onn 4K Plus (2026).

- Shader cache: `display.shader_cache`, the desktop's program cache on
  disk, now runs in the Android app, default true in the 32-bit app. First
  run links and stores each program; later runs load it
  (`shader_cache/<driver hash>.bin` under the save root). `debug.gpu_stats`
  logs "programs: N from the shader cache, M linked ...". To check: play a
  map twice with `debug.gpu_stats = true`; the second run should link
  almost nothing and lose the first-use stutters. If rendering is wrong,
  set `display.shader_cache = false` (and say so: the driver's binaries
  would not be trustworthy).
- Link-time optimisation of the guest: `configure.py --android-guest-lto`
  (`CI_ANDROID_GUEST_LTO`, set by the workflow's release build). About half
  the functions are inlined away. To check: fps and frame times against the
  last release in the same Slayer game with `display.max_fps = 0`; the
  log's unaligned fixup summary (inlining can make new unaligned sites: fix
  the busy ones with `HALO_UNALIGNED`); a campaign level and a network game
  for crashes (deeper inlining means larger stack frames). To build
  without it, remove `CI_ANDROID_GUEST_LTO` from the workflow.
- ARMv8 image: the app carries `halo_guest_armv8.elf` beside
  `halo_guest_armv7.elf` and the host starts it where the processor is
  ARMv8 (log: `processor: platform v8l ... ARMv8`, `guest image:
  halo_guest_armv8.elf`). Hardware divide and VRINT/VCVT rounding in line
  instead of ~600 calls to routines; `-mtune=cortex-a55`. To compare on the
  box: `adb shell setprop debug.halo.guest_image armv7`, restart, measure;
  `armv8`, restart, measure; `""` to go back. If the log says ARMv7 on the
  Onn boxes, send the `processor:` line (the detection needs widening).
  A network game between the two images (or with a PC) checks they stay in
  step.
- `memcpy`/`memmove` through NEON registers (both images,
  `guest_string.c`), tested on x86 against the C library.
- ARMv7 image only: `__builtin_rint` in line (`halo_linux_prefix.h`),
  tested bit for bit against `rint`.

Symbolizing: use the ELF the log's `guest image:` line names, from the same
build (the APK's assets).

## Profile of a script-heavy modded server (2026-11-12, build 112, Onn 4K Plus)

The user's simpleperf recording of a modded server's map that ran at 5-15
fps (not the usual frame drops). Symbolized against the release's
`halo_guest_armv8.elf` (the ARMv8 image was the one running: the log's
unaligned site 0010e04c is a VLDR there). Guest threads all show as
"Thread-2"; split them by thread id.

- Main thread 91% of a core: HaloScript's interpreter 45% (`hs_stack_allocate`
  11%, `hs_thread_main` 8%, `hs_evaluate` 7%: about a million stack
  allocations a second, the map's scripts), objects and units 13%,
  collision 9%, animation 4%, the renderer's own code 4%, the Mali driver
  7%, kernel 3%.
- The sound mixer's thread 14% of a core (`mix_voice`, `mix`), internet
  play's thread 6%. Neither is on the main thread.
- The log had ~1500 unaligned fixups a second at two VLDRs in
  `hs_evaluate_inequality`: a script's real numbers read from its thread's
  stack. Fixed at the source in the branch (`hs_stack_real` in
  `hs_runtime.c`, and the arguments' structures packed on 32-bit ARM in
  `hs.c`).

So on that map the simulation, not drawing, is the limit, and the
interpreter's cost per node (its checks on every stack allocation) is what
there is to gain; nothing in it says where a normal map's drops come from.
Wanted: the same recording on a stock map during the drops (many players,
grenades).

## Profiles of stock maps (2026-11-12, build 112): the frame drops are the sound mixer

Two 30 s recordings in normal network games, at the 30 fps limit.

- Main thread: about 50% of a core (Mali driver 26% of that, kernel 7%,
  `prepare_draw` 4%), and never over 84% in any 100 ms: it is not out of
  time.
- The mixer's thread (`mix_voice`): 36% and 52% of a core over the
  recordings, 76% at the 90th percentile of 100 ms windows and up to 93%.
  About 40% of it is the HRTF's filters (`audio.hrtf`, on by default since
  the main project's 176b10c5), 26% the 48-tap sinc resampler: the user's
  `config.toml` still says `audio.resampler = "sinc"`, the default before
  176b10c5 (a setting is written to the file with the default of the day it
  first appears, and later defaults do not replace it).
- The mix held `mixer_lock` for all of each 1024-frame mix and took it
  again at once, and the game's thread takes that lock for every sound
  call: with the mix busy 50-90% of the time, the game waited on it. That
  fits the drops coming with many players and explosions (many voices).

In the branch: everything but the mix takes the lock through `mixer_gate`,
which the mix passes before each turn, so a waiting game thread goes first;
and the 32-bit app's mix takes turns of 128 frames (`MIX_TURN_FRAMES`). The
sound itself is unchanged (the mixer's harness tests give the same results).
Not yet run on a device. On the box, also set `audio.resampler = "xbox"`
(the current default, an eight-tap low pass); `audio.hrtf = false` would
take another 40% off the mixer at the cost of the Xbox's 3D filters, and
should not be needed once the lock is fair.

Result on the box (the user's fourth recording, the branch's build): still
dips, and the recording caught one. For 1.5 s the mixer's thread was at
87-88% of a core while the main thread fell from ~70% to ~30% (its Mali
share from ~20% to ~5%): it was waiting, not working. The gate alone made
each of a frame's sound calls wait out a 128-frame turn (2.4 ms at that
load). Two more changes in the branch:

- the mix stays out while a burst of the game's calls lasts (it waits until
  none has passed the gate for 60 us, at most eight times a turn);
- `audio.hrtf` defaults to false in the 32-bit app (about 40% of the mixer).

The user's log also says `settings: cannot write .../config.toml` and
`menus: could not set audio.resampler`: the file was pushed with adb and
the app cannot write it, so it still says `sinc` and no setting changed in
the menus is kept. Fix on the box: pull a copy, delete the file with adb,
start the app (it writes a new one with today's defaults), set things again
in the menus. Settings added since (`audio.hrtf` among them) take their
default at every start there, so the new default applies at once.

The branch build's guest image could not be fetched here (Actions
artifacts come from a host the session's network does not allow), so that
recording's guest samples are unsymbolized; releases' assets can be.

If drops remain after this, record again: the next suspects are the Mali
driver's share of the main thread and the GL thread idea below.

## Turn buttons (branch `claude/androidtv-quick-turn`, not yet tested on the box)

The user plays with an 8BitDo Ultimate 2 over Bluetooth and wanted its
back paddles to turn quickly, without anything that would count as a cheat:
not a fixed or instant 180, but what the look stick held over does at a
high sensitivity. While a turn button is held the view turns at
`input.quick_turn_rate` degrees a second (400 in the 32-bit app, off
elsewhere, 720 at most), added to the stick's look in `player_control.c`
(`halo_linux_quick_turn`, `xinput_sdl.c`) before the stun and the magnetism,
which slow it as they slow the stick's. Local input only: nothing machines
must agree on.

Which buttons the paddles are over Bluetooth is a guess: Android's
BUTTON_C (left) and BUTTON_Z (right), SDL's joystick buttons 17 and 18,
from SDL's mappings of the 8BitDo Ultimate 2C; the Ultimate 2 has four
(L4, R4, PL, PR). With the turn on, the log names each unnamed button the
first time it is pressed (`gamepad: buttonN pressed`); `input.quick_turn_left`
and `input.quick_turn_right` take `buttonN`. The host reads raw joystick
buttons for the guest through `host_sdl_gamepad_button` (0x100 + N).

## Next steps

1. Measure the branch on the box (above), then a fresh simpleperf profile
   of the ARMv8 LTO build: the remaining ideas below should be chosen from
   it, not guessed.
2. A GL thread: the main thread is ~88% busy and about half of that is
   inside the Mali driver, while three of the four cores idle. Queuing the
   frame's GL calls (the guest's generated entry points, `guest_gl.c`) to a
   second thread would move the driver's time off the game's thread. The
   largest possible gain left, and the largest change: calls that return
   values must wait, and pointer arguments must be copied. Worth a design
   pass before any code.
3. DXT3/DXT5 -> ASTC (DXT1 already goes to ASTC: `display.dxt1_astc`):
   biggest memory win.
4. Merge consecutive small "immediate" draws (HUD/effects, 100-150/frame).
5. Optional: batch object shadows into one atlas to bring shadows back cheaply.
6. Smaller compiler-level items, each to be measured: release images
   without frame pointers (r11 back as a register; crash reports and the
   sampler would lose their call chains), `floorf`/`ceilf` in line in the
   ARMv7 image (139 calls left), the hard-float convention inside the guest
   (needs the compiler runtime rebuilt: lld refuses to mix it with the
   NDK's softfp libgcc).
7. Prelinking the programs at map load, for the first run's stutters (the
   shader cache covers later runs).
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
