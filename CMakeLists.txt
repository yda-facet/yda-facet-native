cmake_minimum_required(VERSION 3.22)

# Yda Facet 01 — native shell. Version of the app wrapper (separate from the Web screen's version).
project(YdaFacet VERSION 0.2.0)

# Build for both Apple Silicon and Intel Macs, and keep older macOS working.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

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

juce_add_gui_app(YdaFacet
    PRODUCT_NAME "Yda Facet 01"
    COMPANY_NAME "Yda"
    BUNDLE_ID "com.yda.facet01"
    NEEDS_WEB_BROWSER TRUE
    NEEDS_CURL ${FACET_NEEDS_CURL}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Facet uses the microphone or audio input when you record a sample.")

juce_generate_juce_header(YdaFacet)

target_sources(YdaFacet PRIVATE Source/Main.cpp)

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
