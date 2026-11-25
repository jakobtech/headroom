# Changelog

Versions follow `MAJOR.MINOR.PATCH`, with `-alphaN` while a version is being tested.
Every state that was tested and worked is tagged `vX.Y.Z[-alphaN]`.

Going back to a known state:

```
git checkout v2.1.0-alpha1      # look around (detached)
git switch -c back-to-alpha1 v2.1.0-alpha1   # continue from there
git revert <commit>             # undo one change, keep the history
```

## 2.1.0-alpha7 (in progress)
- Two outputs at once: Ctrl + right-click on the master row (or on an app, an output row or a listened-to input) and choose *Also play on another output* to play the same sound on a second output as well, for example speakers and a headset. The sound runs through the mixer's engine, so it needs VB-CABLE. An app's own second output wins over the one of its output.
- Sync the outputs: right after choosing, a page opens to line the two up by ear (also *Sync the two outputs* at the end of the same list). *Play a test sound* gives a soft low tap on the first output and a higher one on the second; a slider (up to 1 s, finer near zero) and buttons for -10, -1, +1 and +10 ms move the second output later (right) or the first one (left) until the two are heard as one.

## 2.1.0-alpha6
- Start-up: when the settings file or the log cannot be read right after logging in (the disk or a virus scanner is still busy), Headroom waits for it, never writes over it, and loads the settings as soon as it can be read. Before, a session could start with everything at its default: no listened-to input, System sounds back in the list.
- A listened-to input that could not be started (for example because it was not ready yet) is tried again after a few seconds.
- A listened-to input that is not connected stays in the list.
- The log (`%LOCALAPPDATA%\Headroom\log.txt`) says more: which rows the list shows, the hidden list, the level and output of a listened-to input. It opens again later if it could not be opened at start-up.

## 2.1.0-alpha5
- Inputs: a line-in (or any input) can be listened to through Headroom. In the microphone tab, open the device list, right-click the input and choose *Listen through Headroom*. It then shows up in the playback list like an app, with its own volume, mute, output, equalizer, loudness equalization, LoudMax and gain.
- Settings: an *Apply* button puts the changes into effect without closing the window.
- Settings: *Open the mixer with a key*: a key combination that opens (and closes) the mixer from anywhere, at the bottom right of the monitor the mouse is on. Windows shows the tray icon only on the main monitor's taskbar.
- Settings: *Extended equalizer range* and *Equalizer range* let the equalizer bands go beyond 12 dB, up to 60 dB.
- A listened-to input has the options of an app: volume up to 500 % (unlock more volume), gain, mute hotkey, volume at startup, output, equalizer and effects.
- Settings: *Equalizer APO* On/Off decides who runs the equalizers of outputs: Equalizer APO, or Headroom's own engine.

## 2.1.0-alpha4
- Updates: *Check for updates* in the settings window looks for a newer release on GitHub, downloads it, checks it against the release's checksums and installs it over the running copy (with the setup if installed, by replacing the file if it runs on its own), then restarts.
- Gain per output: in the Effects page of an output, a switch and a slider raise a quiet device by up to 12 dB (works through Equalizer APO for all sound, or through the mixer's engine for apps that run through it), with a limiter behind it against crackling.
- Export and import now also carry "Start with Windows" and the output Windows keeps for each app.
- Before a reset or an import replaces the settings, a copy is kept as `settings.before-replace.cfg`.
- The silent output goes back to stereo whenever no app needs surround sound, also after a reset.
- The small bar below the selected choice in selection controls is gone.

## 2.1.0-alpha3
- The loudness speed "Compet." is now called "Comp".

## 2.1.0-alpha2
- A setup program (`Headroom-Setup-<version>.exe`) next to the portable `Headroom.exe`: installs for the current user without administrator rights, with a Start menu entry and optional desktop icon and *Start with Windows*; uninstalls cleanly and asks whether to keep the settings.

## 2.1.0-alpha1
- Settings window: scroll bars for the hidden apps and hidden devices lists, Reset to default (everything, after a confirmation), and Export / Import of all settings to a file.
- The app is now called Headroom. Settings, log and the autostart entry move along on the first start; the Equalizer APO files are renamed too.
- Surround channels: for games that play 5.1/7.1, every channel (center, back, sides ...) gets its own volume per app (0 to 200 %),
  for example to hear footsteps better. The silent output is set to 7.1 while an app uses it and back to stereo afterwards.

## 2.0.0 (first stable version)
- Everything from the alpha versions below, as one tested state
- New README

## 2.0.0-alpha20
- Fixed an empty box that stayed under the preset picker: closing the presets list with no saved presets
  opened a second, empty list ("Delete a preset") by mistake
- Short badges ("LE") are no longer dropped; list windows that outlive their list are removed

## 2.0.0-alpha16
- Badges sit closer together and the startup volume one is short ("S50"), so four fit next to a name
- The equalizer / effects panel returns to its place when it gets shorter again (after Custom)
- "Volume at startup" without the dots

## 2.0.0-alpha15
- Own icon for "Volume at startup"

## 2.0.0-alpha14
- An output's volume slider no longer lags with USB and wireless headsets: the volume is sent on a thread
  of its own, only the latest value counts

## 2.0.0-alpha13
- Loudness speeds fit the panel; menus are as wide as their longest entry
- A "Start 50%" badge shows where a volume at startup is set (click it to change)
- Master volume menu has "Volume at startup" too

## 2.0.0-alpha12
- Loudness equalization: new mode "Comp" for games (quiet footsteps up to 15 dB louder, shots and
  explosions cut by up to 18 dB within milliseconds, 1.5 ms look-ahead, shorter output queue)
- Custom loudness settings start from a ready-made speed; the preset list offers each as "Start from ..."
- "Volume at startup" in the menu of apps and devices: an app is set to it whenever it starts to play,
  a device whenever the mixer starts or the device comes back
- System sounds are listed under hidden apps in the settings, no longer as "more hidden without a name"

## 2.0.0-alpha11
- Settings: hidden apps and outputs side by side, no custom apps and no import button, window at system scale

## 2.0.0-alpha9
- Microphone tab: a button next to the outputs button switches the mixer to the microphone. It shows the
  default microphone (volume, mute), every input device on request, and the apps that record right now
  (volume and mute per app). Pick the default microphone from the list. Effects, boost and per-app output
  stay with playback.

## 2.0.0-alpha8
- Equalizer APO is optional: an output's equalizer and LoudMax run in the mixer's own
  engine when it is not installed (setting "Output effects": Auto, APO, Built-in)
- Measured: the engine adds about 43 ms (not 30); texts say 40 ms now
- Setup guide marks Equalizer APO as optional

## 2.0.0-alpha7
- Saved loudness-equalization presets, usable for every app
- (i) explanations next to settings
- Setup guide page, hidden outputs in the settings window

## 2.0.0-alpha6
- Loudness equalization expert mode (custom values)
- Tray menu and settings window in the mixer's own style
- Hiding an output now needs Ctrl+right-click; restore it from the tray menu

## 2.0.0-alpha5
- LM / LE badges open the effects page
- Ultra short loudness equalization
- Choosing a release time no longer switches the effect on

## 2.0.0-alpha4
- Effects is its own page, no longer part of the equalizer
- The build script closes a running copy by itself

## 2.0.0-alpha3
- Effects entry in the menus
- 31-band layout removed
- No automatic preamp for apps

## 2.0.0-alpha2
- Equalizer with the same faders for every band count
- Effects page, framed LoudMax window

## 2.0.0-alpha1
- First standalone build: platform layer, tray menu, settings window
