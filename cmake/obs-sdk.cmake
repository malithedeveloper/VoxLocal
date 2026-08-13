include_guard(GLOBAL)

function(_voxlocal_download_and_extract label url sha256 archive destination)
  if(EXISTS "${destination}/.voxlocal-extracted")
    file(READ "${destination}/.voxlocal-extracted" extracted_hash)
    string(STRIP "${extracted_hash}" extracted_hash)
    if(extracted_hash STREQUAL sha256)
      return()
    endif()
    file(REMOVE_RECURSE "${destination}")
  endif()

  file(MAKE_DIRECTORY "${destination}")
  if(NOT EXISTS "${archive}")
    message(STATUS "Downloading ${label}: ${url}")
    file(
      DOWNLOAD "${url}" "${archive}"
      EXPECTED_HASH "SHA256=${sha256}"
      SHOW_PROGRESS
      STATUS download_status
    )
    list(GET download_status 0 download_code)
    list(GET download_status 1 download_message)
    if(NOT download_code EQUAL 0)
      file(REMOVE "${archive}")
      message(FATAL_ERROR "Could not download ${label}: ${download_message}")
    endif()
  endif()

  message(STATUS "Extracting ${label}")
  file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${destination}")
  file(WRITE "${destination}/.voxlocal-extracted" "${sha256}\n")
endfunction()

function(voxlocal_setup_obs_sdk)
  if(NOT WIN32 AND NOT APPLE AND NOT UNIX)
    message(FATAL_ERROR "The fetched OBS SDK is not supported on this platform")
  endif()

  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" buildspec)
  set(sdk_root "${CMAKE_BINARY_DIR}/_obs_sdk")
  set(download_root "${sdk_root}/downloads")
  file(MAKE_DIRECTORY "${download_root}")

  string(JSON obs_version GET "${buildspec}" dependencies obs-studio version)
  string(JSON obs_base_url GET "${buildspec}" dependencies obs-studio baseUrl)
  string(JSON deps_version GET "${buildspec}" dependencies prebuilt version)
  string(JSON deps_base_url GET "${buildspec}" dependencies prebuilt baseUrl)
  string(JSON qt_version GET "${buildspec}" dependencies qt6 version)
  string(JSON qt_base_url GET "${buildspec}" dependencies qt6 baseUrl)

  if(WIN32)
    set(platform windows-x64)
    set(obs_archive_name "OBS-Studio-${obs_version}-Sources.tar.gz")
    set(deps_archive_name "windows-deps-${deps_version}-x64.zip")
    set(qt_archive_name "windows-deps-qt6-${qt_version}-x64.zip")
    set(deps_destination "${sdk_root}/obs-deps-${deps_version}-x64")
    set(qt_destination "${sdk_root}/obs-deps-qt6-${qt_version}-x64")
  elseif(APPLE)
    set(platform macos)
    set(obs_archive_name "OBS-Studio-${obs_version}-Sources.tar.gz")
    set(deps_archive_name "macos-deps-${deps_version}-universal.tar.xz")
    set(qt_archive_name "macos-deps-qt6-${qt_version}-universal.tar.xz")
    set(deps_destination "${sdk_root}/obs-deps-${deps_version}-universal")
    set(qt_destination "${sdk_root}/obs-deps-qt6-${qt_version}-universal")
  else()
    set(platform linux-x64)
    set(obs_archive_name "OBS-Studio-${obs_version}-Sources.tar.gz")
    set(dependency_prefix "")
  endif()

  string(JSON obs_hash GET "${buildspec}" dependencies obs-studio hashes ${platform})

  set(obs_extract_root "${sdk_root}/sources")
  _voxlocal_download_and_extract(
    "OBS Studio sources"
    "${obs_base_url}/${obs_version}/${obs_archive_name}"
    "${obs_hash}"
    "${download_root}/${obs_archive_name}"
    "${obs_extract_root}"
  )
  if(WIN32 OR APPLE)
    string(JSON deps_hash GET "${buildspec}" dependencies prebuilt hashes ${platform})
    string(JSON qt_hash GET "${buildspec}" dependencies qt6 hashes ${platform})
    _voxlocal_download_and_extract(
      "OBS build dependencies"
      "${deps_base_url}/${deps_version}/${deps_archive_name}"
      "${deps_hash}"
      "${download_root}/${deps_archive_name}"
      "${deps_destination}"
    )
    _voxlocal_download_and_extract(
      "OBS Qt dependencies"
      "${qt_base_url}/${qt_version}/${qt_archive_name}"
      "${qt_hash}"
      "${download_root}/${qt_archive_name}"
      "${qt_destination}"
    )
    set(dependency_prefix "${deps_destination};${qt_destination}")
  endif()

  set(obs_source "${obs_extract_root}/obs-studio-${obs_version}-sources")
  set(obs_build "${sdk_root}/obs-build")
  set(obs_install "${sdk_root}/install")

  set(obs_sdk_ready FALSE)
  if(EXISTS "${obs_install}/.voxlocal-obs-sdk-ready")
    file(READ "${obs_install}/.voxlocal-obs-sdk-ready" installed_obs_version)
    string(STRIP "${installed_obs_version}" installed_obs_version)
    if(installed_obs_version STREQUAL obs_version)
      set(obs_sdk_ready TRUE)
    endif()
  endif()

  if(NOT obs_sdk_ready)
    set(configure_command
      "${CMAKE_COMMAND}" -S "${obs_source}" -B "${obs_build}"
      -DOBS_CMAKE_VERSION:STRING=3.0.0
      -DENABLE_PLUGINS:BOOL=OFF
      -DENABLE_FRONTEND:BOOL=OFF
      -DENABLE_SCRIPTING:BOOL=OFF
      -DENABLE_HEVC:BOOL=OFF
      -DOBS_VERSION_OVERRIDE:STRING=${obs_version}
      "-DCMAKE_PREFIX_PATH=${dependency_prefix}"
    )
    if(WIN32)
      list(APPEND configure_command -G "Visual Studio 17 2022" -A x64)
    elseif(APPLE)
      list(APPEND configure_command
        -G Xcode
        "-DCMAKE_OSX_ARCHITECTURES:STRING=${CMAKE_OSX_ARCHITECTURES}"
        -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=13.0
      )
    else()
      list(APPEND configure_command
        -G Ninja
        -DCMAKE_BUILD_TYPE:STRING=Release
        -DENABLE_WAYLAND:BOOL=OFF
        -DENABLE_PULSEAUDIO:BOOL=OFF
      )
    endif()

    message(STATUS "Configuring the OBS development SDK")
    execute_process(COMMAND ${configure_command} COMMAND_ERROR_IS_FATAL ANY)
    message(STATUS "Building libobs and obs-frontend-api")
    if(UNIX AND NOT APPLE)
      execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${obs_build}" --config Release --parallel
        COMMAND_ERROR_IS_FATAL ANY
      )
    else()
      execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${obs_build}" --target obs-frontend-api --config Release --parallel
        COMMAND_ERROR_IS_FATAL ANY
      )
    endif()
    message(STATUS "Installing the OBS development SDK")
    if(UNIX AND NOT APPLE)
      execute_process(
        COMMAND "${CMAKE_COMMAND}" --install "${obs_build}" --component Runtime --config Release --prefix
                "${obs_install}"
        COMMAND_ERROR_IS_FATAL ANY
      )
    endif()
    execute_process(
      COMMAND "${CMAKE_COMMAND}" --install "${obs_build}" --component Development --config Release --prefix
              "${obs_install}"
      COMMAND_ERROR_IS_FATAL ANY
    )
    file(WRITE "${obs_install}/.voxlocal-obs-sdk-ready" "${obs_version}\n")
  endif()

  # Keep an explicitly provided Qt SDK ahead of obs-deps. CI adds the matching
  # Qt WebSockets module there, which OBS itself does not ship.
  set(updated_prefix
    "${obs_install};$ENV{QT_ROOT_DIR};$ENV{CMAKE_PREFIX_PATH};${CMAKE_PREFIX_PATH};${dependency_prefix}")
  if(APPLE)
    set(libobs_config_dir "${obs_install}/Frameworks/libobs.framework/Resources/cmake")
    list(PREPEND updated_prefix "${libobs_config_dir}")
    set(libobs_DIR "${libobs_config_dir}" PARENT_SCOPE)
    set(CMAKE_FRAMEWORK_PATH "${obs_install}/Frameworks;${CMAKE_FRAMEWORK_PATH}" PARENT_SCOPE)
  endif()
  set(CMAKE_PREFIX_PATH "${updated_prefix}" PARENT_SCOPE)
  set(VOXLOCAL_OBS_SDK_ROOT "${obs_install}" PARENT_SCOPE)
  if(DEFINED qt_destination)
    set(VOXLOCAL_QT_SDK_ROOT "${qt_destination}" PARENT_SCOPE)
  endif()
endfunction()
