cmake_minimum_required(VERSION 3.22)

# Yda Facet 01 — native shell. Version of the app wrapper (separate from the Web screen's version).

# Build for both Apple Silicon and Intel Macs, and keep older macOS working (must come before project()).
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(YdaFacet VERSION 0.3.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.6
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# macOS downloads with its own system code; Linux (used for automatic tests) needs curl
if(APPLE)
    set(FACET_NEEDS_CURL FALSE)
else()
    set(FACET_NEEDS_CURL TRUE)
endif()

# App icon: Resources/icon.png is drawn from Resources/icon.svg by the build (see the workflow)
if(EXISTS "${CMAKE_SOURCE_DIR}/Resources/icon.png")
    set(FACET_ICON ICON_BIG "${CMAKE_SOURCE_DIR}/Resources/icon.png")
endif()

juce_add_gui_app(YdaFacet
    ${FACET_ICON}
    PRODUCT_NAME "Yda Facet 01"
    COMPANY_NAME "Yda"
    BUNDLE_ID "com.yda.facet01"
    NEEDS_WEB_BROWSER TRUE
    NEEDS_CURL ${FACET_NEEDS_CURL}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Facet uses the microphone or audio input when you record a sample.")

juce_generate_juce_header(YdaFacet)

target_sources(YdaFacet PRIVATE Source/Main.cpp)
if(APPLE)
    target_sources(YdaFacet PRIVATE Source/MacBits.mm)   # alert / confirm / prompt as Mac dialogs
endif()

target_compile_definitions(YdaFacet PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=$<IF:$<BOOL:${APPLE}>,0,1>
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:YdaFacet,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:YdaFacet,JUCE_VERSION>")

target_link_libraries(YdaFacet PRIVATE
    juce::juce_gui_extra
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Put the Web screen inside the app (Contents/Resources on macOS, next to the program elsewhere).
if(APPLE)
    add_custom_command(TARGET YdaFacet POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/web/facet.html"
                "$<TARGET_BUNDLE_CONTENT_DIR:YdaFacet>/Resources/facet.html")
else()
    add_custom_command(TARGET YdaFacet POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/web/facet.html"
                "$<TARGET_FILE_DIR:YdaFacet>/facet.html")
endif()
