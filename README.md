### Wait, this isn't LibreSplit, what is this fork??

_Lot of things are moving around at the present, so please have patience for mismatched branding!_

This project is a personal spin on the auto-splitting speedrun timer for Linux. Originally my local contributing fork, a couple creative differences inspired me to embrace the full version of my vision for the timer. In short, the hope is to create an overall improved speedrunning timer, less so a recreation of LiveSplit.

### Some key features that (will be) supported!

- [ ] Shareable "game" definitions: Package a consolidated spiltter, category-specific splits, optional subsplits, and easy options for runners in a single lua file.
- [ ] Objective splits: In addition to normal sub-splits, sometimes objectives exist in runs where the order is not necessarily known (Minecraft's nether: bastion / fortress.) Now those objective times can be tracked!
- [ ] KDL data files: Game defintion is now separated from run data, stored in KDL format rather than JSON. Add other runs as pace comparisons!
- [ ] Plugin support: Lightweight base timer program, functionality can be added via C code or Lua scripts. Even the GUI is optional!

### Splitters / Resources

_I am tentatively planning on keeping the auto-splitter backwards compatible with LibreSplit auto-splitters. This would allow a splitter to be written once for either, or otherwise more easily adopted._

> For a the public repository of splits, auto splitters and themes. They are located [here](https://github.com/LibreSplit/LibreSplit-resources)

---

### _Old content pending final revision._

# Katana

Katana is a scriptable splitting speedrun timer with roots based on [urn](https://github.com/3snowp7im/urn) and [LibreSplit](https://github.com/libresplit/libresplit).

## Features

- **Split Tracking and Timing:** Accurately track and time your speedruns with ease.
- **Auto Splitter Support:** Utilize Lua-based auto splitters to automate split timing based on in-game events.
- **Customizable Themes:** Customize your timer's appearance by creating and applying custom themes.
- **Flexible Configuration:** Configure keybindings and various settings to suit your preferences.
- **Icon support for splits.**
- **Always on Top support.**
- **Support for in-game time.**

---

## Building
### Dependencies

|  **Dependency** |                  **Notes** | **[ Arch ]** |    **[ Debian ]** |  **[ Fedora ]** | **[ Gentoo ]** |
|----------------:|---------------------------:|-------------:|------------------:|----------------:|---------------:|
|   \<toolchain\> |     Clang or GCC supported |            - |   build-essential |             gcc |          (WIP) |
|             git |                            |          git |                 - |             git |                |
|           meson |                            |        meson |             meson |           meson |                |
|        binutils |                            |            - |                 - |        binutils |                |
|         libgtk4 |                            |         gtk4 |      libgtk-4-dev |      gtk4-devel |                |
|         jansson |                            |      jansson |    libjansson-dev |   jansson-devel |                |
|          libx11 |                            |            - |                 - |    libX11-devel |                |
|          luajit |                            |       luajit | libluajit-5.1-dev |    luajit-devel |                |
|         openssl |             for `md5sum()` |      openssl |        libssl-dev |   openssl-devel |                |
| glib-networking | web split icons (optional) |              |                   | glib-networking |                |
|            gvfs | web split icons (optional) |              |                   |            gvfs |                |

### Cloning

```sh
git clone https://github.com/Drayux/Katana
```

### Build (Release)

> Ensure working directory is set to the clone location, usually `cd Katana` after running the clone command.

```sh
meson setup build -Dbuildtype=release
meson compile -C build
```

The program can be ran without installation by calling `./build/timer/katana` from the build directory.
> **CTL:** `./build/ctl/katana-ctl`

#### Options (TODO -- probably better for developer guide)

- `LOG_LEVEL` 0, 1, 2, 3, 4 (DEBUG, INFO, WARN, ERROR, FATAL)

#### Targets (TODO -- probably better for developer guide)

Build just a specific target with:
`meson compile -C build libkatana` # static shared library
`meson compile -C build katana` # main timer program
`meson compile -C build katana-plugins` # (TODO) need to determine how to select these
`meson compile -C build katana-ctl` # extra timer control program (wayland workaround)

### Installation

```sh
meson install -C build
```

This will place the executable into `/usr/local/bin/` which is in the PATH of most distributions by default. Run it with `katana` or `/usr/local/bin/katana` in the command line.

---

## Usage
### Default Keybinds

The timer is controlled with the following keys
(note that their action **depends on the state of the timer**):

| Key                  | Timer is Stopped | Timer is running |
| -------------------- | ---------------- | ---------------- |
| <kbd>Space</kbd>     | Start timer      | Split            |
| <kbd>Backspace</kbd> | Reset timer      | Stop timer       |
| <kbd>Delete</kbd>    | Cancel           | -                |

Cancel will **reset the timer** and **decrement the attempt counter**. A run that is reset before the start delay is automatically cancelled.

If you forget to split, or accidentally split twice, you can manually change the current split:

| Key                  | Action     |
| -------------------- | ---------- |
| <kbd>Page Up</kbd>   | Unsplit    |
| <kbd>Page Down</kbd> | Skip Split |

If you want to change the default settings or keybinds, you can check the [Settings and Keybinds documentation](docs/settings-keybinds.md)

### Configuration

When you start **LibreSplit** for the first time, it will create the `libresplit` directory in your config directory (it will usually be `~/.config/libresplit`). Such directory will contain:

- Splits.
- Auto Splitters.
- Themes.

All 3 directories will start empty, so you may want to download the [resource repository](https://github.com/LibreSplit/LibreSplit-resources/) first and clone it in `~/.config/libresplit/` before starting LibreSplit for the first time.

A file dialog will then appear, asking you to select a Split JSON file (see [Split files](#split-files)).

Initially the window is undecorated. You can toggle window decorations by pressing the `Right Control` key.

### Always on Top

LibreSplit supports Always on Top in X11 sessions via an application setting. This can be toggled on or off in the right click context menu. This setting only works on X11 sessions.

On Wayland, always on top can usually be configured in your compositor. If you notice that the "Always on Top" setting does not appear, you are on Wayland. An example on KDE specifically for configuring your compositor to always keep LibreSplit on top is provided below.

To configure LibreSplit as always on top on KDE:
- Open: System Settings -> Apps & Windows -> Window Management -> Window Rules
- Here you can create a new window rule for LibreSplit.
- Give it any Description you'd like such as "LibreSplit Always on Top"
- Set "Window class (application)" to "Exact match" and "libresplit"
- Set "Match whole window class" to "No"
- Set the "Window types" dropdown to "All selected"
- Now click "Add Property..."
  - Scroll and look for "Arrangement & Access"
  - Under this you should see "Keep above other windows" select that
- Close the "Add Property..." panel
- Set Keep above other windows to Force and Yes
- Click Apply

### Colors

The color of a time or delta has a special meaning.

| Color       | Meaning                                |
| ----------- | -------------------------------------- |
| Dark red    | Behind splits in PB and losing time    |
| Light red   | Behind splits in PB and gaining time   |
| Dark green  | Ahead of splits in PB and gaining time |
| Light green | Ahead of splits in PB and losing time  |
| Blue        | Best split time in any run             |
| Gold        | Best segment time in any run           |

### Auto Splitters

Katana supports auto splitters written in Lua to handle splits based on in-game events. For writing one for your speedgame of choice, see [here](docs/auto-splitters.md)!

---

## Split Files

~~Split files in LibreSplit are stored as well-formed JSON.~~
~~Check the [Split Files documentation](docs/split-files.md) for more information.~~

---

## Themes

You can customize LibreSplit to your liking using themes.

For more information, check the [Themes documentation](docs/themes.md).

---

## Uninstall LibreSplit

You can uninstall LibreSplit using your package manager or, if you built it manually, by running

```sh
cd build
sudo ninja uninstall
```
