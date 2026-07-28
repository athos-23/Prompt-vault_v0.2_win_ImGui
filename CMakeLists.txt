cmake_minimum_required(VERSION 3.16)
project(PromptVault CXX)

set(CMAKE_CXX_STANDARD 17)

# Scarica ImGui automaticamente durante la build
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.4
)
FetchContent_MakeAvailable(imgui)

# Aggiungi i file sorgente di ImGui necessari per Windows + DirectX11
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

add_executable(PromptVault WIN32 main.cpp ${IMGUI_SOURCES})

target_include_directories(PromptVault PRIVATE 
    ${imgui_SOURCE_DIR} 
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(PromptVault PRIVATE d3d11 d3dcompiler)
