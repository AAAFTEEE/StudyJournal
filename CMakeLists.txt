cmake_minimum_required(VERSION 3.16)
project(StudyJournal CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SJ_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(StudyJournal WIN32 ${SJ_SOURCES} res/app.rc)
target_link_libraries(StudyJournal
    comctl32 comdlg32 ole32 shell32 dwmapi wininet gdi32 user32
)

# Headless core tests: plain-text diary storage / task files
enable_testing()
add_executable(test_core
    src/encoding.cpp
    src/storage.cpp
    src/tasks.cpp
    tests/test_core.cpp
)
target_link_libraries(test_core gdi32 ole32 shell32)
add_test(NAME core COMMAND test_core)
