cmake_minimum_required(VERSION 3.7)

project(infoviewer)

add_compile_options(-Wall -ffast-math -fno-inline)

set(CMAKE_BUILD_TYPE Debug)

add_executable(
  infoviewer
  error.cpp
  infoviewer.cpp
  io.cpp
  proc.cpp
  str.cpp
)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads)
target_link_libraries(infoviewer Threads::Threads)

include(FindPkgConfig)

pkg_check_modules(LIBCONFIG REQUIRED libconfig++)
target_link_libraries(infoviewer ${LIBCONFIG_LIBRARIES})
target_include_directories(infoviewer PUBLIC ${LIBCONFIG_INCLUDE_DIRS})
target_compile_options(infoviewer PUBLIC ${LIBCONFIG_CFLAGS_OTHER})

pkg_check_modules(SDL REQUIRED sdl2)
target_link_libraries(infoviewer ${SDL_LIBRARIES})
target_include_directories(infoviewer PUBLIC ${SDL_INCLUDE_DIRS})
target_compile_options(infoviewer PUBLIC ${SDL_CFLAGS_OTHER})

pkg_check_modules(SDL_GFX REQUIRED SDL2_gfx)
target_link_libraries(infoviewer ${SDL_GFX_LIBRARIES})
target_include_directories(infoviewer PUBLIC ${SDL_GFX_INCLUDE_DIRS})
target_compile_options(infoviewer PUBLIC ${SDL_GFX_CFLAGS_OTHER})

pkg_check_modules(SDL_IMAGE REQUIRED SDL2_image)
target_link_libraries(infoviewer ${SDL_IMAGE_LIBRARIES})
target_include_directories(infoviewer PUBLIC ${SDL_IMAGE_INCLUDE_DIRS})
target_compile_options(infoviewer PUBLIC ${SDL_IMAGE_CFLAGS_OTHER})

pkg_check_modules(SDL_TTF REQUIRED SDL2_ttf)
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

# set(CMAKE_INTERPROCEDURAL_OPTIMIZATION True)
set(CMAKE_CXX_FLAGS "-Ofast")

set(CMAKE_BUILD_TYPE Debug)
add_compile_options(-Wall -pedantic)
