# Using CMake's fetch content to pull and setup third party libraries.

include(FetchContent)

FetchContent_Declare(
    D3D12MemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator
    GIT_TAG f128d39b7a95b4235bd228d231646278dc6c24b2
    GIT_PROGRESS TRUE
)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb
    GIT_TAG 8b5f1f37b5b75829fc72d38e7b5d4bcbf8a26d55
    GIT_PROGRESS TRUE
)


option(TINYGLTF_BUILD_LOADER_EXAMPLE "" OFF)
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        544969b7324cd6bba29f6203c7d78c7ea92dbab0
)

FetchContent_MakeAvailable(tinygltf D3D12MemoryAllocator stb)

FetchContent_Declare(
    SDL2
	GIT_REPOSITORY https://github.com/libsdl-org/SDL
	GIT_TAG release-2.26.0 
	GIT_PROGRESS TRUE
)
			
FetchContent_MakeAvailable(SDL2)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG 8cbd391f096b9314a08670052cc0025cbcadb249
    GIT_PROGRESS TRUE
)

FetchContent_MakeAvailable(imgui)

add_library(libimgui
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)

target_include_directories(libimgui PUBLIC 
${imgui_SOURCE_DIR} 
${imgui_SOURCE_DIR}/backends
${sdl2_SOURCE_DIR}/include
)

target_link_libraries(libimgui PUBLIC SDL2::SDL2main SDL2::SDL2-static)

add_library(External INTERFACE)
target_link_libraries(External INTERFACE D3D12MemoryAllocator tinygltf libimgui SDL2::SDL2main SDL2::SDL2test)
target_include_directories(External INTERFACE ${sdl2_SOURCE_DIR}/include ${D3D12MemoryAllocator_SOURCE_DIR}/include ${stb_SOURCE_DIR} ${tinygltf_SOURCE_DIR})