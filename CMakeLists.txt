cmake_minimum_required(VERSION 3.20)

project(BlackFix_VideoShuffle LANGUAGES CXX)

add_executable(BlackFix_VideoShuffle WIN32
    src/add_dialog.cpp
    src/app.cpp
    src/delete_dialog.cpp
    src/http.cpp
    src/main.cpp
    src/storage.cpp
    src/thumbnail.cpp
    src/updater.cpp
    src/utils.cpp
)

target_compile_features(BlackFix_VideoShuffle PRIVATE cxx_std_17)
target_compile_definitions(BlackFix_VideoShuffle PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(BlackFix_VideoShuffle PRIVATE gdiplus winhttp shell32 user32 gdi32 ole32)

if(MSVC)
    target_compile_options(BlackFix_VideoShuffle PRIVATE /W4 /permissive- /EHsc)
endif()
