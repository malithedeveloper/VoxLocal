include_guard(GLOBAL)

function(_voxlocal_download_and_extract label url sha256 archive destination)
  if(EXISTS "${destination}/.voxlocal-extracted")
    file(READ "${destination}/.voxlocal-extracted" extracted_hash)
    string(STRIP "${extracted_hash}" extracted_hash)
    if("${extracted_hash}" STREQUAL "${sha256}")
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
  string(JSON qtwebsockets_version GET "${buildspec}" dependencies qtwebsockets version)
  string(JSON qtwebsockets_url GET "${buildspec}" dependencies qtwebsockets url)
  string(JSON qtwebsockets_hash GET "${buildspec}" dependencies qtwebsockets sha256)

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

    set(qtwebsockets_archive_name "qtwebsockets-${qtwebsockets_version}.tar.gz")
    set(qtwebsockets_source_root "${sdk_root}/qtwebsockets-sources")
    set(qtwebsockets_source
      "${qtwebsockets_source_root}/qtwebsockets-${qtwebsockets_version}")
    set(qtwebsockets_build "${sdk_root}/qtwebsockets-build")
    set(qtwebsockets_install "${sdk_root}/qtwebsockets-install")
    _voxlocal_download_and_extract(
      "Qt WebSockets ${qtwebsockets_version} sources"
      "${qtwebsockets_url}"
      "${qtwebsockets_hash}"
      "${download_root}/${qtwebsockets_archive_name}"
      "${qtwebsockets_source_root}"
    )

    set(qtwebsockets_marker
      "${qtwebsockets_install}/.voxlocal-qtwebsockets-ready")
    set(qtwebsockets_build_id
      "${qtwebsockets_version}-${platform}-${CMAKE_OSX_ARCHITECTURES}")
    set(qtwebsockets_ready FALSE)
    if(EXISTS "${qtwebsockets_marker}")
      file(READ "${qtwebsockets_marker}" installed_qtwebsockets_build_id)
      string(STRIP "${installed_qtwebsockets_build_id}" installed_qtwebsockets_build_id)
      if(installed_qtwebsockets_build_id STREQUAL qtwebsockets_build_id)
        set(qtwebsockets_ready TRUE)
      endif()
    endif()

    if(NOT qtwebsockets_ready)
      file(REMOVE_RECURSE "${qtwebsockets_build}" "${qtwebsockets_install}")
      set(qtwebsockets_configure_command
        "${CMAKE_COMMAND}" -S "${qtwebsockets_source}" -B "${qtwebsockets_build}"
        -DQT_BUILD_TESTS:BOOL=OFF
        -DQT_BUILD_EXAMPLES:BOOL=OFF
        -DBUILD_SHARED_LIBS:BOOL=ON
        "-DCMAKE_PREFIX_PATH=${qt_destination}"
        "-DCMAKE_INSTALL_PREFIX=${qtwebsockets_install}"
      )
      if(WIN32)
        list(APPEND qtwebsockets_configure_command
          -G "Visual Studio 17 2022" -A x64)
      else()
        list(APPEND qtwebsockets_configure_command
          -G Xcode
          "-DCMAKE_OSX_ARCHITECTURES:STRING=${CMAKE_OSX_ARCHITECTURES}"
          -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=13.4)
      endif()
      message(STATUS "Configuring Qt WebSockets against the OBS Qt SDK")
      execute_process(
        COMMAND ${qtwebsockets_configure_command}
        COMMAND_ERROR_IS_FATAL ANY)
      message(STATUS "Building and installing Qt WebSockets")
      execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${qtwebsockets_build}"
                --target install --config Release --parallel
        COMMAND_ERROR_IS_FATAL ANY)
      file(WRITE "${qtwebsockets_marker}" "${qtwebsockets_build_id}\n")
    endif()

    list(PREPEND dependency_prefix "${qtwebsockets_install}")
  endif()

  set(obs_source "${obs_extract_root}/obs-studio-${obs_version}-sources")
  set(obs_build "${sdk_root}/obs-build")
  set(obs_install "${sdk_root}/install")

  set(obs_sdk_ready FALSE)
  if(EXISTS "${obs_install}/.voxlocal-obs-sdk-ready")
    file(READ "${obs_install}/.voxlocal-obs-sdk-ready" installed_obs_version)
    string(STRIP "${installed_obs_version}" installed_obs_version)
    if("${installed_obs_version}" STREQUAL "${obs_version}")
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

  # Use OBS's exact Qt build. Mixing another Qt minor version in the OBS
  # process prevents Windows from loading the plugin.
  set(updated_prefix
    "${obs_install};${dependency_prefix};${CMAKE_PREFIX_PATH}")
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
