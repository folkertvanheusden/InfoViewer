cmake_minimum_required(VERSION 3.7)

project(infoviewer)

add_compile_options(-Wall -ffast-math)

set(CMAKE_BUILD_TYPE Debug)

add_executable(
  infoviewer
  infoviewer.cpp
)
include(FindPkgConfig)

pkg_check_modules(SDL REQUIRED sdl)
target_link_libraries(infoviewer ${SDL_LIBRARIES})
target_include_directories(infoviewer PUBLIC ${SDL_INCLUDE_DIRS})
target_compile_options(infoviewer PUBLIC ${SDL_CFLAGS_OTHER})

pkg_check_modules(SDL_GFX REQUIRED SDL_gfx)
target_link_libraries(infoviewer ${SDL_GFX_LIBRARIES})
target_include_directories(infoviewer PUBLIC ${SDL_GFX_INCLUDE_DIRS})
target_compile_options(infoviewer PUBLIC ${SDL_GFX_CFLAGS_OTHER})

pkg_check_modules(SDL_TTF REQUIRED SDL_ttf)
target_link_libraries(infoviewer ${SDL_TTF_LIBRARIES})
target_include_directories(infoviewer PUBLIC ${SDL_TTF_INCLUDE_DIRS})
target_compile_options(infoviewer PUBLIC ${SDL_TTF_CFLAGS_OTHER})

pkg_check_modules(MOSQUITTO REQUIRED libmosquitto)
target_link_libraries(infoviewer ${MOSQUITTO_LIBRARIES})
target_include_directories(infoviewer PUBLIC ${MOSQUITTO_INCLUDE_DIRS})
target_compile_options(infoviewer PUBLIC ${MOSQUITTO_CFLAGS_OTHER})

pkg_check_modules(JANSSON REQUIRED jansson)
target_link_libraries(infoviewer ${JANSSON_LIBRARIES})
target_include_directories(infoviewer PUBLIC ${JANSSON_INCLUDE_DIRS})
target_compile_options(infoviewer PUBLIC ${JANSSON_CFLAGS_OTHER})
