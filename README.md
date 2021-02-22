# bspguy
A tool for modifying and merging Sven Co-op maps without decompiling.

# Usage
To launch the 3D editor, drag and drop a .bsp file onto the executable, or "Open with" bspguy, or run `bspguy <mapname>`

See the [wiki](https://github.com/wootguy/bspguy/wiki) for tutorials.

## Editor Features
- Keyvalue editor with FGD support
- Entity + BSP model creation and duplication
- Easy object movement and scaling
- Vertex manipulation + face splitting
    - Used to make perfectly shaped triggers. A box is often good enough, though.
- BSP model origin movement/alignment
- Optimize + clean commands to prevent overflows
- Hull deletion + redirection + creation
  - clipnode generation is similar to `-cliptype legacy` in the CSG compiler (the _worst_ method)
- Basic face editing

![image](https://user-images.githubusercontent.com/12087544/88471604-1768ac80-cec0-11ea-9ce5-13095e843ce7.png)

**The editor is full of bugs, unstable, and has no undo button yet. Save early and often! Make backups before experimenting with anything.**

Requires OpenGL 3.0 or later.

## First-time Setup
1. Click `File` -> `Settings` -> `General`
1. Set the `Game Directory` to your `Sven Co-op` folder path, then click `Apply Changes`.
    - This will fix the missing textures.
1. Click the `FGDs` tab and add the full path to your sven-coop.fgd (found in `Sven Co-op/svencoop/`). Click `Apply Changes`.
    - This will give point entities more colorful cubes, and enable the `Attributes` tab in the `Keyvalue editor`.

bspguy saves configuration files to `%APPDATA%/bspguy` on Windows.


## Command Line
Some functions are only available via the CLI.

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
1. Install CMake and Visual Studio
1. Download and extract the source somewhere
1. Download [Dear ImGui](https://github.com/ocornut/imgui/releases/latest/) and extract next to the `src` folder. Rename to `imgui`.
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
1. Download [Dear ImGui](https://github.com/ocornut/imgui/releases/latest) and extract next to the `src` folder. Rename to `imgui`.
1. Open a shell in the `bspguy` folder and run these commands:
    ```
    mkdir build; cd build
    cmake .. -DCMAKE_BUILD_TYPE=RELEASE
    make
    ```
