<p align="center">
  <img src="https://raw.githubusercontent.com/skitzellez/xbox-game-vault/main/GameVault/art/logo-banner.png" alt="Game Vault" width="640">
</p>

<h1 align="center">XBSX2 for Game Vault</h1>

<p align="center">
  <b>The XBSX2 PlayStation 2 emulator for Xbox Series X|S in Dev Mode, built to work hand in hand with
  <a href="https://github.com/skitzellez/xbox-game-vault">Game Vault</a>.</b><br>
  Any USB stick, your memory cards on it, and PS2 discs straight from the console's drive.
</p>

<p align="center">
  <a href="https://github.com/skitzellez/XBSX2-GameVault/releases"><img alt="Latest release" src="https://img.shields.io/github/v/release/skitzellez/XBSX2-GameVault?include_prereleases&label=release&color=107C10"></a>
  <img alt="Platform: Xbox Dev Mode" src="https://img.shields.io/badge/platform-Xbox%20Dev%20Mode-107C10?logo=xbox&logoColor=white">
  <img alt="License: GPL-3.0" src="https://img.shields.io/badge/license-GPL--3.0-blue">
  <a href="https://www.patreon.com/Skitzelles"><img alt="Patreon" src="https://img.shields.io/badge/Patreon-Skitzelles-F96854?logo=patreon&logoColor=white"></a>
</p>

---

> [!IMPORTANT]
> This is an **unofficial build**. It is not made or supported by XboxEmulationHub or the PCSX2 project, so please
> report problems with it **here**, not to them.

> [!NOTE]
> A PlayStation 2 BIOS is needed and is **not included**. Dump it from your own console and put it in the
> `Game Vault\bios` folder on your USB stick.

## ✨ What's different from the original

| | |
|---|---|
| 💽 **Any USB stick** | FAT32, exFAT or NTFS, exactly as it is. No formatting, no permission commands on a PC. |
| 💾 **Memory cards on your stick** | Started from Game Vault, it keeps BIOS files, memory cards, save states and its log in the `Game Vault` folder on the stick, and moves over what it had on the console the first time. |
| 📀 **Discs from the drive** | PlayStation 2 discs play straight from the Series X disc drive. |
| 🤝 **Lives next to the original** | Its own name and link, so the original XBSX2 app and its settings are left alone. |

## 🚀 Getting started

The easy way is the all-in-one setup from **[Game Vault](https://github.com/skitzellez/xbox-game-vault#installation)**.
It installs this build, sets up the controller, and copies over your settings, BIOS files and memory cards from
the original XBSX2 app.

By hand: download the `.msixbundle` from [Releases](https://github.com/skitzellez/XBSX2-GameVault/releases),
install it through the Xbox **Device Portal**, then in **Dev Home** set its **App type** to **Game**.

## 🛠️ Source

Everything is on the **`gamevault`** branch, on top of [XboxEmulationHub/XBSX2](https://github.com/XboxEmulationHub/XBSX2).
The main changes: `pcsx2-uwp/src/UWPFileRedirect.cpp` (any USB stick), `pcsx2/CDVD/Windows/IOCtlSrcXbox.inl`
(the disc drive) and `pcsx2-uwp/src/main.cpp` (the Game Vault folder and launch link). Build with `build_gv.bat`
(Visual Studio 2022 Build Tools).

## 📜 Credits and licence

- **[PCSX2](https://pcsx2.net)** and its contributors: all the emulation is theirs.
- **[XBSX2](https://github.com/XboxEmulationHub/XBSX2)** by SternXD / XboxEmulationHub, the Xbox build this is based on.
- Changes for Game Vault by **[Skitzelles](https://github.com/skitzellez)**, written with an AI assistant (Claude).

GPL-3.0, like PCSX2. XBSX2's own README: [README-XBSX2.md](README-XBSX2.md).
