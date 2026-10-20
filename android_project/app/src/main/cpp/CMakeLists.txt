cmake_minimum_required(VERSION 3.16)
project(wrestlefest_native LANGUAGES CXX)

add_library(viewport_manager STATIC viewport_manager.cpp)
target_include_directories(viewport_manager PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(viewport_manager PUBLIC cxx_std_17)

if(ANDROID)
    find_package(game-activity REQUIRED CONFIG)

    add_library(wrestlefest SHARED
        asset_loader.cpp
        audio_player.cpp
        main.cpp
        obb_archive.cpp
    )
    target_compile_features(wrestlefest PRIVATE cxx_std_17)
    target_compile_options(wrestlefest PRIVATE -g -fno-omit-frame-pointer)
    # Keep GameActivity JNI entry points from being stripped by --gc-sections.
    # Without these, GameActivity.initializeNativeCode() throws UnsatisfiedLinkError
    # immediately after onCreate. See https://github.com/android-ndk/ndk/issues/381
    target_link_options(wrestlefest PRIVATE
        "-Wl,-u,GameActivity_onCreate"
        "-Wl,-u,Java_com_google_androidgamesdk_GameActivity_initializeNativeCode"
    )
    target_link_libraries(wrestlefest PRIVATE
        viewport_manager
        game-activity::game-activity_static
        EGL
        GLESv3
        OpenSLES
        android
        log
        z
    )

    set(VIEWPORT_TESTS_DEFAULT OFF)
else()
    set(VIEWPORT_TESTS_DEFAULT ON)
endif()
option(VIEWPORT_BUILD_TESTS "Build the standalone viewport test executable" ${VIEWPORT_TESTS_DEFAULT})

if(VIEWPORT_BUILD_TESTS)
    enable_testing()
    add_executable(viewport_manager_test viewport_manager_test.cpp)
    target_link_libraries(viewport_manager_test PRIVATE viewport_manager)
    add_test(NAME viewport_manager_test COMMAND viewport_manager_test)
endif()
