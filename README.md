# RBLX Clone Engine

A simple C++ game engine recreating the classic 2009-2014 Roblox aesthetic.

## Prerequisites

You need to have the following installed on your system:
1.  **CMake** (3.15 or higher) - [Download](https://cmake.org/download/)
2.  **C++ Compiler** (Visual Studio 2022 Community with "Desktop development with C++") - [Download](https://visualstudio.microsoft.com/vs/community/)

## Building and Running

1.  Open a terminal in this folder.
2.  Create a build directory:
    ```powershell
    mkdir build
    cd build
    ```
3.  Configure the project (this downloads dependencies automatically):
    ```powershell
    cmake ..
    ```
4.  Build the project:
    ```powershell
    cmake --build . --config Release
    ```
5.  Run the engine:
    ```powershell
    .\bin\Release\RBLXCloneEngine.exe
    ```
    (Or just `.\bin\RBLXCloneEngine.exe` if on Linux/Mac)

## Controls

-   **WASD**: Move Camera
-   **Right Click (Hold)**: Rotate Camera (Look around)
-   **E**: Move Up
-   **Q**: Move Down











