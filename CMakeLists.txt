cmake_minimum_required(VERSION 3.14)
project(RBLXClone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RBLX_BUILD_STUDIO "Build the Studio editor target" ON)
option(RBLX_BUILD_CLIENT "Build the game client target" ON)
option(RBLX_BUILD_SERVER "Build the game server target" ON)
option(RBLX_BUILD_TESTS "Build C++ test targets" ON)
option(RBLX_ENABLE_WARNINGS "Enable stricter compiler warnings" ON)

add_compile_definitions(NOMINMAX GLM_ENABLE_EXPERIMENTAL)

if(NOT WIN32)
    if(RBLX_BUILD_CLIENT OR RBLX_BUILD_SERVER)
        message(WARNING "Client and Server currently depend on WinSock/WinHTTP and are disabled on non-Windows hosts.")
    endif()
    set(RBLX_BUILD_CLIENT OFF CACHE BOOL "Build the game client target" FORCE)
    set(RBLX_BUILD_SERVER OFF CACHE BOOL "Build the game server target" FORCE)
endif()

include(FetchContent)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build GLFW examples" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build GLFW tests" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build GLFW documentation" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG 3.3.8
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm
    GIT_TAG 0af55ccecd98d4e5a8d1fad7de25ba429d60e863 # 1.0.1
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG 2af6dd9694288e6befe1edb7ce25510911693c22 # docking branch snapshot
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo
    GIT_TAG be8aa4aeab86b402701c8c1df011bd8cd776760b
)
FetchContent_GetProperties(imguizmo)
if(NOT imguizmo_POPULATED)
    FetchContent_Populate(imguizmo)
endif()

FetchContent_Declare(
    rp3d
    GIT_REPOSITORY https://github.com/DanielChappuis/reactphysics3d
    GIT_TAG v0.9.0
)
FetchContent_MakeAvailable(rp3d)
if(TARGET reactphysics3d AND NOT MSVC)
    target_compile_options(reactphysics3d PUBLIC
        $<$<COMPILE_LANGUAGE:CXX>:-include>
        $<$<COMPILE_LANGUAGE:CXX>:cstdint>
    )
endif()

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb
    GIT_TAG 31c1ad37456438565541f4919958214b6e762fb4
)
FetchContent_MakeAvailable(stb)

FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/libigl/libigl-glad
    GIT_TAG 651a425101365aa6e8504988ef9bb363d066c5ee
)
FetchContent_MakeAvailable(glad)

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

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

set(IMGUIZMO_SOURCES
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)

function(configure_rblx_target target_name)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${stb_SOURCE_DIR}
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${imguizmo_SOURCE_DIR}
        ${imguizmo_SOURCE_DIR}/src
        ${glad_SOURCE_DIR}/include
    )

    if(RBLX_ENABLE_WARNINGS)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endfunction()

if(RBLX_BUILD_STUDIO)
    add_executable(Studio
        src/main.cpp
        src/Editor.cpp
        ${SHARED_SOURCES}
        ${IMGUI_SOURCES}
        ${IMGUIZMO_SOURCES}
    )
    configure_rblx_target(Studio)
    target_link_libraries(Studio PRIVATE glfw glm reactphysics3d glad)
endif()

if(RBLX_BUILD_CLIENT)
    add_executable(Client
        src/Client.cpp
        ${SHARED_SOURCES}
        ${IMGUI_SOURCES}
    )
    configure_rblx_target(Client)
    target_link_libraries(Client PRIVATE glfw glm reactphysics3d glad ws2_32 winhttp shell32 comdlg32)
endif()

if(RBLX_BUILD_SERVER)
    add_executable(Server
        src/Server.cpp
        src/ServerAuth.cpp
        src/WorldLoader.cpp
        src/PhysicsWorld.cpp
    )
    configure_rblx_target(Server)
    target_link_libraries(Server PRIVATE glm reactphysics3d ws2_32 winhttp)
endif()

if(RBLX_BUILD_TESTS)
    enable_testing()
    add_executable(NetworkProtocolTests tests/network_protocol_tests.cpp)
    target_include_directories(NetworkProtocolTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(NetworkProtocolTests PRIVATE glm)
    add_test(NAME NetworkProtocolTests COMMAND NetworkProtocolTests)
endif()
