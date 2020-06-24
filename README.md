# bspguy
A tool for modifying and merging Sven Co-op maps without decompiling.

# Usage
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

To launch the 3D editor, drag and drop a .bsp file onto the executable, or "Open with" bspguy, or run `bspguy <mapname>`

See the [wiki](https://github.com/wootguy/bspguy/wiki) for more info.

# Building the source
### Windows users:
1. Install CMake and Visual Studio
1. Download and extract the source somewhere
1. Download [Dear ImGui](https://github.com/ocornut/imgui/releases) and extract next to the `src` folder. Rename to `imgui`.
1. Download [GLFW](https://www.glfw.org/) and extract next to the `src` folder. Rename to `glfw`.
1. Download [GLEW](http://glew.sourceforge.net/) (choose the  `Binaries 		Windows 32-bit and 64-bit` link) and extract next to the `src` folder. Rename to `glew`.
1. Open a command prompt in the `bspguy` folder and run these commands:
    ```
    mkdir build && cd build
    cmake ..
    cmake --build . --config Release
    ```

### Linux users:
1. Install Git, CMake, X11, GLFW, and GLEW.
    * Debian: `sudo apt install git cmake libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libglfw3-dev libglew-dev`
1. Download the source: `git clone https://github.com/wootguy/bspguy.git`
1. Download [Dear ImGui](https://github.com/ocornut/imgui/releases) and extract next to the `src` folder. Rename to `imgui`.
1. Open a shell in the `bspguy` folder and run these commands:
    ```
    mkdir build; cd build
    cmake .. -DCMAKE_BUILD_TYPE=RELEASE
    make
    ```
