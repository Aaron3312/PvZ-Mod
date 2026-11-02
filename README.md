# PvZ-Mod: mods, Versus and AR for PvZ-Portable

An unofficial fork of [wszqkzqk/PvZ-Portable](https://github.com/wszqkzqk/PvZ-Portable), the community reimplementation of Plants vs. Zombies GOTY. It adds:

- **A hack menu** (Dear ImGui) with infinite sun, no cooldowns, damage and zombie-health sliders, game speed x1–x10, level select and more.
- **8 new plants and 8 new zombies**, plus **5 extra minigames** that use them.
- **Versus**: two players on five stages (Day, Night, Pool, Fog, Roof). One plays the plants, the other sends the zombies and pays with brains. The zombie player can use the same computer or join from **any browser** on another device.
- **AR in the browser**: the game keeps running on your computer, and a phone shows the lawn standing on a real table (WebXR, Chrome on Android). You play by tapping it.
- **AR on the phone**: the whole game runs on an Android phone, and the lawn appears as a **3D diorama on your table**, drawn by the game itself with ARCore. You need neither a computer nor a network.
- **A 3D diorama in the desktop window**, the same one the phone AR uses, with an orbit camera.
- **An extractor for `main.pak`**, so you can swap sprites, sounds and texts.

> **No game assets are included.** Like upstream, you need `main.pak` and `properties/` from your own legally purchased copy of Plants vs. Zombies: GOTY Edition (from [Steam](https://store.steampowered.com/app/3590/Plants_vs_Zombies_GOTY_Edition/) or EA). The new plants and zombies are recolors and filters of the original sprites, generated on your machine from those files. This is a non-commercial fan project. It is not affiliated with or endorsed by PopCap Games or Electronic Arts, and it keeps upstream's license, LGPL-3.0. Report mod bugs here, not upstream.

The in-game menus added by the mods are in **Spanish**; the [menu glossary](#menu-glossary) translates them. [MODS.md](MODS.md) is the full reference, in Spanish: every plant, zombie and rule, plus the code layout and the test switches.

## Contents

- [Getting the game data](#getting-the-game-data)
- [Build and run on a computer](#build-and-run-on-a-computer)
- [Build and install on Android](#build-and-install-on-android)
- [The hack menu](#the-hack-menu)
- [New plants, zombies and minigames](#new-plants-zombies-and-minigames)
- [Versus](#versus)
- [AR in the browser (game on the computer)](#ar-in-the-browser-game-on-the-computer)
- [AR on the phone (native, ARCore)](#ar-on-the-phone-native-arcore)
- [3D diorama in the desktop window](#3d-diorama-in-the-desktop-window)
- [Modding sprites, sounds and texts](#modding-sprites-sounds-and-texts)
- [Testing without a mouse](#testing-without-a-mouse)
- [Where the code is](#where-the-code-is)
- [Menu glossary](#menu-glossary)
- [Credits and licenses](#credits-and-licenses)

## Getting the game data

You need two things from the Windows version of PvZ GOTY: `main.pak` and the `properties/` folder.

- **Steam:** open the Steam console (`steam://open/console`) and run `download_depot 3590 3591`. You get the Windows depot (about 51 MB) even on macOS or Linux, and Steam prints the folder where it landed.
- **EA / an installed copy:** take both from the game's install folder.

Put them in one folder. On the desktop, pass that folder with `-resdir=`. On Android you import it as a ZIP (see below).

## Build and run on a computer

The dependencies are the same as upstream: see [Dependencies](#dependencies) further down for Linux, Windows (MSYS2) and macOS. The hack menu and mods build by default.

```sh
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/pvz-portable -resdir=/path/to/folder/with/main.pak
```

- `-DPVZ_HACK_MENU=OFF` builds the game without the menu and the mods, like plain upstream.
- **macOS app bundle:** `scripts/make-macos-app.sh dist /path/to/gamedata` creates `dist/Plants vs. Zombies.app` with the data inside. Copy it to `/Applications` with `ditto`.

## Build and install on Android

`scripts/make-android-apk.sh` builds the APK without vcpkg. It downloads SDL2, libpng, libjpeg-turbo, libopenmpt and the ARCore AAR, compiles them with the NDK, builds `libmain.so` and packages everything with Gradle.

```sh
ANDROID_NDK=/path/to/ndk ANDROID_HOME=~/Library/Android/sdk scripts/make-android-apk.sh /tmp/pvz-android
adb install -r dist/pvz-portable-android-arm64-v8a.apk
```

- **Requirements:** Android SDK platform 36, an NDK, and JDK 17 or newer. `JOBS=4` speeds up the build; the default is 2.
- **Signing:** the APK is signed with the debug key, which is fine for sideloading.
- **Game data:** on first launch the app asks for a ZIP or folder with `main.pak` and `properties/`, as in upstream's [Android instructions](#special-instructions-for-android). If you have the mod sprites, put a `mods/` folder next to `main.pak`.
  - From a computer that already runs the game, `scripts/make-android-import-zips.py <out-folder>` builds `PvZ-datos.zip` (data and mods) and `PvZ-partida.zip` (your save).
- **Later changes:** long-press the app icon and choose **Manage Data** to re-import data or import a save.
- **ARCore is optional.** Phones without it can still install and play; only the AR checkbox stays off.

## The hack menu

| Platform | How to open and close it |
|---|---|
| Desktop | The key left of **1** (`` ` `` on a US keyboard, `º` on a Spanish one), or **fn+F1** |
| Android | **Tap with three fingers at once** (or the window's X) |

Settings are saved with the game and survive restarts.

- **Sol y plantas** (sun and plants): infinite sun, +1000 sun, no cooldown, plant price 0–100 %, plant anywhere ignoring rules, invincible plants.
- **Combate** (combat): plant damage 10–2000 %, one-hit kills, health of new zombies 10–1000 %, frozen zombies, kill all zombies, spawn any zombie.
- **Partida** (game): speed x1–x10, infinite money, win the level, choose a level.
- **Ver en AR** (view in AR): the three AR switches described below.

During a Versus match every cheat that would tilt the game is ignored; only game speed still applies.

## New plants, zombies and minigames

- **Plants:** open the seed chooser and press the **Mods** button next to "Let's Rock!". They unlock after you finish Adventure once. Outside Adventure they are always available.
  - Lanzafuego (fire peashooter), Hielonuez (ice-nut), Nuez tiradora (pea-nut), Girasol dorado (gold sunflower), Lanzaveneno (poison peashooter), Nuez espinosa (spiky wall-nut), Girasol sanador (healing sunflower), Cereza de hielo (ice cherry).
- **Zombies:** they appear in Adventure from world 3 and in the mod minigames.
  - Speedy, Ice, Ghost (only lobbed, splash or explosive damage hurts it), Giant Buckethead, Lane Hopper, Splitter (splits into two mini zombies), Medic (heals nearby zombies) and Kamikaze (explodes on death).
- **Minigames:** go to **Minigames** and press the **Mods** button at the bottom right. The modes are Invasion mod, Noche fantasma (ghost night), Gigantes (giants), Cinta mod (conveyor belt) and Caos en la piscina (pool chaos), with 30 waves each, plus the five Versus stages.

Stats and behavior of each one are in the tables of [MODS.md](MODS.md#plantas-y-zombis-nuevos-rama-featmod-content).

## Versus

Start one of the **Versus** modes (second row of the Mods page in Minigames). A match lasts 8 minutes in three phases, and each phase unlocks heavier zombies. There are no waves: the zombie player sends every zombie and pays in **brains**, which refill over time. The zombies win by eating **3 brains** (one waits at the end of each lane, behind the lawn mower). The plants win if the clock runs out first.

**Zombie player on the same computer:** the plant player uses the mouse; the zombie player uses the keyboard.

| Key | Action |
|---|---|
| W / S or ↑ / ↓ | Choose the lane |
| Q / E | Previous / next page of zombies |
| 1–9 | Send the zombie in that slot |
| T (Night only) | Toggle "from the grave": the zombie rises from the nearest grave |

**Zombie player on another device:**

1. While a Versus match is open, the game serves a page on **port 8090**. The address appears above the game's bottom bar, with a **6-letter code**.
2. On the other device, open `http://<computer-ip>:8090/?c=<code>` in any browser on the same network.
3. The page shows the game live with every zombie, its price and cooldown. Tap a lane and a zombie to send it.
4. **From another network:** if [Tailscale Funnel](https://tailscale.com/kb/1223/funnel) is enabled on your tailnet, the game also publishes `https://<machine>.<tailnet>.ts.net/?c=<code>` and turns it off when the match ends. `PVZ_VERSUS_FUNNEL=0` disables this.

When an Android phone hosts, share its local or Tailscale address instead; Funnel isn't available there. A Versus match can't be saved halfway.

## AR in the browser (game on the computer)

The game runs on your computer and the phone acts as an AR window. You see the lawn on your table through the camera and play by tapping it.

**You need:** an Android phone with Chrome and [Google Play Services for AR](https://play.google.com/store/apps/details?id=com.google.ar.core), which is what gives Chrome WebXR. The phone also needs internet, because the page loads three.js from jsDelivr. iPhones and desktop browsers have no WebXR AR, so they get the flat or 3D view only.

1. **Turn it on:** in the hack menu, under **Ver en AR**, tick **"Ver en AR desde el celular"** (or start the game with `PVZ_AR_VIEW=1`). The game serves the AR page on **port 8092** (`PVZ_AR_PORT`) with its own 6-letter code. The code is saved, so a bookmark on the phone keeps working. `PVZ_AR_CODE` sets it yourself.
2. **Connect the phone.** WebXR only runs on `https://` or `localhost`.
   - **By USB (simplest):** run `adb reverse tcp:8092 tcp:8092`, then open `http://localhost:8092/ar?c=<code>` in Chrome on the phone.
   - **Wireless, with Tailscale on both devices:** `tailscale serve --bg 8092` gives you `https://<computer>.<tailnet>.ts.net/ar?c=<code>`, with a valid certificate and reachable only inside your tailnet.
   - Over plain `http://<lan-ip>:8092` the page loads but can't enter AR; you can still play it flat.
3. **Place the board:** the page starts with the game flat (you can already play it by tapping). Press **Entrar en AR** (enter AR) and point at a table until a ring appears. A tap places the lawn there: 40 cm wide, with a wooden frame and a shadow.
4. **Ajustar** (adjust): drag with one finger to move it, use ▲▼ to tilt it from lying flat to 60°, and change its size. Close the panel to play: each tap is a click in the game (sun, seed packets, tiles, shovel).
5. **Plano / 3D:** the switch at the top picks the view.
   - **Plano** (flat) is the game's own picture on the board.
   - **3D** is a diorama: the lawn on a wooden base, the fence or sky standing at the back, the house folded up at the left edge, and every plant and zombie as a standing card with its own shadow.

Menus, dialogs and anything the diorama doesn't model are shown flat.

**Trying it without a phone:**

```sh
PVZ_AR_VIEW=1 PVZ_AR_PORT=8093 PVZ_AR_CODE=test ./build/pvz-portable -resdir=...
curl -o f.jpg "localhost:8093/frame.jpg?c=test"     # the first request answers 503 and starts the capture
curl "localhost:8093/click?c=test&a=tap&x=400&y=300"
```

## AR on the phone (native, ARCore)

The whole game runs on the Android phone, and the lawn stands on your table as a 3D diorama that the game draws with OpenGL ES. There is no computer, browser or network in the loop.

**You need:** the APK built with `scripts/make-android-apk.sh` (it links the ARCore C API), and a phone that supports [ARCore](https://developers.google.com/ar/devices).

1. **Turn it on:** tap with three fingers to open the hack menu. Under **Ver en AR**, tick **"Ver en AR en este cel"** (AR on this phone).
   - The first time, the app checks for Google Play Services for AR and opens the Play Store if it's missing. Then it asks for camera permission.
   - If either fails, a notice appears and the game keeps running in 2D.
2. **Outside a lawn** (menus, seed chooser, pause), the game shows in 2D as usual, with the camera in the side bars so you can see AR is ready. After 90 s without a lawn, the camera rests and comes back with the next level.
3. **On a lawn**, the camera fills the screen.
   - **"Busca la mesa…"** (look for the table): ARCore is still looking for a surface, and its tracking points show green.
   - Once found, surfaces are drawn dotted and a ring marks where the board would go. **One tap places the board.** It is anchored, so it stays on the table while you walk around.
4. **Ajustar** (adjust):
   - One finger drags the board.
   - Two fingers rotate and scale it.
   - **− / +** change the width (15–160 cm, 40 cm by default).
   - **Girar 90** turns it by 90°, and **De frente** turns the front edge toward you.
5. **Fijar** (lock) switches to playing. Each tap on the board is a click on that spot (sun, coin, seed packet, tile), and a finger held still for a moment drags. **Ajustar** goes back to moving the board; **Recolocar** (re-place) starts over.

When ARCore can't track (or for a quick test), `adb shell am start -n io.github.wszqkzqk.pvzportable/.PvZPortableActivity --es PVZ_AR_NATIVE 1 --es PVZ_AR_TEST_POSE 1` places the board right in front of the lens.

The ARCore header (`third_party/arcore`) and the AAR that the build downloads are not Apache-licensed. They fall under Google's **ARCore Additional Terms of Service**: see section 1 of `third_party/arcore/LICENSE`. The manifest marks ARCore as optional.

## 3D diorama in the desktop window

The same diorama the phone draws, in the game window. It's handy for developing the AR without a phone.

- Tick **"Diorama 3D en la ventana"** in the hack menu, or set `PVZ_AR_DIORAMA=1`.
- While a lawn is in play, drag with the mouse to orbit, use the wheel to zoom, and click to play.
- In menus and dialogs the window shows the normal 2D game.

## Modding sprites, sounds and texts

```sh
python3 scripts/extract-pak.py /path/to/main.pak "<game data dir>/gamedata-mod"
```

This unpacks all 3198 files of `main.pak`: `images/`, `sounds/`, `reanim/`, `particles/`, and `properties/LawnStrings.txt` (every text in the game). If a `gamedata-mod` folder exists, the game reads it instead of `main.pak`. Edit, restart, and you'll see your changes. Delete or rename the folder to go back.

- **Images and sounds** are read as they are.
- **Animations and particles** load their compiled cache first. After editing one, delete its `.compiled` file in `gamedata-mod/compiled/` and in `cache64/compiled/`.

## Testing without a mouse

The mods include a scripted test driver that plays itself and saves screenshots. Run it on a copy of your save folder. These are the main switches; MODS.md lists all of them.

| Variable | What it does |
|---|---|
| `PVZ_MOD_TEST=1` | Enters a level, plants the new plants and spawns three of each new zombie, logging their state every 2 s |
| `PVZ_MOD_TEST=mode1` … `mode10` | Plays a mod minigame (1–5) or a Versus stage (6–10) |
| `PVZ_MOD_TEST_VERSUS=1` | In Versus, plants a defense and plays the zombie side with the real keys |
| `PVZ_MOD_TEST_SHOTS=/dir` | The game saves its own screenshots there |
| `PVZ_TEST_HIDDEN_WINDOW=1` | Keeps the test window hidden |
| `SDL_AUDIODRIVER=dummy` | Runs without sound |

## Where the code is

Everything the mods add lives in `src/Mods/`. The hooks into the game are one or two lines each, inside `#ifdef PVZ_HACK_MENU`.

| File | What it does |
|---|---|
| `Mods.{h,cpp}`, `HackMenu.cpp` | Settings and the ImGui menu |
| `ModContent.{h,cpp}` | The new plants and zombies |
| `Versus.{h,cpp}`, `VersusNet.cpp` | Versus rules, and the HTTP server for the second player |
| `ARView.cpp`, `ARViewPage.h` | AR in the browser: MJPEG and scene streams, clicks, and the WebXR page |
| `ARScene.{h,cpp}` | Draws each object alone into an atlas and describes the scene |
| `ARDiorama.{h,cpp}`, `ARDioramaPreview.cpp` | The 3D diorama, and its preview in the window |
| `ARNative.cpp` | ARCore session, camera, anchors and touch on Android |
| `scripts/` | APK, macOS app, import ZIPs, `main.pak` extractor, mod art |

## Menu glossary

| In the game | Meaning |
|---|---|
| Sol y plantas · Combate · Partida | Sun & plants · Combat · Game |
| Ver en AR | View in AR |
| Ver en AR desde el celular | AR in the phone's browser (game on the computer) |
| Ver en AR en este cel | Native AR on this phone |
| Diorama 3D en la ventana | 3D diorama in the desktop window |
| Entrar en AR · Ajustar · Fijar · Recolocar | Enter AR · Adjust · Lock · Re-place |
| Girar 90 · De frente | Rotate 90° · Face me |
| Busca la mesa… | Look for the table… |
| Plano / 3D | Flat / 3D |
| Trucos en pausa en Versus | Cheats paused during Versus |

## Credits and licenses

- **[PvZ-Portable](https://github.com/wszqkzqk/PvZ-Portable)** by wszqkzqk and contributors: the whole engine and game. LGPL-3.0, kept here.
- **[Dear ImGui](https://github.com/ocornut/imgui)** v1.93.0 (MIT), vendored in `third_party/imgui`.
- **[ARCore SDK](https://github.com/google-ar/arcore-android-sdk)** v1.56.0: C header in `third_party/arcore`, and the AAR downloaded at build time (ARCore Additional Terms of Service).
- **[three.js](https://threejs.org/)** 0.160.0 (MIT), loaded by the AR page from jsDelivr.
- Plants vs. Zombies is a trademark of Electronic Arts. No game assets are included or distributed.

---

# Upstream PvZ-Portable README

Everything below is upstream's README, unchanged: build dependencies, platforms and save compatibility.

<div align="center">
  <img src="icon-readme.png" alt="PvZ-Portable" width="450">
</div>

[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/wszqkzqk/PvZ-Portable)

A **cross-platform** community-driven reimplementation of Plants vs. Zombies: Game of the Year Edition, aiming to bring the **100% authentic experience** of Plants vs. Zombies to every platform.

| 🌿 Authentic | 🎮 Portable | 🛠️ Open |
| :---: | :---: | :---: |
| Almost 100% gameplay recreation | Support for 32/64 bit systems<br>Run on Linux, Windows, macOS, Android, iOS, WebAssembly, Switch... | OpenGL ES 2.0 & SDL |

🌐 **No install wanted?** [Try directly in your browser!](https://wszqkzqk.github.io/PvZ-Portable/pvz-portable.html) (You still need your own game data files.)

**⚠️ Notice:**

* This repository does **NOT** contain any copyrighted game assets (such as images, music, or fonts) owned by PopCap Games or Electronic Arts. Users must provide their own `main.pak` and `properties/` folder from a **legally purchased copy** of Plants vs. Zombies: GOTY Edition.
* The codebase is a manual reimplementation derived from community research (such as [植物大战僵尸吧](https://tieba.baidu.com/f?ie=utf-8&kw=%E6%A4%8D%E7%89%A9%E5%A4%A7%E6%88%98%E5%83%B5%E5%B0%B8), [PVZ Wiki](https://wiki.pvz1.com/doku.php?id=home) and [PvZ Tools](https://pvz.tools/memory/)). It is written to utilize portable backends like SDL2 and OpenGL ES 2.0 (with desktop OpenGL 2.1 fallback). The author (wszqkzqk) **NEVER reverse engineered** the program; the author wrote it solely based on publicly available information and game testing. Also, code generated directly through reverse engineering will **not be accepted**.
* This project is intended solely for **educational purposes**, focusing on **cross-platform porting techniques**, engine modernization, and learning how classic game logic can be adapted to various hardware architectures.
* Non-Commercial: This project is not affiliated with, authorized, or endorsed by PopCap Games or Electronic Arts.
* Project icons and platform-specific logos are created by me (wszqkzqk) with the help of AI image generation tools and are not official assets of PopCap/EA.
* To play the game using this project you **MUST** have access to the original game files by purchasing it on [EA's official website](https://www.ea.com/games/plants-vs-zombies/plants-vs-zombies) or [Steam](https://store.steampowered.com/app/3590/Plants_vs_Zombies_GOTY_Edition/).

## Features

- [x] Render with SDL + OpenGL ES 2.0 (desktop OpenGL 2.1 fallback)
  - Also enable to **resize the window**, which was not possible in the original game
  - **Why OpenGL ES 2.0?** GLES 2.0 is the common subset of virtually all modern GPU APIs — every desktop OpenGL 2.1+ driver, mobile GPU, and game console inherently supports it. This means the game works **out of the box** everywhere without extra dependencies. [ANGLE](https://chromium.googlesource.com/angle/angle) can also be optionally used to translate calls to DirectX/Metal/Vulkan if needed.
- [x] Implement a cross-platform audio system based on [SDL Mixer X](https://github.com/WohlSoft/SDL-Mixer-X)
  - This project uses a fork of SDL Mixer X that adds compatibility with the MO3 format by using libopenmpt. This fork is located under SexyAppFramework/sound/SDL-Mixer-X
- [x] Save more memory by disabling caching for console platforms that have very limited RAM
- [x] **Compatible** with original PvZ GOTY Edition's ***global user data*** (profile info, adventure progress, coins, Zen Garden, etc., stored in `user*.dat`)
  - [x] Fix 2038 year problem while keeping compatibility
- [x] **Portable mid-level save game** format `.v4` support (share **mid-level saves** between Windows, Linux, macOS, Android, Switch, etc.)
  - [x] Support export/import of `.v4` save files to/from human-readable YAML format for easy editing
- [x] Implement with `std::thread` for cross-platform threading support
- [x] Implement with `std::filesystem` for cross-platform file system support
- [x] Unified use of UTF-8 encoding within the program
- [x] **Multilingual Support**: Supports game resource data from official GOTY editions in various languages, including **Chinese, German, Spanish, French, and Italian**.
- [x] 32 and 64-bit builds support
- [x] Different CPU architectures support (i686, x86_64, aarch64, riscv64, loongarch64, etc.)
- [x] Unicode path support on all platforms
- [x] Different endianness support (little-endian and big-endian)
  - [x] Save data compatibility across endianness
  - Theoretically supports big-endian platforms, but untested due to lack of hardware
- [x] Unlockable **Hidden Limbo Page** with additional levels in the original game
  - To unlock it, open the Mini-Games / Puzzle / Survival selection screen and tap any blank area **5 times in rapid succession**
- [x] **Record and deterministically replay** gameplay sessions — see [Recording and Playing Back Gameplay](#recording-and-playing-back-gameplay)

This project supports the following platforms (including but not limited to):

| Platform        | Data path                    | Status                                                                                 |
|-----------------|------------------------------|----------------------------------------------------------------------------------------|
| Linux           | Executable dir (resources); per-user app-data for writable files | Works                                                                                  |
| Windows         | Executable dir (resources); per-user app-data for writable files | Works                                                                                  |
| macOS           | Executable dir (resources); per-user app-data for writable files | Works                                                                                  |
| BSD Family      | Executable dir (resources); per-user app-data for writable files | Works (verified at least on FreeBSD)                                                                               |
| Haiku           | Executable dir (resources); per-user app-data for writable files | Partially works: no music                                                              |
| Android         | `Android/data/io.github.wszqkzqk.pvzportable/files/` | Works                                                                                  |
| iOS / iPadOS    | App Documents directory (Files app) | Works (sideload only; unsigned IPA)                                                    |
| Web (WASM)      | Browser IndexedDB (saves); resources uploaded at runtime    | Works (requires a HTTP server) |
| Nintendo Switch | sdmc:/switch/PvZPortable | Works on real hardware. Kenji-NX crashes on boot.                           |

To play the game, you need the game data from PvZ GOTY. Place `main.pak` and the `properties/` folder next to the `pvz-portable` executable (the game will search for resources relative to the executable's directory). You can also use extracted data instead of `main.pak` if you prefer.

Note about writable data and caches:

- The game will read resources (like `main.pak` and `properties/`) from the executable directory by default, so you can launch the binary from any working directory and it will still find them.
- Per-user writable files (settings, savegames, compiled caches, screenshots) are stored in the **OS-recommended application data path**. With the current build these are under `io.github.wszqkzqk/PvZPortable` and include subfolders such as:
  - `userdata/` — Player save files.
  - `cache64/` if you use the 64-bit version or `cache32/` if you use the 32-bit version — Compiled binary caches (reanimation / compiled definitions). These caches are **local startup** artifacts (**native layout**), not portable files; when cache/schema checks fail, the game transparently recompiles from source data.
  - `registry.regemu` — Settings/registry emulation.

Examples:

- Linux: `~/.local/share/io.github.wszqkzqk/PvZPortable/`
- Windows: `%APPDATA%\io.github.wszqkzqk\PvZPortable\`
- macOS: `~/Library/Application Support/io.github.wszqkzqk/PvZPortable/`

You can customize these paths via command-line parameters:
- `-resdir <path>` / `-resdir=<path>`: Set the **resource directory** (where `main.pak` and `properties/` are located). This only affects where the game looks for resources, not where it saves data.
- `-savedir <path>` / `-savedir=<path>`: Set the **save data directory** (where settings, savegames, caches, and screenshots are stored). This overrides the default OS-recommended application data path.

### Special Instructions for Android

Download the APK from the [Releases](https://github.com/wszqkzqk/PvZ-Portable/releases) page or build it yourself. Because this project **does not include** any game assets, you will need to **import the game resources** from a **legally purchased copy** of Plants vs. Zombies: GOTY Edition.

#### First Launch

1. **Install the APK** (you may need to enable "Install from unknown sources").
2. **Launch the app** — since no game resources are found, a **resource import screen** will appear automatically.
3. **Import game resources** by selecting either:
   - A **ZIP file** containing `main.pak` and `properties/` (either at the ZIP root or inside one wrapper directory, e.g. `PvZ/main.pak`).
   - A **folder** that directly contains `main.pak` and `properties/`, or whose immediate subfolder does.
4. Press **Start Game** once the import succeeds.

#### Managing Data Later

Long-press the app icon on your launcher to access the **Manage Data** shortcut, which opens the resource management screen where you can re-import resources, export saves, or import saves.

#### Notes

- Requires Android 7.0+. The prebuilt APK is arm64-v8a only, but **you can build for other architectures** if needed.
- All data is stored in `Android/data/io.github.wszqkzqk.pvzportable/files/`. No extra storage permissions are needed — the app uses the **Storage Access Framework (SAF)** for all imports and exports.
- Save data is interchangeable with desktop versions. See the [save data section](#save-data-compatibility-user-data-and-mid-level-saves) chapter for details.
- The Android port is part of this project's **cross-platform porting research**. It preserves the original game's 4:3 aspect ratio and mouse-based input model — **no touch-screen-specific UI optimizations have been made**. SDL2 automatically maps touch events to mouse input, so the game is playable but not designed for mobile ergonomics.

### Special Instructions for iOS / iPadOS

Download the unsigned IPA from the [Releases](https://github.com/wszqkzqk/PvZ-Portable/releases) page or build it yourself with `ios/build-ios.sh`. The IPA must be sideloaded — common methods include [AltStore](https://altstore.io/), [TrollStore](https://github.com/opa334/TrollStore), or deploying directly from Xcode with a free Apple ID.

#### Importing Game Resources

The app's Documents folder is exposed via iTunes/Finder file sharing and the iOS Files app (`UIFileSharingEnabled`). Place `main.pak` and the `properties/` folder directly into the app's Documents directory (shown as "PvZ Portable" in the Files app).

#### Notes

- Requires iOS 16.4+ (arm64).
- Free Apple ID signatures expire after 7 days; TrollStore installs are permanent.
- Same touch-to-mouse mapping and aspect ratio behavior as the Android port.

### Play in Your Browser (WebAssembly)

**[▶ Play Online](https://wszqkzqk.github.io/PvZ-Portable/pvz-portable.html)** — open the link, load your legally purchased game resources either from a ZIP package or from `main.pak` plus the `properties/` folder, then click **Start Game**. All files stay in your browser locally and are **never uploaded to any server** (the hosting site is purely static). Save data is stored in your browser's IndexedDB and can be imported from ZIP or folder, or exported as a ZIP via the on-screen buttons.

You can also [download the WASM build](https://github.com/wszqkzqk/PvZ-Portable/releases) and self-host it. Note that the HTML file must be served over HTTP (e.g. `python3 -m http.server`) — opening it directly as a local file will not work due to browser security restrictions.

## Game Version Compatibility

This project is designed and tested against Plants vs. Zombies **GOTY Edition 1.2.0.1073** EN (the standalone PopCap release). **Non-English GOTY editions** (1.2.0.1093 DE/ES/FR/IT or 1.1.0.1056 ZH based on 1.2.0.1073) and the **Steam GOTY Edition 1.2.0.1096** are also fully playable — all game mechanics work correctly. The only differences are minor cosmetic UI text issues caused by renamed string keys across versions, and these can be **easily fixed** by the user via a custom `properties/pvz-portable.xml`.

For **1.2.0.1096** and **non-English GOTY editions**, generate compatibility and translation overrides with:

```bash
python3 scripts/make-compat-overrides.py "/path/to/main.pak"
```

The script writes `pvz-portable.xml` to the `properties/` directory beside `main.pak`. Use `-o` to choose a different output path or `--force` to overwrite an existing file.

**Recommendation:** use the **1.2.0.1073 EN** asset package for the best **out-of-box** experience.

<details>
<summary>Known cosmetic differences with non-1.2.0.1073 EN assets (click to expand)</summary>

| Issue (Non-1.2.0.1073 EN only) | Cause |
| :---: | :---: |
| **Almanac blue description text missing** | In 1.2.0.1096, the plain-text introductory paragraph was split out from `[XXX_DESCRIPTION]` into a new `[XXX_DESCRIPTION_HEADER]` key. The engine only reads `[XXX_DESCRIPTION]`, so the header text is never displayed. |
| **"Restart" button label missing** | The key `[RESTART_LEVEL]` (used for the button label) was renamed to `[RESTART_LEVEL_BUTTON]` in 1.2.0.1096. |
| **Unencountered zombie shows `???`** instead of `(not encountered yet)` | The string `[NOT_ENCOUNTERED_YET]` changed its value to `???` in 1.2.0.1096; the old text was moved to a new key `[NOT_ENCOUNTERED_YET_DESCRIPTION]`. |
| **Crazy Dave's plant sell price shows 1/10 of the correct value** | In 1.2.0.1073, the string template `[CRAZY_DAVE_1700]` contains a trailing `0` after `{SELL_PRICE}` (i.e. `${SELL_PRICE}0`) because the engine passes the price divided by 10. In 1.2.0.1096 the trailing `0` was removed, so the displayed price becomes 1/10 of the intended amount. |
</details>

## Dependencies

Before building on PC, ensure you have the necessary dependencies installed:

- **Build Tools**: `CMake`, `Ninja`, A C/C++ compiler (e.g., `gcc`, `clang`, `MSVC`) supporting **C++20** (Also need a standard library implementation like `libstdc++`, `libc++` or MSVC STL that supports C++20)
- **Graphics**: `OpenGL ES 2.0` or `OpenGL 2.1+` (auto-detected at runtime via SDL2)
- **Audio**: `libopenmpt`
- **Image**: `libpng`, `libjpeg-turbo`
- **Windowing/Input**: `SDL2`

### Arch Linux

You can install the required dependencies using the following command:

```bash
sudo pacman -S --needed base-devel cmake libjpeg-turbo libopenmpt libpng ninja sdl2-compat
```

### Debian/Ubuntu

You can install the required dependencies using the following command:

```bash
sudo apt install cmake ninja-build libjpeg-dev libopenmpt-dev libpng-dev libsdl2-dev
```

### Windows (MSYS2 UCRT64)

You can install the required dependencies using the following command:

```bash
pacman -S --needed base-devel mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libjpeg-turbo mingw-w64-ucrt-x86_64-libopenmpt mingw-w64-ucrt-x86_64-libpng mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2
```

### macOS (Homebrew)

You can install the required dependencies using [Homebrew](https://brew.sh/) with the following command:

```bash
brew install cmake dylibbundler jpeg-turbo libopenmpt libpng ninja sdl2
```

## Build Instructions

Run the following commands (assuming you have CMake and other dependencies installed) where the `CMakeLists.txt` file is located:

```bash
cmake -G Ninja -B build
```

```bash
cmake --build build
```

### Performance Optimization

It is recommended to use the **Release** build type for the best performance, as it usually implies compiler optimizations:

```bash
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
```

For advanced users, you can also specify `CFLAGS` and `CXXFLAGS` to enable architecture-specific optimizations (e.g. `-march=native` to fully utilize your CPU's instruction set):

```bash
cmake -G Ninja -B build -DCMAKE_C_FLAGS="-march=native" -DCMAKE_CXX_FLAGS="-march=native" -DCMAKE_BUILD_TYPE=Release
```

### Configuration Options

You can customize the game features by adding options to the first `cmake` command:

| Option | Default | Description |
| :--- | :--- | :--- |
| `PVZ_DEBUG` | `OFF`<br>(`ON` if `CMAKE_BUILD_TYPE` is `Debug`) | Enable **cheat keys**, debug displays and other debug features. |
| `DO_FIX_BUGS` | `OFF` | Apply community fixes for "bugs" of official 1.2.0.1073 GOTY Edition.[^1] However, these "bugs" are usually **considered "features"** by many players. |
| `CONSOLE` | `OFF`<br>(`ON` if `CMAKE_BUILD_TYPE` is `Debug`) | Show a console window (Windows only). |
| `BUILD_STATIC` | `OFF` | Link dependencies statically (Windows with MinGW-based toolchains, glibc-based Linux). Use a vcpkg `-static` triplet for MSVC. |

[^1]: Current `DO_FIX_BUGS` includes the following fixes:
    - Fix bungee zombie duplicate sun/item drop in I, Zombie mode.
    - Make mind-controlled Gargantuars smash enemy zombies instead of plants.
    - Make mind-controlled Gargantuars throw mind-controlled Imps (with scale, health, and direction fixes).
    - Make mind-controlled Gargantuars can smash vases in Scary Potter mode.
    - Make mind-controlled Pea/Gatling Head zombies shoot forward instead of backward.
    - Make mind-controlled Jalapeno/Squash zombies damage enemy zombies instead of plants.
    - Coordinate fixes for mind-controlled Squash zombies tracking and smashing enemy zombies.
    - Make mind-controlled Jalapeno zombies correctly clear Dr. Zomboss' skills (Iceball/Fireball) and ladder logic.
    - Sync Dancer Zombie animations (fixes "Maid" displacement bug).
    - Fix visual glitch of Ladder Zombie's arm recovery.
    - Fix Dr. Zomboss' attack (RV, Fireball/Iceball) and summon range coverage for 6-lane (Pool) levels.

Example: Manually enable `PVZ_DEBUG` in **Release build** so that you can use **cheat keys** while having optimized performance:

```bash
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release -DPVZ_DEBUG=ON
```

If running these commands does not create a successful build please create an issue and detail your problem.

## Save data compatibility (user data and mid-level saves)

PvZ-Portable uses two distinct types of save data:

1.  **Global User Data** (`users.dat`, `user1.dat`, etc.):
    *   Stores profile info, adventure progress, coins, Zen Garden, etc.
    *   **Fully compatible** with the original PC game format.
    *   Already portable by design (uses explicit serialization).

2.  **Mid-Level Save States** (`game1_0.v4`, etc. legacy `game1_0.dat`, etc.):
    *   Stores the exact state of a level when "Save and Exit" is used (zombies, projectiles, plants, etc.).
    *   The game now writes **only** `*.v4` files by default:
        *   `*.v4` files: **Portable format**. Sharing these files to transfer progress between different platforms is fully **supported**.
        *   `*.dat` files: **Legacy dump** from old versions. Contains raw memory dumps. **Do not share this file** across platforms as it will cause crashes due to architecture differences.
    *   When loading, the game **prefers** `.v4`; `*.dat` is fallback-only for migration compatibility.
    *   If a save is loaded from legacy `*.dat`, the game automatically re-saves it to `*.v4` and removes the legacy `*.dat` after successful migration.

### Why a new mid-level save format?

The original game's mid-level save format (`.dat`) effectively dumps raw memory structures to disk. This is fast but fragile, as it relies on specific memory layouts that break across:

*   **Different CPU Architectures**: x86, ARM, RISC-V, LoongArch (alignment).
*   **32-bit vs 64-bit builds**: Pointer sizes and struct layouts differ.
*   **Different Compilers**: MSVC vs GCC/Clang (padding, enum size, struct packing).

As a result, legacy saves are generally not guaranteed to load across those variants. To solve this, **Format v4** serializes game objects field-by-field using Type-Length-Value (TLV) tags. This ensures that a level saved on a x86_64 Linux machine can be loaded on a LoongArch or ARM (Switch) machine without crashing.

### Developer Guidelines for Format v4

- Add new fields as new TLV IDs; do not reuse IDs.
- Keep defaults for missing fields when loading older saves.
- Avoid raw struct dumps for data that may change layout; prefer explicit per-field sync with fixed-width primitives.

### Save Editing & Conversion Tool

There's a Python script `scripts/pvzp-v4-converter.py` to inspect and modify `.v4` save files. 

**Features:**
*   **Info**: View basic save stats (Wave, Sun, Zombies, etc.).
*   **Export to YAML**: Convert binary `.v4` files to human-readable YAML to view/edit gameplay-relevant data.
*   **Import from YAML**: Convert modified YAML back to `.v4` format with correct checksums.

**Usage:**
```bash
# Modify these paths as needed
# View basic info
python3 scripts/pvzp-v4-converter.py info ~/.local/io.github.wszqkzqk/PvZPortable/userdata/game1_13.v4

# Export to YAML for editing
python3 scripts/pvzp-v4-converter.py export ~/.local/io.github.wszqkzqk/PvZPortable/userdata/game1_13.v4 level.yaml

# Import back to savegame
# **BACKUP** your original .v4 file fist!
mv ~/.local/io.github.wszqkzqk/PvZPortable/userdata/game1_13.v4 ~/.local/io.github.wszqkzqk/PvZPortable/userdata/game1_13.v4.bak
python3 scripts/pvzp-v4-converter.py import level.yaml ~/.local/io.github.wszqkzqk/PvZPortable/userdata/game1_13.v4
```

## Recording and Playing Back Gameplay

The game can record a gameplay session into a `.dmo` demo file and replay it deterministically later. Automatically named demo files are written to the current working directory:

- `-record [<path>]` / `-record=[<path>]` — record to a file named after the current local time, in the form `pvzp-YYYYMMDD-HHMMSS[-N].dmo`, or to an explicit `<path>` if given; an explicit target disables automatic retention.
- `-play [<path>]` / `-play=[<path>]` — play back the first matching `pvzp-*.dmo` recording in descending timestamp/name order, or an explicit `<path>` if given.
- `-playnum <N>` / `-playnum=<N>` — play back the N-th recording in descending timestamp/name order; `1` selects the first.
- `-recnum <N>` / `-recnum=<N>` — record and keep the first `N` files matching the automatic timestamp pattern in descending timestamp/name order; other `pvzp-*.dmo` files are left untouched.

This feature is primarily designed for debugging and regression testing.

## Contributing

When contributing please follow the following guides:

<details><summary>SexyAppFramework coding philosophy</summary>

### From the SexyAppFramework docs:

<br>
The framework differs from many other APIs in that some class properties are not wrapped in accessor methods, but rather are made to be accessed directly through public member data.   The window caption of your application, for example, is set by assigning a value to the std::string mTitle in the application object before the application’s window is created.  We felt that in many cases this reduced the code required to implement a class.  Also of note is the prefix notation used on variables: “m” denotes a class member, “the” denotes a parameter passed to a method or function, and “a” denotes a local variable.
</br>
</details>

## License

**Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>**

This project is licensed under the terms of the [**GNU Lesser General Public License v3.0**](https://www.gnu.org/licenses/lgpl-3.0.html) or later (LGPL-3.0-or-later).

* The repository includes complete license texts at the root:
  * `LICENSE` — LGPL-3.0 text
  * `COPYING` — GPL-3.0 text, referenced by LGPL-3.0
* The code is provided "as is", **WITHOUT WARRANTY** of any kind.
* The **original game IP (Plants vs. Zombies) belongs to PopCap/EA**. This license applies **only to the code implementation** in this repository.
* This project does **NOT** include any copyrighted assets from the original game.

### PopCap Games Framework Acknowledgment

The `SexyAppFramework` directory may contain code originally based on the **PopCap Games Framework**. This code is subject to the permissive **[PopCap Games Framework License](src/SexyAppFramework/LICENSE)**. To the extent that original code remains, the following acknowledgment applies:

> "This product includes portions of the PopCap Games Framework, © 2005-2009 PopCap Games, Inc.  All rights reserved. (http://popcapframework.sourceforge.net/)."

Note that this code has been **heavily refactored**, **optimized** and **modernized** by the community over time under the **LGPL-3.0-or-later** license.

## Thanks

- **PopCap Games**: For creating the amazing game and releasing their framework to the public with a permissive license.
- **The SDL Team**: For the amazing cross-platform development library that powers this port.
- **The OpenMPT Team**: For libopenmpt, enabling high-quality MO3 music playback.
- All the contributors who have worked or are actively working in this amazing project, especially [@Headshotnoby](https://www.github.com/headshot2017) and [@Patoke](https://www.github.com/Patoke) for their groundwork.
