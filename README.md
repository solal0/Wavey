# Wavey
A standalone Roblox Studio like game engine built in C++

## What changes?
Compared to Roblox Studio, games you make are built into an executable instead of being uploaded to a locked plateforme.

## Why?
Because of Roblox. I started developping in Luau, or more precisely, Roblox Studio. Roblox Studio is great but the problem is the first word, Roblox.
<br>When developping in Roblox Studio, if you want to publish a game you HAVE to publish it on the Roblox platform.
<br>This is a problem for many reason, first of, being the requirements:
- you need to have or make a roblox account
- since not too long ago you need to be 16+ verified with id card to play any newly unpopular games

and second of, all your assets need to comply roblox's rules and pass it's AI system.

## Manual Compiling
Download, Install and add to PATH [CMake](https://cmake.org/download/) and then simply run those three commands:
```
cd C:\Wavey // or wherever wavey's root folder is
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

## Who made Wavey?
- AI - Did all the heavy lifting
- Me (Skira) - HMI, Details, Ideas and a bit few fixes there and there

## Thanks to
- Luau
- ImGui
- ImGuizmo
