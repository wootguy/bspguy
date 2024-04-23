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

**The editor is full of bugs, unstable, and not all actions can be undone. Save early and often! Make backups before experimenting with anything.**

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
1. Install [CMake](https://cmake.org/download/), [Visual Studio Community](https://visualstudio.microsoft.com/downloads/), and [Git](https://git-scm.com/download/win).
    * Visual Studio: Make sure to checkmark "Desktop development with C++" if you're installing for the first time. 
1. Open a command prompt somewhere and run this command to download the source code (don't click the download zip button!):
   ```
    git clone --recurse-submodules https://github.com/wootguy/bspguy
    ```
1. Download [GLEW](http://glew.sourceforge.net/) (choose the `Binaries Windows 32-bit and 64-bit` link) and extract the `glew-x.y.z` folder into the `bspguy` folder that was created in the previous step. Rename the `glew-x.y.z` folder to `glew`.
1. Open a command prompt in the `bspguy` folder and run these commands:
    ```
    mkdir build && cd build
    cmake ..
    cmake --build . --config Release
    ```
    (you can open a command-prompt in the current folder by typing `cmd` into the address bar of the explorer window)

### Linux users:
1. Install Git, CMake, X11, GLFW, GLEW, and a compiler.
    * Debian: `sudo apt install build-essential git cmake libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev xorg-dev libglfw3-dev libglew-dev libxxf86vm-dev`
1. Open a terminal somewhere and run these commands:
    ```
    git clone --recurse-submodules https://github.com/wootguy/bspguy
    cd bspguy
    mkdir build; cd build
    cmake .. -DCMAKE_BUILD_TYPE=RELEASE
    make
    ```
    (a terminal can _usually_ be opened by pressing F4 with the file manager window in focus)
