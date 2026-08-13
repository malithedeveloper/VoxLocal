cmake_minimum_required(VERSION 3.28)

project(VoxLocal VERSION 1.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(VOXLOCAL_BUILD_PLUGIN "Build the OBS Studio plugin" ON)
option(VOXLOCAL_BUILD_TESTS "Build unit tests" ON)
option(VOXLOCAL_ENABLE_ONNX "Enable ONNX Runtime-backed Chatterbox inference when available" ON)
option(VOXLOCAL_FETCH_ONNXRUNTIME "Download a pinned ONNX Runtime when no local SDK is found" ON)
option(VOXLOCAL_FETCH_OBS_SDK "Download and build the OBS development SDK on Windows/macOS" OFF)

if(VOXLOCAL_FETCH_OBS_SDK)
  include(cmake/obs-sdk.cmake)
  voxlocal_setup_obs_sdk()
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Concurrent Network WebSockets)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(voxlocal-core STATIC
  include/voxlocal/kick-connector.hpp
  include/voxlocal/model-manager.hpp
  include/voxlocal/speech-queue.hpp
  include/voxlocal/chatterbox-engine.hpp
  include/voxlocal/runtime.hpp
  src/config-store.cpp
  src/command-router.cpp
  src/kick-connector.cpp
  src/model-manager.cpp
  src/chatterbox-engine.cpp
  src/speech-queue.cpp
  src/runtime.cpp
)
target_include_directories(voxlocal-core PUBLIC include)
target_link_libraries(voxlocal-core
  PUBLIC
    Qt6::Core
    Qt6::Concurrent
    Qt6::Network
    Qt6::WebSockets
    nlohmann_json::nlohmann_json
)
target_compile_definitions(voxlocal-core PUBLIC
  VOXLOCAL_VERSION="${PROJECT_VERSION}"
  VOXLOCAL_MODEL_REVISION="452d3f434aa592098f1eedac9099f33642ab2da5"
  $<$<PLATFORM_ID:Windows>:NOMINMAX>
  $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
)
if(MSVC)
  target_compile_options(voxlocal-core PRIVATE /W4 /permissive-)
else()
  target_compile_options(voxlocal-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(VOXLOCAL_ENABLE_ONNX)
  find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    HINTS ENV ONNXRUNTIME_ROOT
    PATH_SUFFIXES include include/onnxruntime/core/session)
  find_library(ONNXRUNTIME_LIBRARY onnxruntime
    HINTS ENV ONNXRUNTIME_ROOT
    PATH_SUFFIXES lib lib64)
  if((NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY) AND VOXLOCAL_FETCH_ONNXRUNTIME)
    include(cmake/onnxruntime.cmake)
    voxlocal_fetch_onnxruntime()
    find_library(ONNXRUNTIME_LIBRARY onnxruntime
      HINTS "${ONNXRUNTIME_LIBRARY_DIR}"
      NO_DEFAULT_PATH REQUIRED)
  endif()
  if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    target_include_directories(voxlocal-core PRIVATE "${ONNXRUNTIME_INCLUDE_DIR}")
    target_link_libraries(voxlocal-core PRIVATE "${ONNXRUNTIME_LIBRARY}")
    target_compile_definitions(voxlocal-core PRIVATE VOXLOCAL_HAVE_ONNXRUNTIME=1)
    if(VOXLOCAL_ORT_DIRECTML)
      target_compile_definitions(voxlocal-core PRIVATE VOXLOCAL_ORT_DIRECTML=1)
    endif()
  else()
    message(STATUS "ONNX Runtime not found: VoxLocal will build with model-management support but inference disabled")
  endif()
endif()

if(VOXLOCAL_BUILD_PLUGIN)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Gui Widgets)
  find_package(libobs CONFIG QUIET)
  find_package(obs-frontend-api CONFIG QUIET)
  if(TARGET OBS::libobs AND TARGET OBS::obs-frontend-api)
    set(VOXLOCAL_OBS_LIBRARIES OBS::libobs OBS::obs-frontend-api)
  else()
    find_path(LIBOBS_INCLUDE_DIR obs-module.h
      HINTS ENV OBS_ROOT "${VOXLOCAL_OBS_SDK_ROOT}"
      PATH_SUFFIXES include include/obs)
    find_library(LIBOBS_LIBRARY obs
      HINTS ENV OBS_ROOT "${VOXLOCAL_OBS_SDK_ROOT}"
      PATH_SUFFIXES lib lib64 bin/64bit)
    if(NOT LIBOBS_INCLUDE_DIR OR NOT LIBOBS_LIBRARY)
      message(FATAL_ERROR "OBS development headers/libraries not found; set OBS_ROOT")
    endif()
    find_library(OBS_FRONTEND_API obs-frontend-api
      HINTS ENV OBS_ROOT "${VOXLOCAL_OBS_SDK_ROOT}"
      PATH_SUFFIXES lib lib64 bin/64bit
      REQUIRED)
    set(VOXLOCAL_OBS_LIBRARIES "${LIBOBS_LIBRARY}" "${OBS_FRONTEND_API}")
  endif()

  add_library(voxlocal MODULE
    include/voxlocal/voxlocal-dock.hpp
    include/voxlocal/welcome-wizard.hpp
    src/plugin-main.cpp
    src/voxlocal-source.cpp
    src/voxlocal-dock.cpp
    src/welcome-wizard.cpp
  )
  target_include_directories(voxlocal PRIVATE include)
  if(LIBOBS_INCLUDE_DIR)
    target_include_directories(voxlocal SYSTEM PRIVATE "${LIBOBS_INCLUDE_DIR}")
  endif()
  target_link_libraries(voxlocal PRIVATE
    voxlocal-core
    ${VOXLOCAL_OBS_LIBRARIES}
    Qt6::Gui
    Qt6::Widgets
  )
  target_compile_definitions(voxlocal PRIVATE VOXLOCAL_PLUGIN_BUILD=1)
  if(MSVC)
    target_compile_options(voxlocal PRIVATE /W4 /permissive-)
  else()
    target_compile_options(voxlocal PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  set_target_properties(voxlocal PROPERTIES PREFIX "" OUTPUT_NAME "voxlocal")
  if(APPLE)
    set_target_properties(voxlocal PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION plugin
      MACOSX_BUNDLE_GUI_IDENTIFIER dev.malithedeveloper.voxlocal
      MACOSX_BUNDLE_BUNDLE_NAME VoxLocal
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      INSTALL_RPATH "@loader_path/../Frameworks")
  elseif(UNIX)
    set_target_properties(voxlocal PROPERTIES INSTALL_RPATH "$ORIGIN")
  endif()

  if(APPLE)
    install(TARGETS voxlocal LIBRARY DESTINATION . COMPONENT Runtime)
    install(DIRECTORY data/ DESTINATION voxlocal.plugin/Contents/Resources COMPONENT Runtime)
  else()
    install(TARGETS voxlocal
      LIBRARY DESTINATION voxlocal/bin/64bit COMPONENT Runtime
      RUNTIME DESTINATION voxlocal/bin/64bit
      COMPONENT Runtime)
    install(DIRECTORY data/ DESTINATION voxlocal/data COMPONENT Runtime)
  endif()
  if(ONNXRUNTIME_LIBRARY)
    get_filename_component(_ort_library_dir "${ONNXRUNTIME_LIBRARY}" DIRECTORY)
    if(WIN32)
      file(GLOB _ort_runtime_files "${_ort_library_dir}/*.dll")
    elseif(APPLE)
      file(GLOB _ort_runtime_files "${_ort_library_dir}/libonnxruntime*.dylib")
    else()
      file(GLOB _ort_runtime_files "${_ort_library_dir}/libonnxruntime.so*")
    endif()
    if(APPLE)
      install(FILES ${_ort_runtime_files}
        DESTINATION voxlocal.plugin/Contents/Frameworks
        COMPONENT Runtime)
    else()
      install(FILES ${_ort_runtime_files} DESTINATION voxlocal/bin/64bit COMPONENT Runtime)
    endif()
  endif()
  if(WIN32)
    install(FILES "$<TARGET_FILE:Qt6::WebSockets>"
      DESTINATION voxlocal/bin/64bit
      COMPONENT Runtime)
    get_target_property(_qt_core_location Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT _qt_core_location)
      get_target_property(_qt_core_location Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(_qt_bin_dir "${_qt_core_location}" DIRECTORY)
    get_filename_component(_qt_root_dir "${_qt_bin_dir}" DIRECTORY)
    set(_qt_schannel_backend "${_qt_root_dir}/plugins/tls/qschannelbackend.dll")
    if(NOT EXISTS "${_qt_schannel_backend}")
      message(FATAL_ERROR "Could not locate Qt's Windows Schannel TLS backend: ${_qt_schannel_backend}")
    endif()
    install(FILES "${_qt_schannel_backend}"
      DESTINATION voxlocal/bin/64bit/tls
      COMPONENT Runtime)
  elseif(APPLE)
    get_target_property(_qt_websockets_location Qt6::WebSockets IMPORTED_LOCATION_RELEASE)
    if(NOT _qt_websockets_location)
      get_target_property(_qt_websockets_location Qt6::WebSockets IMPORTED_LOCATION)
    endif()
    if(_qt_websockets_location MATCHES "^(.+\\.framework)/")
      set(_qt_websockets_framework "${CMAKE_MATCH_1}")
      install(DIRECTORY "${_qt_websockets_framework}"
        DESTINATION voxlocal.plugin/Contents/Frameworks
        COMPONENT Runtime)
    else()
      message(FATAL_ERROR "Could not locate the Qt WebSockets framework")
    endif()
  endif()
endif()

if(VOXLOCAL_BUILD_TESTS)
  enable_testing()
  add_executable(voxlocal-tests tests/core-tests.cpp)
  target_link_libraries(voxlocal-tests PRIVATE voxlocal-core Qt6::Core)
  if(WIN32 AND ONNXRUNTIME_LIBRARY)
    get_filename_component(_ort_test_library_dir "${ONNXRUNTIME_LIBRARY}" DIRECTORY)
    file(GLOB _ort_test_runtime_files "${_ort_test_library_dir}/*.dll")
    add_custom_command(TARGET voxlocal-tests POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              ${_ort_test_runtime_files} "$<TARGET_FILE_DIR:voxlocal-tests>"
      COMMAND_EXPAND_LISTS)
  endif()
  add_test(NAME voxlocal-core-tests COMMAND voxlocal-tests)
  if(WIN32)
    add_executable(voxlocal-tls-smoke tests/tls-smoke.cpp)
    target_link_libraries(voxlocal-tls-smoke PRIVATE Qt6::Core Qt6::Network)
  endif()
endif()

install(FILES README.md CHANGELOG.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION . COMPONENT Documentation)
install(DIRECTORY assets/ DESTINATION assets COMPONENT Documentation)
