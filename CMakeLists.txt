cmake_minimum_required(VERSION 3.22)
project(kde-widget-live2d VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(GNUInstallDirs)
include(CTest)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 DBus)
qt_standard_project_setup(REQUIRES 6.5)

set(CUBISM_SDK_ROOT "" CACHE PATH "Extracted Cubism SDK for Native 5 R5 directory")
set(GLEW_SOURCE_ROOT "" CACHE PATH "Optional GLEW source directory (containing src/glew.c)")
if(NOT EXISTS "${CUBISM_SDK_ROOT}/Core/include/Live2DCubismCore.h" OR
   NOT EXISTS "${CUBISM_SDK_ROOT}/Framework/src/Rendering/OpenGL/Shaders/Standard")
    message(FATAL_ERROR "Set -DCUBISM_SDK_ROOT=/path/to/CubismSdkForNative-5-r.5 (Core and Framework required). See README.md.")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(_core_platform linux/x86_64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(_core_platform experimental/linux/arm64)
else()
    message(FATAL_ERROR "This build supports Linux x86_64 and aarch64.")
endif()
set(CUBISM_CORE_LIBRARY "${CUBISM_SDK_ROOT}/Core/lib/${_core_platform}/libLive2DCubismCore.a"
    CACHE FILEPATH "Cubism Core static library")
if(NOT EXISTS "${CUBISM_CORE_LIBRARY}")
    message(FATAL_ERROR "Cubism Core library not found: ${CUBISM_CORE_LIBRARY}")
endif()
find_package(OpenGL REQUIRED COMPONENTS OpenGL EGL)
if(GLEW_SOURCE_ROOT)
    add_library(live2d_glew STATIC "${GLEW_SOURCE_ROOT}/src/glew.c")
    target_include_directories(live2d_glew PUBLIC "${GLEW_SOURCE_ROOT}/include")
    # The helper uses EGL without GLX or a Qt OpenGL context.
    target_compile_definitions(live2d_glew PUBLIC GLEW_STATIC GLEW_NO_GLU PRIVATE GLEW_EGL)
    target_link_libraries(live2d_glew PUBLIC OpenGL::GL ${CMAKE_DL_LIBS})
    set(_glew_target live2d_glew)
else()
    find_package(GLEW REQUIRED)
    set(_glew_target GLEW::GLEW)
endif()

add_library(CubismCore STATIC IMPORTED)
set_target_properties(CubismCore PROPERTIES IMPORTED_LOCATION "${CUBISM_CORE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${CUBISM_SDK_ROOT}/Core/include")
# Each helper owns a separate Cubism Framework instance and graphics device.
set(_framework_root "${CUBISM_SDK_ROOT}/Framework/src")
file(GLOB_RECURSE _framework_common CONFIGURE_DEPENDS "${_framework_root}/*.cpp")
list(FILTER _framework_common EXCLUDE REGEX "/Rendering/[^/]+/")
function(add_live2d_renderer backend renderer)
    file(GLOB _renderer_sources CONFIGURE_DEPENDS "${_framework_root}/Rendering/${renderer}/*.cpp")
    if(backend STREQUAL "vulkan")
        include(cmake/CubismVulkanFixes.cmake)
        live2d_patch_vulkan_sources(_patched_sources)
        list(FILTER _renderer_sources EXCLUDE REGEX "/(CubismRenderer_Vulkan|CubismClass_Vulkan|CubismRenderTarget_Vulkan)\\.cpp$")
        list(APPEND _renderer_sources ${_patched_sources})
    endif()
    add_library(cubism_${backend} STATIC ${_framework_common} ${_renderer_sources})
    set_target_properties(cubism_${backend} PROPERTIES AUTOMOC OFF)
    target_include_directories(cubism_${backend} SYSTEM PUBLIC "${_framework_root}")
    target_include_directories(cubism_${backend} PRIVATE "${_framework_root}/Rendering/${renderer}")
    target_link_libraries(cubism_${backend} PUBLIC CubismCore)
    qt_add_executable(live2d-render-${backend}
        src/renderhelper.cpp src/cubismmodel.cpp src/cubismmodel.h
        src/modelassets.cpp src/modelassets.h src/graphicscontext.h src/renderprotocol.h)
    target_include_directories(live2d-render-${backend} PRIVATE src)
    target_link_libraries(live2d-render-${backend} PRIVATE cubism_${backend} Qt6::Gui)
    target_compile_options(live2d-render-${backend} PRIVATE -Wall -Wextra -Wpedantic)
    install(TARGETS live2d-render-${backend} RUNTIME DESTINATION "${LIVE2D_HELPER_INSTALL_DIR}")
endfunction()
set(LIVE2D_HELPER_INSTALL_DIR "${CMAKE_INSTALL_LIBEXECDIR}/kde-live2d" CACHE STRING "Renderer helper directory")
add_live2d_renderer(opengl OpenGL)
target_compile_definitions(cubism_opengl PUBLIC CSM_TARGET_LINUX_GL)
target_link_libraries(cubism_opengl PUBLIC ${_glew_target} OpenGL::GL)
target_sources(live2d-render-opengl PRIVATE src/graphicscontext_gl.cpp)
target_link_libraries(live2d-render-opengl PRIVATE OpenGL::EGL)
file(GLOB _cubism_shaders "${_framework_root}/Rendering/OpenGL/Shaders/Standard/*")
qt_add_resources(live2d-render-opengl cubism_gl_shaders
    PREFIX "/live2d/FrameworkShaders" BASE "${_framework_root}/Rendering/OpenGL/Shaders/Standard"
    FILES ${_cubism_shaders})

option(LIVE2D_BUILD_VULKAN "Build the native Vulkan renderer" ON)
if(LIVE2D_BUILD_VULKAN)
    find_package(Vulkan REQUIRED)
    find_program(GLSLC_EXECUTABLE glslc REQUIRED)
    add_live2d_renderer(vulkan Vulkan)
    target_compile_definitions(cubism_vulkan PUBLIC CSM_TARGET_LINUX_VK)
    target_link_libraries(cubism_vulkan PUBLIC Vulkan::Vulkan)
    target_compile_definitions(live2d-render-vulkan PRIVATE LIVE2D_VULKAN)
    target_sources(live2d-render-vulkan PRIVATE src/graphicscontext_vk.cpp)
    target_compile_options(live2d-render-vulkan PRIVATE -Wno-missing-field-initializers)
    add_subdirectory("${_framework_root}/Rendering/Vulkan/Shaders" "${CMAKE_BINARY_DIR}/vulkan-shaders")
    qt_add_resources(live2d-render-vulkan cubism_vk_shaders
        PREFIX "/live2d/FrameworkShaders" BASE "${CMAKE_BINARY_DIR}/vulkan-shaders/compiledShaders"
        FILES ${compiled_shaders_framework})
    add_dependencies(live2d-render-vulkan FrameworkShaders)
endif()

qt_add_library(live2dbackend SHARED)
qt_add_qml_module(live2dbackend
    URI org.kde.plasma.live2d VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/org/kde/plasma/live2d"
    SOURCES src/live2ditem.h src/live2ditem.cpp src/renderprotocol.h
            src/cursorstate.h src/desktoptracker.h src/desktoptracker.cpp)
set_source_files_properties(src/kwin-pointer.js PROPERTIES QT_RESOURCE_ALIAS kwin-pointer.js)
qt_add_resources(live2dbackend cursor_script PREFIX "/live2d" FILES src/kwin-pointer.js)
target_include_directories(live2dbackend PRIVATE src)
target_link_libraries(live2dbackend PRIVATE Qt6::Quick Qt6::DBus ${CMAKE_DL_LIBS})
target_compile_definitions(live2dbackend PRIVATE
    LIVE2D_HELPER_BUILD_DIR="${CMAKE_BINARY_DIR}")
# Resolve installed helpers relative to this module, so staging installs work.
set(LIVE2D_QML_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/qt6/qml/org/kde/plasma/live2d"
    CACHE STRING "Qt 6 QML module installation directory")
get_filename_component(_qml_absolute "${LIVE2D_QML_INSTALL_DIR}" ABSOLUTE BASE_DIR "${CMAKE_INSTALL_PREFIX}")
get_filename_component(_helper_absolute "${LIVE2D_HELPER_INSTALL_DIR}" ABSOLUTE BASE_DIR "${CMAKE_INSTALL_PREFIX}")
file(RELATIVE_PATH _helper_relative "${_qml_absolute}" "${_helper_absolute}")
target_compile_definitions(live2dbackend PRIVATE LIVE2D_HELPER_RELATIVE="${_helper_relative}")
target_compile_options(live2dbackend PRIVATE -Wall -Wextra -Wpedantic)
add_dependencies(live2dbackend live2d-render-opengl)
if(LIVE2D_BUILD_VULKAN)
    add_dependencies(live2dbackend live2d-render-vulkan)
endif()

qt_add_executable(live2d-preview src/preview.cpp)
set_target_properties(live2d-preview PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
target_link_libraries(live2d-preview PRIVATE Qt6::Quick Qt6::QuickControls2)
add_dependencies(live2d-preview live2dbackendplugin)
target_compile_definitions(live2d-preview PRIVATE LIVE2D_QML_BUILD_DIR="${CMAKE_BINARY_DIR}/qml")
set_source_files_properties(preview/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
qt_add_resources(live2d-preview preview_qml PREFIX "/preview" FILES preview/Main.qml)

set_target_properties(live2dbackend PROPERTIES INSTALL_RPATH "$ORIGIN")
set_target_properties(live2dbackendplugin PROPERTIES INSTALL_RPATH "$ORIGIN")
install(TARGETS live2dbackend live2dbackendplugin LIBRARY DESTINATION "${LIVE2D_QML_INSTALL_DIR}")
install(FILES "${CMAKE_BINARY_DIR}/qml/org/kde/plasma/live2d/qmldir"
              "${CMAKE_BINARY_DIR}/qml/org/kde/plasma/live2d/live2dbackend.qmltypes"
        DESTINATION "${LIVE2D_QML_INSTALL_DIR}")
install(TARGETS live2d-preview RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY package/ DESTINATION "${CMAKE_INSTALL_DATADIR}/plasma/plasmoids/org.kde.plasma.live2d")

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(cursor-test tests/cursor_test.cpp)
    set_target_properties(cursor-test PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    target_include_directories(cursor-test PRIVATE src)
    target_link_libraries(cursor-test PRIVATE live2dbackend Qt6::Quick Qt6::DBus Qt6::Test)
    target_compile_definitions(cursor-test PRIVATE
        LIVE2D_CURSOR_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/src/kwin-pointer.js"
        LIVE2D_CONFIG_QML="${CMAKE_CURRENT_SOURCE_DIR}/package/contents/ui/configGeneral.qml")
    add_test(NAME cursor-tracking COMMAND cursor-test)
    set_tests_properties(cursor-tracking PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    qt_add_executable(modelassets-test tests/modelassets_test.cpp src/modelassets.cpp src/modelassets.h)
    target_include_directories(modelassets-test PRIVATE src)
    target_link_libraries(modelassets-test PRIVATE Qt6::Test Qt6::Gui)
    add_test(NAME modelassets COMMAND modelassets-test)
    set(LIVE2D_TEST_MODEL "" CACHE FILEPATH "Optional .model3.json for real rendering and integration tests")
    if(LIVE2D_TEST_MODEL)
        qt_add_executable(item-test tests/item_test.cpp)
        target_include_directories(item-test PRIVATE src)
        target_link_libraries(item-test PRIVATE live2dbackend Qt6::Quick Qt6::Test)
        add_test(NAME item-lifecycle COMMAND item-test)
        set_tests_properties(item-lifecycle PROPERTIES ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;LIBGL_ALWAYS_SOFTWARE=1;LIVE2D_TEST_MODEL=${LIVE2D_TEST_MODEL}")
        # QRhi is a private Qt API and is confined to the integration test.
        if(LIVE2D_BUILD_VULKAN AND Qt6_VERSION VERSION_GREATER_EQUAL 6.6)
            find_package(Qt6 QUIET COMPONENTS GuiPrivate)
            if(TARGET Qt6::GuiPrivate)
                qt_add_executable(presentation-test tests/presentation_test.cpp)
                target_include_directories(presentation-test PRIVATE src)
                target_link_libraries(presentation-test PRIVATE live2dbackend Qt6::Quick Qt6::GuiPrivate Vulkan::Vulkan)
                foreach(_backend opengl vulkan)
                    add_test(NAME presentation-vulkan-${_backend} COMMAND presentation-test
                        "${LIVE2D_TEST_MODEL}" "${_backend}" "${CMAKE_BINARY_DIR}/presentation-${_backend}.png")
                    set_tests_properties(presentation-vulkan-${_backend} PROPERTIES
                        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=rhi;LIBGL_ALWAYS_SOFTWARE=1")
                endforeach()
            endif()
        endif()
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME renderer-opengl COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/renderer_test.py" "$<TARGET_FILE:live2d-render-opengl>"
            "${LIVE2D_TEST_MODEL}" "${CMAKE_BINARY_DIR}/opengl-frame.rgba")
        set_tests_properties(renderer-opengl PROPERTIES ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        if(LIVE2D_BUILD_VULKAN)
            add_test(NAME renderer-vulkan COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/renderer_test.py" "$<TARGET_FILE:live2d-render-vulkan>"
                "${LIVE2D_TEST_MODEL}" "${CMAKE_BINARY_DIR}/vulkan-frame.rgba")
        endif()
    endif()
endif()
