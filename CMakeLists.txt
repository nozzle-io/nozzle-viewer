cmake_minimum_required(VERSION 3.20)
project(nozzle_viewer VERSION 0.1.0 LANGUAGES C CXX)

option(NOZZLE_VIEWER_BUILD_TESTS "Build nozzle-viewer tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(NOZZLE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NOZZLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(nozzle)
add_subdirectory(libs/glfw)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
)
if(APPLE)
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_metal.mm)
elseif(WIN32)
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
else()
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
endif()

add_library(nozzle_viewer_imgui STATIC ${IMGUI_SOURCES})
target_include_directories(nozzle_viewer_imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(nozzle_viewer_imgui PUBLIC glfw)
if(APPLE)
    target_link_libraries(nozzle_viewer_imgui PUBLIC "-framework Metal" "-framework QuartzCore" "-framework AppKit")
elseif(WIN32)
    target_link_libraries(nozzle_viewer_imgui PUBLIC d3d11 dxgi d3dcompiler)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(nozzle_viewer_imgui PUBLIC OpenGL::GL)
endif()

set(NOZZLE_VIEWER_APP_SOURCES
    src/app/source_registry.cpp
    src/app/viewer_state.cpp
    src/gui/gui.cpp
)
if(APPLE)
    list(APPEND NOZZLE_VIEWER_APP_SOURCES src/gui/platform/macos/render_backend_metal.mm)
elseif(WIN32)
    list(APPEND NOZZLE_VIEWER_APP_SOURCES src/gui/platform/windows/render_backend_d3d11.cpp)
else()
    list(APPEND NOZZLE_VIEWER_APP_SOURCES src/gui/platform/linux/render_backend_opengl.cpp)
endif()

add_library(nozzle_viewer_app STATIC ${NOZZLE_VIEWER_APP_SOURCES})
target_include_directories(nozzle_viewer_app PUBLIC src)
target_link_libraries(nozzle_viewer_app PUBLIC nozzle nozzle_viewer_imgui)
target_compile_options(nozzle_viewer_app PRIVATE
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(nozzle-viewer src/main.cpp)
target_link_libraries(nozzle-viewer PRIVATE nozzle_viewer_app)

if(APPLE)
    set_target_properties(nozzle-viewer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER org.nozzle-io.viewer
        MACOSX_BUNDLE_BUNDLE_NAME "Nozzle Viewer"
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    )
endif()

if(NOZZLE_VIEWER_BUILD_TESTS)
    include(CTest)
    add_executable(nozzle_viewer_tests tests/test_main.cpp)
    target_link_libraries(nozzle_viewer_tests PRIVATE nozzle_viewer_app)
    add_test(NAME nozzle_viewer_tests COMMAND nozzle_viewer_tests)
endif()
