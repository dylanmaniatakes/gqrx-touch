# Validation

Checked 2026-09-16 against upstream Gqrx commit
`08f84f5` (`v2.17.7-22-g08f84f5`) on macOS arm64.

## Passed locally

- Full Gqrx build with the touch adapter enabled: Qt 6.9.2, GNU Radio 3.10.12.0,
  Apple command-line tools, PortAudio, and a locally built gr-osmosdr with file
  and SoapySDR support. Dependencies were installed only in the ignored build
  directory where additional builds were needed.
- Full desktop-only build with `ENABLE_TOUCH_UI=OFF`; help output contains no
  touch-specific options.
- Eight Qt UI test cases with Qt 5.15.19 and again with Qt 6.9.2, using Qt's
  offscreen platform. These cover 640 × 480 geometry and primary target bounds,
  frequency stepping and limits, bidirectional mode synchronization, arrow/WASD
  navigation, editing, numeric keypad entry, refreshed menu actions, scrolling
  dialogs and auxiliary windows, original control dimensions/focus restoration,
  repeated desktop/touch switching, and preference preservation on shutdown.
- One full application integration scenario with the real MainWindow and a
  generated complex-float I/Q file. This starts and stops the receiver, renders
  the spectrum, changes frequency and demodulation, visits all six original
  panels, opens the I/Q and remote-settings dialogs, checks saved frequency,
  and repeats the desktop/touch transition.
- Visual inspection of actual 640 × 480 application screenshots. The
  documentation images are captures from the integration scenario, with
  synthetic input, not mockups.
- Whitespace/error checks with `git diff --check`.

The Qt-only CTest target reports 10 passes including setup/cleanup (eight behavior
cases). The integration target reports three passes including setup/cleanup (one
scenario). Offscreen-platform size-hint warnings and an upstream missing-font
warning were emitted; tests completed successfully.

The full Qt 5 application build was not validated on this Mac: the installed Qt 6
headers in Homebrew's shared include directory conflicted with Qt 5 framework
headers. The adapter and original frequency controller compile and pass their
standalone tests under Qt 5. The complete application was built under Qt 6.

Local logs are under the ignored `build/` directory:
`touch-test.log`, `touch-qt6-test.log`, `integration-test.log`,
`integration-build.log`, and `desktop-build.log`. Screenshots of every panel are
in `build/screenshots/`. GitHub workflow definitions were added locally; no claim
is made that hosted CI has run.

## Native Pi deployment and remaining checks

A subsequent native Pi build, installation, UI test run and Wayland startup check
are documented in [DEPLOYMENT-PI.md](DEPLOYMENT-PI.md). Physical touchscreen and
movement-key behavior and live RF reception remain unverified. The
synthetic receiver test does not prove audio quality, SDR driver behavior,
recording reliability, RDS/AFSK decoding, remote-client operation, battery life or
thermal performance on the PocketTerm.

Suggested device acceptance pass:

1. Build on the Pi with its normal SDR driver packages and start `gqrx --touch`.
2. Check the entire screen at 100% scale and in fullscreen, then verify touch
   alignment and swipe/scroll behavior.
3. Check arrows or WASD, Enter, Escape and Tab through every page and a file
   chooser. If dedicated movement keys send gamepad events instead of keyboard
   events, configure a keyboard mapping before relying on them.
4. Select the intended receiver, tune a known signal, and check demodulation,
   filter, RF gain, squelch, audio and device limits.
5. Check audio/IQ recording and playback, bookmarks, decoder options and any
   remote control features used on the deck.
6. Toggle UI modes during reception, then restart and check both receiver
   settings and the saved UI choice.

## Local development commands

On this Mac, the successful full build uses the private gr-osmosdr prefix under
`build/deps/install`, and the existing Apple command-line toolchain:

```sh
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
cmake -S . -B build/full-qt6 -DFORCE_QT6=ON \
  -DCMAKE_PREFIX_PATH="$PWD/build/deps/install;/opt/homebrew" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DENABLE_TOUCH_INTEGRATION_TESTS=ON
cmake --build build/full-qt6 -j4
GQRX_TOUCH_SCREENSHOTS="$PWD/build/screenshots" \
  ctest --test-dir build/full-qt6 --output-on-failure
```

The resulting executable is `build/full-qt6/src/gqrx`. It is a local development
build with external Homebrew dependencies, not a portable macOS application
bundle or Raspberry Pi binary.
