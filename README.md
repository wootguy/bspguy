# bspguy
A tool for editing GoldSrc maps without decompiling.

# Usage
Double-click to launch the 3D editor, or run `bspguy -help` for the command-line interface.

See the [wiki](https://github.com/wootguy/bspguy/wiki) for merging and porting tutorials.

## Editor Features
- Entity editor with FGD support
- Visualized entity connections and keyvalue searching
- Map merging and porting tools
- BSP model editing (convex only)
- Texture editing
- Clipnode rendering
- And more!

![image](https://github.com/user-attachments/assets/1db127c9-e52a-4ca6-a011-ccb2ed2a7407)

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
