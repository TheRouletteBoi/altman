<div align="center">
    <img src="src/assets/images/256x256.png"
            alt="AltMan - Roblox Account Manager"
            width="256"
            height="256"
            style="display: block; margin: 0 auto" />
<h1>AltMan</h1>
<h3>Roblox Account Manager & Multi Instance Launcher</h3>
<p><strong>AltMan</strong> is a cross platform Roblox account manager focused on stability, transparency, and local user control. It enables managing multiple Roblox accounts, launching multiple instances, and switching accounts without repeated logins.</p>

<p>
    <img src="https://img.shields.io/badge/Roblox-Account%20Manager-red" alt="Roblox Account Manager"/>
    <img src="https://img.shields.io/badge/Multi%20Roblox-Supported-green" alt="Multi Roblox"/>
    <img src="https://img.shields.io/badge/Platform-Windows%20%7C%20macOS-blue" alt="Platform"/>
</p>
</div>

---

## Project Goals

* Provide a stable and predictable Roblox account manager
* Keep all data local, encrypted, and under user control
* Support multi instance workflows without background services
* Maintain a clear, contributor friendly codebase

---

## What is AltMan?

AltMan allows you to manage multiple Roblox accounts within a single application. It simplifies switching between accounts, launching multiple Roblox clients simultaneously, and joining servers through multiple supported methods.

The project is designed to be straightforward, auditable, and extensible rather than opaque or over automated.

---

## Features

* **Multi Account Management** – Add, organize, and securely store cookies for multiple Roblox accounts
* **Multi Instance Support** – Launch multiple Roblox instances on the same machine
* **Quick Join** – Join games via JobID, PlaceID, or Private Server Links
* **Friends Integration** – View and manage friends per account
* **Friend Requests** – Send friend requests directly from the interface
* **Server Browser** – Browse active Roblox servers
* **Private Servers**

  * View joinable private servers
  * Manage private servers you own
* **Advanced Filtering** – Filter servers by ping or player count
* **Game Discovery** – Search Roblox games by title or keyword
* **Log Parser** – Convert Roblox logs into a human readable format
* **Multiple Client Support (macOS only)** – Assign different Roblox clients per account:

  * Default
  * Hydrogen
  * Delta
  * MacSploit

---

## Preview
<div align="center">
  <img src=".github/previews/screenshot.png" alt="AltMan Preview" />
  <br/>
  <video src="https://github.com/user-attachments/assets/170bb0f1-c5e9-4fa3-8ffd-69185b593448" controls autoplay loop muted width="600"></video>
</div>

---

## Usage Guide

### Adding Accounts

1. Launch **AltMan**
2. Navigate to `Accounts`
3. Click `Add Account` → `Add Via Cookie` or `Add Via Login`
4. Paste your cookie and confirm

### Joining Games

* **By JobID** – Join a specific server instance
* **By PlaceID** – Join a game by place ID
* **By Username** – Join a user's session (if allowed)
* **By Private Server Link** – Join using a private server share link

> You can also join games via the **Servers** or **Games** tabs

### Managing Friends

1. Select an account
2. Open the **Friends** tab
3. Send or manage friend requests

---

## Contributing

This project is actively maintained and welcomes contributions.

### How to contribute

* Fork the repository
* Create a feature branch from `main`
* Keep changes focused and well scoped
* Follow existing project structure and code style
* Open a pull request explaining **what** changed and **why**

### Before submitting

* Open an issue first for large or architectural changes
* Ensure the project builds successfully on your platform
* Avoid unrelated refactors in the same pull request

Issues, bug reports, and feature discussions are welcome.

---

## Requirements

* Windows 10 or 11 (tested on Windows 11 24H2)
* macOS 13.3+
* Active internet connection

---

## Building from Source

### Prerequisites

* Visual Studio 2022 (or Build Tools) with **Desktop development with C++**
* CMake ≥ 3.25
* [vcpkg](https://github.com/microsoft/vcpkg) (set `VCPKG_ROOT`)
* Git

### Clone the repository

```bat
git clone https://github.com/TheRouletteBoi/altman.git
cd altman
```

### Bootstrap vcpkg (if needed)

```bat
git clone https://github.com/microsoft/vcpkg.git %USERPROFILE%\vcpkg
%USERPROFILE%\vcpkg\bootstrap-vcpkg.bat
```

### Install dependencies

```bat
%USERPROFILE%\vcpkg\vcpkg.exe install
```

### Build (Windows)

```bat
mkdir build
cmake -B build -S . ^
  -DCMAKE_TOOLCHAIN_FILE=%USERPROFILE%\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Build (macOS)

```bash
mkdir build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target AltMan -j 8
```

---

## Security & Transparency

AltMan is designed with a strong emphasis on transparency and user awareness.

* Account cookies are stored **locally** and **encrypted** on your machine
* No account data is transmitted to third party servers

---

## License

This project is licensed under the **MIT License**. See the `LICENSE` file for details.

---

## Important risk notice

Using account managers, multi instance tools, or automation related features may:

* Violate Roblox’s Terms of Service depending on usage
* Trigger automated moderation or anti cheat systems
* Result in warnings, temporary suspensions, or permanent bans

AltMan does **not** attempt to bypass Roblox security systems. However, any tool that manages cookies or launches multiple clients carries inherent risk.

**You are solely responsible for how you use this app.**

Never share your cookies with anyone.

---

<div align="center">
<sub>AltMan • Roblox Account Manager • Multi Instance Launcher</sub>
</div>
