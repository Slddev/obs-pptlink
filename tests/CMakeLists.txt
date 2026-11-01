add_executable(pptlink-tests)

target_sources(
  pptlink-tests
  PRIVATE
    test.h
    test-main.cpp
    test-layout.cpp
    test-thumb.cpp
    ${CMAKE_SOURCE_DIR}/src/sources/presenter-layout.cpp
    ${CMAKE_SOURCE_DIR}/src/sources/presenter-layout.h
    ${CMAKE_SOURCE_DIR}/src/com-bridge/thumb-cache.cpp
    ${CMAKE_SOURCE_DIR}/src/com-bridge/thumb-cache.h
)

target_include_directories(pptlink-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pptlink-tests PRIVATE ole32 oleaut32 gdiplus)
target_compile_options(pptlink-tests PRIVATE /utf-8 /permissive-)
target_compile_definitions(pptlink-tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

add_test(NAME pptlink-tests COMMAND pptlink-tests)
