# bspguy
A tool for editing GoldSrc maps without decompiling.

# Usage
Double-click to launch the 3D editor, or drag a BSP file onto the program, or run `bspguy -help` for the command-line interface.

Supported BSP files: 
- BSP30 (Half-Life and Half-Life: Blue Shift)
- BSP29 (Quake 1)
- BSP2/2PSB (Quake 1 source ports)

See the [wiki](https://github.com/wootguy/bspguy/wiki) for tutorials.

## Editor Features
- Entity editor with FGD support
- Visualized entity connections with search tool
- Map [merging](https://github.com/wootguy/bspguy/wiki/Merging-workflow), [splitting](https://github.com/wootguy/bspguy/wiki/Porting-Sven-Co%E2%80%90op-Maps-to-Half%E2%80%90Life#split-and-merge-basic), and [other porting tools](https://github.com/wootguy/bspguy/wiki/Porting-Sven-Co%E2%80%90op-Maps-to-Half%E2%80%90Life)
- [VIS](https://github.com/wootguy/bspguy/wiki/PVS-Editing#vis-recompilation) and [RAD](https://github.com/wootguy/bspguy/wiki/Porting-Sven-Co%E2%80%90op-Maps-to-Half%E2%80%90Life#rad-preparation-tool) recompilation tools
- [PVS editing](https://github.com/wootguy/bspguy/wiki/PVS-Editing)
- BSP model editing
- Face editing (textures, lightmaps)
- Clipnode/Leaf visualization
- And more!

<img width="1823" alt="image" src="https://github.com/user-attachments/assets/8fcf5dc5-448d-41f8-94de-49d02f5c8f58" />

## System Requirements
- OpenGL 2.1
- Windows XP or later / Any flavor of Linux (AFAIK)
- 256MB of VRAM

If the program fails to start on Windows, launch it from the Command Prompt so you can see what failed. If the program crashes when loading a map, or textures/objects are black or completely missing, switch to the legacy renderer (`Settings` -> `Editor Setup` -> `Rendering` -> `OpenGL (Legacy)`).

## First-time Setup
1. Click `Settings` -> `Editor Setup` -> `Asset Paths`
1. Set the `Game Directory` to your game folder path (e.g. `D:\Steam\steamapps\common\Half-Life`).
1. Add mod directory names as `Asset Paths` (e.g. `valve`, `cstrike`, `svencoop`)
    - Suffixed paths are searched automatically (adding `valve` implies `valve_addon` and `valve_downloads`).
    - Use absolute paths if you have multiple game directories (e.g. `C:\Steam\steamapps\common\Sven Co-op\svencoop`).
1. Click the `FGDs` tab and add your mod FGD file(s). Paths can be absolute or relative to your Asset Paths.
1. Click `Apply Changes`. This should fix missing textures, replace pink cubes, and enable the `Attributes` tab in the `Keyvalue editor`

bspguy saves configuration files to `%APPDATA%/bspguy` on Windows, and to `~/.config/bspguy` on Linux.

## Command Line
Some editor functions are also available via the CLI. I recommend using the CLI for map merging because it can process 3+ maps at once.

```
Usage: bspguy <command> <mapname> [options]

<Commands>
  info      : Show BSP data summary
  merge     : Merges two or more maps together
  noclip    : Delete some clipnodes/nodes from the BSP
  delete    : Delete BSP models
  simplify  : Simplify BSP models
  transform : Apply 3D transformations to the BSP

Run 'bspguy <command> help' to read about a specific command.
```

# Building the source
### Windows users:
1. Install [CMake](https://cmake.org/download/), [Visual Studio Community](https://visualstudio.microsoft.com/downloads/), and [Git](https://git-scm.com/download/win).
    * Visual Studio: Make sure to checkmark "Desktop development with C++" if you're installing for the first time. 
1. Open a command prompt somewhere and run these commands to download and build the source code:
   ```
    git clone --recurse-submodules --shallow-submodules https://github.com/wootguy/bspguy
    cd bspguy
    mkdir build && cd build
    cmake ..
    cmake --build . --config Release
    ```
    (you can open a command-prompt in the current folder by typing `cmd` into the address bar of the explorer window)

To build an x86 version for Windows XP, replace the `cmake ..` command with `cmake -A win32 -T v141_xp ..`. You will need the  `v141_xp` toolset downloaded. It's available in the Visual Studio Installer for VS 2017.

### Linux users:
1. Install Git, CMake, X11, GLFW, GLEW, FreeType, and a compiler.
    * Debian: `sudo apt install build-essential git cmake libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev xorg-dev libglfw3-dev libglew-dev libxxf86vm-dev libfreetype6-dev`
1. Open a terminal somewhere and run these commands:
    ```
    git clone --recurse-submodules --shallow-submodules https://github.com/wootguy/bspguy
    cd bspguy
    mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make
    ```
    (a terminal can _usually_ be opened by pressing F4 with the file manager window in focus)
