# PocketTerm deployment

Deployed on 2026-09-16 to PiDeck35 (`100.74.103.244`), Raspberry Pi 4 Model B
Rev 1.5, aarch64, Debian 13 (trixie).

## Installed locations on the deck

- Source checkout: `/home/ticnitsi/Documents/gqrx-touch`
- Application: `/home/ticnitsi/.local/opt/gqrx-touch/bin/gqrx`
- Launch command: `/home/ticnitsi/.local/bin/gqrx-touch`
- Application-menu entry: **Gqrx Touch**
- Launcher file: `/home/ticnitsi/.local/share/applications/gqrx-touch.desktop`
- Existing settings backup:
  `/home/ticnitsi/.cache/gqrx-touch-deploy/backup-20260916-114019`

The distribution's `/usr/bin/gqrx` was preserved. The touch launcher uses Wayland
when a Wayland session is present, unless `QT_QPA_PLATFORM` is explicitly set.
The application shares the existing Gqrx receiver settings, which were backed up
before launch. No reboot, firmware, display, Plasma or OSK changes were requested
or performed by the deployment.

The local uncommitted touch changes were transferred directly with their upstream
Git history; no changes were pushed to GitHub. `DEPLOYED-SOURCE.json` records the
transferred source hashes. The upstream base is `08f84f5`.

## Checks completed on the Pi

- Confirmed active KDE Wayland session, 640 × 480 display, scale 1.
- Built the complete application natively with Qt 5.15.15 and GNU Radio 3.10.12.0,
  using the PulseAudio backend (the session uses PipeWire's PulseAudio service).
- Passed the eight Qt UI behavior tests on the Pi with the offscreen platform.
- Verified installed ARM64 executable, resolved shared libraries, and desktop
  launcher syntax.
- Verified Qt startup against the actual Wayland session and confirmed a running
  process using the installed executable with `--touch` and the Wayland backend.
- Verified the source-file hashes before and after an unexpected device reboot.

The device rebooted during the build. Compilation resumed afterward; four empty
intermediate object files left by the interruption were removed and regenerated.
The final build linked successfully. The deployment did not initiate that reboot.

Installed executable SHA-256:
`731f5f3bad00159e86b1cbe1449a3643eddd8daa19c27e3675d0667830816177`

Physical touchscreen gestures, physical key presses and live RF reception remain
unverified. The saved configuration selected HackRF, but no HackRF was present in
the USB listing during deployment; connect it or select another receiver in Gqrx.
A running process and Wayland startup do not prove physical input or RF behavior.

Build and test logs on the deck are under
`~/Documents/gqrx-touch/build/`: `pi-configure.log`, `pi-build.log`,
`pi-install.log`, and `touch-test.log`.

## Compact layout update

The 2026-09-16 density adjustment reduces standard control minimum heights from
44 to 38 pixels, with smaller text and tighter spacing. Frequency keypad digits
retain a 44-pixel minimum. The native Pi rebuild and UI tests passed. The binary
was replaced atomically, preserving the running session; reopen Gqrx Touch to
load the update. Logs: `build/compact-build.log` and `build/compact-tests.log`.
