cmake_minimum_required(VERSION 3.14)
project(RBLXClone)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-DNOMINMAX)
add_definitions(-DGLM_ENABLE_EXPERIMENTAL)

include(FetchContent)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG 3.3.8
)
FetchContent_MakeAvailable(glfw)

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm
    GIT_TAG master
)
FetchContent_MakeAvailable(glm)

# Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG docking # Use docking branch for full features
)
FetchContent_MakeAvailable(imgui)

# ImGuizmo
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo
    GIT_TAG master
)
FetchContent_MakeAvailable(imguizmo)

# ReactPhysics3D
FetchContent_Declare(
    rp3d
    GIT_REPOSITORY https://github.com/DanielChappuis/reactphysics3d
    GIT_TAG v0.9.0
)
FetchContent_MakeAvailable(rp3d)

# STB Image
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb
    GIT_TAG master
)
FetchContent_MakeAvailable(stb)

# GLAD (Using libigl-glad for pre-generated files)
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/libigl/libigl-glad
    GIT_TAG master
)
FetchContent_MakeAvailable(glad)

# Source Files
set(SHARED_SOURCES
    src/Renderer.cpp
    src/Camera.cpp
    src/Shader.cpp
    src/Window.cpp
    src/Raycaster.cpp
    src/Skybox.cpp
    src/PhysicsWorld.cpp
    src/ShadowMap.cpp
    src/Character.cpp
    src/WorldLoader.cpp
    src/Avatar.cpp
)

# Global Includes
include_directories(include)
include_directories(${stb_SOURCE_DIR})
include_directories(${imgui_SOURCE_DIR})
include_directories(${imgui_SOURCE_DIR}/backends) # ImGui backends
include_directories(${imguizmo_SOURCE_DIR}) # ImGuizmo headers
include_directories(${glad_SOURCE_DIR}/include) # GLAD headers

# ImGui Sources
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# ImGuizmo Sources
set(IMGUIZMO_SOURCES
    ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
)

# 1. Studio (Renamed from Engine)
add_executable(Studio
    src/main.cpp
    src/Editor.cpp
    ${SHARED_SOURCES}
    ${IMGUI_SOURCES}
    ${IMGUIZMO_SOURCES}
)
target_link_libraries(Studio PRIVATE glfw glm reactphysics3d glad ws2_32)

# 2. Client (Renamed from Client, covers offline/online)
add_executable(Client
    src/Client.cpp
    ${SHARED_SOURCES}
    ${IMGUI_SOURCES}
)
target_link_libraries(Client PRIVATE glfw glm reactphysics3d glad ws2_32 winhttp)

# 3. Server (Renamed from Server)
add_executable(Server
    src/Server.cpp
    src/ServerAuth.cpp
    src/WorldLoader.cpp
    src/PhysicsWorld.cpp
)
target_link_libraries(Server PRIVATE glm reactphysics3d ws2_32 winhttp)
