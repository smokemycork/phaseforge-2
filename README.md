# PhaseForge

**Phase-align your bass to your kick, then lock the whole low end to your key.**

Put PhaseForge on the bass (or any low element), send the kick into its sidechain, and it
measures how the two line up at every frequency from 20 to 400 Hz. It can find the best
delay and polarity for you, or you can line it up by eye on a live kick-and-bass overlap view.

- VST3 (Windows + macOS), Audio Unit (macOS), standalone app
- Fixed 10 ms latency, reported to the host (your DAW compensates automatically)

---

## Using it in Ableton Live

1. Put **PhaseForge** on your **bass** track.
2. In Ableton's device strip for PhaseForge (not inside the plugin window), switch **Sidechain** on and set **Audio From** to your **kick** track (Live 10.1+).
3. Play a loop. The badge in the header turns green: **KICK CONNECTED**.
4. Press **MAKE IT MATCH**, or drag the yellow wave until it lines up with the pink one.

> No kick on the sidechain = nothing to align to. The UI tells you so. Click **? HELP** for these steps inside the plugin.

### The main screen

- **OVERLAP view** - the kick (pink) and your bass after PhaseForge (yellow) drawn on top of each other,
  locked to the kick hit so the picture holds still. The faint dashed line is the bass before.
  The fill between the waves is **mint where they add up** and **red where they fight**; the strip
  along the bottom shows the same thing at a glance. Aim for mint.
- **Tiny moves** - drag the waves (hold SHIFT for 10x finer), scroll the wheel (0.1 ms, SHIFT = 0.01 ms),
  or use the -1.0 / -0.1 / -0.01 / +0.01 / +0.1 / +1.0 ms buttons. Double-click to reset.
  Zoom 12 / 25 / 50 / 100 ms. **FREEZE** holds the picture.
- **BY FREQUENCY view** - phase of the bass vs the kick at every pitch (20-400 Hz). Dots on the green
  line are in phase. Cream = before, yellow = after your current settings.
- **HOW IN-SYNC?** - a face that goes from worried to smiling, a match %, and a plain-language hint
  ("Move the bass earlier - about 1.20 ms").
- **SONG KEY** - detected (or manual) key, bass and kick notes, and whether the kick is in tune with the key.
- **PRO TOOLS** - phase twist (all-pass stages), twist pitch/shape, band split ("Only below split"),
  follow pitch, reaction speed, mix and volume.

### Buttons

| Control | What it does |
|---|---|
| **MAKE IT MATCH** | Listens for ~2 s of kick + bass, then sets the shift and flip for you. |
| **AUTO-FOLLOW** | Keeps re-matching as the music plays. |
| **FLIP** | Flips bass polarity (fixes a bass that is 180 degrees out). |
| **RESET** | Back to no shift, no flip. |
| **FREEZE** | Holds the display so you can fine-tune. |

---

## Build locally

Requirements: CMake 3.22+, a C++17 compiler (Visual Studio 2022 on Windows, Xcode on macOS).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure     # DSP unit tests
```

JUCE 8.0.14 is downloaded automatically on first configure. Artefacts end up in
`build/PhaseForge_artefacts/`. Add `-DPHASEFORGE_COPY_AFTER_BUILD=ON` to copy the plugin
straight into your system plugin folder.

The DSP tests need no JUCE at all:
`g++ -std=c++17 -O2 tests/dsp_test.cpp -o dsp_test && ./dsp_test`

---

## Making the installer on GitHub (step by step)

The workflow in `.github/workflows/build.yml` builds on real Windows and macOS machines,
runs the tests, builds the installers and attaches them to a GitHub Release.

### One-time setup

1. Create a new repository on github.com (e.g. `PhaseForge`). Public repositories get free
   build minutes on every runner.
2. Upload this folder's contents to it (drag-and-drop in the browser works, or):
   ```bash
   git init
   git add .
   git commit -m "PhaseForge 1.0"
   git branch -M main
   git remote add origin https://github.com/<you>/PhaseForge.git
   git push -u origin main
   ```
3. Open the **Actions** tab. The *Build & Package* workflow starts on its own. The first run
   takes roughly 10–20 minutes (it downloads and compiles JUCE).
4. When it's green, open the run and download the **PhaseForge-Windows** and
   **PhaseForge-macOS** artifacts at the bottom. These are your installers.

### Publishing a release

```bash
git tag v1.0.0
git push origin v1.0.0
```

The workflow runs again and attaches `PhaseForge-1.0.0-Windows-Setup.exe` and
`PhaseForge-1.0.0-macOS.pkg` to a new Release at
`https://github.com/<you>/PhaseForge/releases`. Send people that page.
(Settings → Actions → General → Workflow permissions must allow *Read and write*; the
workflow file already requests `contents: write`.)

### What the installers do

| | Windows (`.exe`, Inno Setup) | macOS (`.pkg`) |
|---|---|---|
| VST3 | `C:\Program Files\Common Files\VST3\PhaseForge.vst3` | `/Library/Audio/Plug-Ins/VST3` |
| Audio Unit | – | `/Library/Audio/Plug-Ins/Components` |
| Standalone | `C:\Program Files\PhaseForge` + Start Menu | `/Applications` |
| Uninstall | Windows "Apps & features" | delete the three items above |

After installing, rescan plugins in your DAW (Ableton: Preferences → Plug-ins → Rescan).

### The "unsigned" warnings (normal for free hobby projects)

- **Windows SmartScreen** shows "Windows protected your PC" → *More info → Run anyway*.
  Removing it needs a code-signing certificate (~$100–400/yr).
- **macOS Gatekeeper** says the developer can't be verified → right-click the `.pkg` →
  *Open* (or System Settings → Privacy & Security → *Open Anyway*). Removing it needs an
  Apple Developer account ($99/yr); then set the `APP_SIGN_ID` / `INSTALLER_SIGN_ID`
  variables used by `installer/macos/build-pkg.sh` (and notarize).

### Changing things

- **Version:** comes from the tag (`v1.2.0` → `1.2.0`).
- **Name/IDs:** `CMakeLists.txt` (plugin name, 4-char codes), `installer/windows/PhaseForge.iss`
  (keep the `AppId` GUID forever), `installer/macos/distribution.xml`.
- **Windows 32-bit / Linux:** not built by the workflow.

---

## License note

PhaseForge uses the [JUCE](https://juce.com) framework, which is available under AGPLv3 or a
commercial licence. Releasing PhaseForge free and open source (AGPLv3) is fine. If you want
to sell it closed-source, you need a JUCE commercial licence.

## Project layout

```
Source/DspCore.h          all DSP: delay, crossover, all-pass, YIN pitch, key finder,
                          cross-correlation, per-frequency phase spectrum (no JUCE needed)
Source/PluginProcessor.*  audio thread, parameters, presets, background analysis thread
Source/PluginEditor.*     sunny UI, overlap view, frequency view, key panel
tests/dsp_test.cpp        unit + end-to-end tests of the DSP
installer/                Inno Setup script + macOS pkg builder
.github/workflows/        CI: build, test, package, release
```
