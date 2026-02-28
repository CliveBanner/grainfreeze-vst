cmake_minimum_required(VERSION 3.15)

project(GRAINFREEZE VERSION 0.0.1)

# ==============================================================================
# 1. Include FetchContent to download JUCE automatically
# ==============================================================================
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0 # Using latest stable version
)
FetchContent_MakeAvailable(JUCE)

# ==============================================================================
# 2. Create the plugin target
# ==============================================================================
juce_add_plugin(Grainfreeze
    # Name of the executable/bundle
    COMPANY_NAME "GeminiDev"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE "Gmni"
    PLUGIN_CODE "Grfz"
    FORMATS VST3 Standalone # Standalone is great for quick testing
)

# ==============================================================================
# 3. Add source files
# ==============================================================================
target_sources(Grainfreeze PRIVATE
    PluginProcessor.cpp
    PluginProcessor.h
    PluginEditor.cpp
    PluginEditor.h
)

# ==============================================================================
# 4. Generate JuceHeader.h
# This is required because your current code uses #include <JuceHeader.h>
# ==============================================================================
juce_generate_juce_header(Grainfreeze)

# ==============================================================================
# 5. Link necessary modules
# ==============================================================================
target_link_libraries(Grainfreeze PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ==============================================================================
# 6. Global configuration
# ==============================================================================
target_compile_definitions(Grainfreeze PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
