# SHOP
project for that one class im taking

# USAGE
The only dependency you need is `gcc`, `python`, and `make` as well to build Raylib.
```
gcc nob.c -o nob.exe # or just 'nob' on MacOS 
./nob.exe
./shop
```
It really is that simple. It should work on Windows/MacOS.

## WEB BUILD
This can also be built to run on web, you will need the emscripten toolchain. 
```
gcc nob_wasm.c -o nob_wasm
./nob_wasm
```
In general, it's annoying to test this way, so I prefer native build. The end result is intended to be for the web though.

# OVERVIEW
If I had to describe some kind of "tech stack" it would be
```
ImGui (certain components of UI)
------------------------------------
Raylib (platform abstraction)
------------------------------------
OpenGL/GLSL (epic rendering)
------------------------------------
SQLite (database)
------------------------------------
C/C++ (because Jai isn't out yet!!)
------------------------------------
WASM (the future of web development)
```
Everything is vendored and built into the binary. This makes dependency hell and your 3GB `node_modules` folder impossible.

# BACKEND
For now this is a TODO. The main limitation so far is that the database will be per-machine. My intent is to just host it via github pages and be done with it, but maybe a proper persistant database and cloud infrastructure will be worth it. (Honestly just get a raspberry PI, and host everything there).
