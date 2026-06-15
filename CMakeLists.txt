cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# Windows-only: WGC + COM + GDI+ + D3D11 + WinRT
if(WIN32)
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE ole32 oleaut32 uuid gdiplus windowsapp d3d11 dxgi
  )

  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      /utf-8
      /Zc:__cplusplus
      /await
      /permissive-
      /wd4996 # obs_frontend_get_global_config deprecated
      /wd4267 # size_t -> int narrowing in source-presenter.cpp
  )

  target_compile_definitions(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      UNICODE
      _UNICODE
      NOMINMAX
      WIN32_LEAN_AND_MEAN
      WINVER=0x0A00
      _WIN32_WINNT=0x0A00
      PLUGIN_VERSION="${CMAKE_PROJECT_VERSION}"
  )
else()
  message(FATAL_ERROR "PPTLink only supports Windows (requires COM + WGC)")
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/com-bridge/ppt-com-bridge.cpp
    src/com-bridge/ppt-com-bridge.h
    src/wgc-capture/wgc-capture.cpp
    src/wgc-capture/wgc-capture.h
    src/wgc-capture/wgc-frame-pool.cpp
    src/wgc-capture/wgc-frame-pool.h
    src/sources/source-slide.cpp
    src/sources/source-slide.h
    src/sources/source-presenter.cpp
    src/sources/source-presenter.h
    src/docks/dock-next-slide.cpp
    src/docks/dock-next-slide.h
    src/docks/dock-notes.cpp
    src/docks/dock-notes.h
    src/hotkeys.cpp
    src/hotkeys.h
)

target_include_directories(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src src/com-bridge src/wgc-capture src/sources src/docks
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
