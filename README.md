# canvas-of-doom


## Canvas of DOOM (WIP)

I've always thought it would be fun to run a game inside Windows' Paint. And because nobody else did it, I had to.



## TODO

Support input directly from Paint viewport. Currently it is not considered stable.



## Building

To build the project, you need Microsoft VC++ toolchain for x86 and CMake.

```
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A Win32 && cmake --build . --config Release
```

## Running

You have to place a copy of Chocolate DOOM inside a directory named `DOOM`. It has to be in the same place as start.exe.

To start the program, invoke `start.exe` with path to mspaint binary as the argument. E.g. `start.exe C:\Program Files (x86)\Paint Classic\mspaint.exe"`.

**Important**: This project has been written to be compatible with mspaint.exe version **10.0.14393.321**, nowadays distributed by 3rd party vendors as Classic Paint.
It can be made to support other versions by tweaking the offsets in **paintext.c**, but the new Windows 11 edition of Paint varies wildly and hence it is **NOT supported**.



## Preview



https://github.com/treife/canvas-of-doom/assets/162191702/61e34569-7e8c-4dee-b4fd-45e443a82e44

