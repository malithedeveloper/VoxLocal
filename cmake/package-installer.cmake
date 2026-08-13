cmake_minimum_required(VERSION 3.28)

foreach(required_variable IN ITEMS
    VOXLOCAL_PLATFORM
    VOXLOCAL_PAYLOAD_DIR
    VOXLOCAL_OUTPUT
    VOXLOCAL_WORK_DIR)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "${required_variable} is required")
  endif()
endforeach()

if(NOT VOXLOCAL_PLATFORM MATCHES "^(windows|macos|linux)$")
  message(FATAL_ERROR "VOXLOCAL_PLATFORM must be windows, macos, or linux")
endif()
if(NOT IS_DIRECTORY "${VOXLOCAL_PAYLOAD_DIR}")
  message(FATAL_ERROR "Plugin payload does not exist: ${VOXLOCAL_PAYLOAD_DIR}")
endif()

if(VOXLOCAL_PLATFORM STREQUAL "windows")
  set(expected_plugin "${VOXLOCAL_PAYLOAD_DIR}/voxlocal/bin/64bit/voxlocal.dll")
  set(expected_locale "${VOXLOCAL_PAYLOAD_DIR}/voxlocal/data/locale/en-US.ini")
elseif(VOXLOCAL_PLATFORM STREQUAL "macos")
  set(expected_plugin "${VOXLOCAL_PAYLOAD_DIR}/voxlocal.plugin/Contents/MacOS/voxlocal")
  set(expected_locale "${VOXLOCAL_PAYLOAD_DIR}/voxlocal.plugin/Contents/Resources/locale/en-US.ini")
else()
  set(expected_plugin "${VOXLOCAL_PAYLOAD_DIR}/voxlocal/bin/64bit/voxlocal.so")
  set(expected_locale "${VOXLOCAL_PAYLOAD_DIR}/voxlocal/data/locale/en-US.ini")
endif()
foreach(expected_file IN ITEMS "${expected_plugin}" "${expected_locale}")
  if(NOT EXISTS "${expected_file}")
    message(FATAL_ERROR "Installer payload has an invalid OBS plugin layout: ${expected_file}")
  endif()
endforeach()

if(NOT DEFINED VOXLOCAL_BINARYCREATOR OR VOXLOCAL_BINARYCREATOR STREQUAL "")
  find_program(VOXLOCAL_BINARYCREATOR NAMES binarycreator binarycreator.exe REQUIRED)
endif()
if(NOT EXISTS "${VOXLOCAL_BINARYCREATOR}")
  message(FATAL_ERROR "binarycreator was not found: ${VOXLOCAL_BINARYCREATOR}")
endif()

get_filename_component(VOXLOCAL_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(READ "${VOXLOCAL_SOURCE_DIR}/buildspec.json" buildspec)
string(JSON VOXLOCAL_VERSION GET "${buildspec}" version)
set(VOXLOCAL_RELEASE_DATE "2026-08-13")

if(VOXLOCAL_PLATFORM STREQUAL "windows")
  set(VOXLOCAL_DEFAULT_TARGET "@RootDir@/ProgramData/obs-studio/plugins")
  set(VOXLOCAL_ADMIN_REQUIREMENT "    <RequiresAdminRights>true</RequiresAdminRights>")
elseif(VOXLOCAL_PLATFORM STREQUAL "macos")
  set(VOXLOCAL_DEFAULT_TARGET "@HomeDir@/Library/Application Support/obs-studio/plugins")
  set(VOXLOCAL_ADMIN_REQUIREMENT "")
else()
  set(VOXLOCAL_DEFAULT_TARGET "@HomeDir@/.config/obs-studio/plugins")
  set(VOXLOCAL_ADMIN_REQUIREMENT "")
endif()

get_filename_component(work_parent "${VOXLOCAL_WORK_DIR}" DIRECTORY)
if(VOXLOCAL_WORK_DIR STREQUAL "/" OR VOXLOCAL_WORK_DIR STREQUAL "${work_parent}")
  message(FATAL_ERROR "Refusing unsafe installer work directory: ${VOXLOCAL_WORK_DIR}")
endif()

set(config_dir "${VOXLOCAL_WORK_DIR}/config")
set(package_root "${VOXLOCAL_WORK_DIR}/packages/dev.malithedeveloper.voxlocal")
set(meta_dir "${package_root}/meta")
set(data_dir "${package_root}/data")
file(REMOVE_RECURSE "${VOXLOCAL_WORK_DIR}")
file(MAKE_DIRECTORY "${config_dir}" "${meta_dir}" "${data_dir}")

configure_file(
  "${VOXLOCAL_SOURCE_DIR}/installer/config/config.xml.in"
  "${config_dir}/config.xml"
  @ONLY)
configure_file(
  "${VOXLOCAL_SOURCE_DIR}/installer/packages/dev.malithedeveloper.voxlocal/meta/package.xml.in"
  "${meta_dir}/package.xml"
  @ONLY)
configure_file(
  "${VOXLOCAL_SOURCE_DIR}/installer/config/controller.qs"
  "${config_dir}/controller.qs"
  COPYONLY)
configure_file("${VOXLOCAL_SOURCE_DIR}/LICENSE" "${meta_dir}/LICENSE" COPYONLY)
configure_file(
  "${VOXLOCAL_SOURCE_DIR}/assets/voxlocal-logo.png"
  "${config_dir}/voxlocal-logo.png"
  COPYONLY)
file(COPY "${VOXLOCAL_PAYLOAD_DIR}/" DESTINATION "${data_dir}")

get_filename_component(output_dir "${VOXLOCAL_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
execute_process(
  COMMAND "${VOXLOCAL_BINARYCREATOR}"
          --offline-only
          -c "${config_dir}/config.xml"
          -p "${VOXLOCAL_WORK_DIR}/packages"
          "${VOXLOCAL_OUTPUT}"
  RESULT_VARIABLE binarycreator_result
  COMMAND_ECHO STDOUT)
if(NOT binarycreator_result EQUAL 0)
  message(FATAL_ERROR "binarycreator failed with exit code ${binarycreator_result}")
endif()
