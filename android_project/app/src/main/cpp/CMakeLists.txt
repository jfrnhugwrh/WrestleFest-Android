cmake_minimum_required(VERSION 3.16)
project(wrestlefest_native LANGUAGES CXX)

add_library(viewport_manager STATIC viewport_manager.cpp)
target_include_directories(viewport_manager PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(viewport_manager PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# Simulation: data tables + rules. No GL, no Android - host testable.
# ---------------------------------------------------------------------------
add_library(wrestlefest_sim STATIC
    data/asset_source.cpp
    data/game_data.cpp
    data/sml_parser.cpp
    sim/ai.cpp
    sim/collision.cpp
    sim/match.cpp
    sim/referee.cpp
    sim/wrestler.cpp
)
target_include_directories(wrestlefest_sim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(wrestlefest_sim PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# Rendering: sprite batch, decoders, game renderer and the app shell.
# Needs GLES3 + zlib (deflate in the asset archives).
# ---------------------------------------------------------------------------
if(ANDROID)
add_library(wrestlefest_render STATIC
    game/app.cpp
    game/game_renderer.cpp
    game/menu_screens.cpp
    render/gl_sprite.cpp
    render/image.cpp
    render/zip_reader.cpp
)
target_include_directories(wrestlefest_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(wrestlefest_render PUBLIC cxx_std_17)
target_link_libraries(wrestlefest_render PUBLIC wrestlefest_sim GLESv3 android log z)
endif()

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
        wrestlefest_render
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
option(VIEWPORT_BUILD_TESTS "Build the standalone host tests" ${VIEWPORT_TESTS_DEFAULT})

if(VIEWPORT_BUILD_TESTS)
    enable_testing()
    add_executable(viewport_manager_test viewport_manager_test.cpp)
    target_link_libraries(viewport_manager_test PRIVATE viewport_manager)
    add_test(NAME viewport_manager_test COMMAND viewport_manager_test)

    # The simulation is deterministic and GL-free, so the whole ruleset can be
    # exercised on the build machine (GAMEPLAY_IMPLEMENTATION 9, phase 1-2).
    add_executable(sim_test sim/sim_test.cpp)
    target_link_libraries(sim_test PRIVATE wrestlefest_sim)
    target_compile_definitions(sim_test PRIVATE
        WRESTLEFEST_DATA_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../main/assets/data/Data")
    add_test(NAME sim_test COMMAND sim_test)

    # .sml parsing + typed tables.
    add_executable(data_test data/data_test.cpp)
    target_link_libraries(data_test PRIVATE wrestlefest_sim)
    target_compile_definitions(data_test PRIVATE
        WRESTLEFEST_DATA_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../main/assets/data/Data")
    add_test(NAME data_test COMMAND data_test)

    # PNG / SST decoding against the shipped art.
    # GlSprite needs GLES3 headers, so the decoders are built straight into the
    # test instead of pulling in the renderer library.
    add_executable(image_test render/image_test.cpp render/image.cpp render/zip_reader.cpp
        data/asset_source.cpp data/game_data.cpp data/sml_parser.cpp)
    target_link_libraries(image_test PRIVATE z)
    include(CheckIncludeFileCXX)
    check_include_file_cxx("zlib.h" HAVE_ZLIB_H)
    if(NOT HAVE_ZLIB_H)
        # The build machine ships libz.so without headers; a declaring-only
        # shim is enough to link against the real library.
        target_include_directories(image_test PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../../tools/host_check")
    endif()
    target_compile_definitions(image_test PRIVATE
        WRESTLEFEST_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../main/assets")
    add_test(NAME image_test COMMAND image_test)
endif()
