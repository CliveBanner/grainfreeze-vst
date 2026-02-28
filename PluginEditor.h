#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class WaveformDisplay : public juce::Component
{
public:
    WaveformDisplay(GrainfreezeAudioProcessor& p) : processor(p) {}
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    GrainfreezeAudioProcessor& processor;
    enum class DragMode { None, Playhead, LoopStart, LoopEnd };
    DragMode dragMode = DragMode::None;
    void updateFromMouse(const juce::MouseEvent& event);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};

//==============================================================================
class SpectrumVisualizer : public juce::Component
{
public:
    SpectrumVisualizer(GrainfreezeAudioProcessor& p) : processor(p) {}
    void paint(juce::Graphics& g) override;
    void updateSpectrum(const std::vector<float>& magnitudes, int fftSize, double sampleRate);

private:
    GrainfreezeAudioProcessor& processor;
    std::vector<float> noteMagnitudes;
    static const int numNotes = 88;
    static const int lowestNote = 21;
    int frequencyToMidiNote(float frequency);
    juce::String midiNoteToName(int midiNote);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumVisualizer)
};

//==============================================================================
class GrainfreezeAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    GrainfreezeAudioProcessorEditor(GrainfreezeAudioProcessor&);
    ~GrainfreezeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    GrainfreezeAudioProcessor& audioProcessor;

    WaveformDisplay waveformDisplay;
    SpectrumVisualizer spectrumVisualizer;

    juce::TextButton loadButton;
    juce::TextButton playButton;
    juce::TextButton freezeButton;
    juce::ToggleButton syncToDawButton;
    juce::TextButton midiModeButton;

    juce::Label statusLabel;
    juce::Label recommendedLabel;

    juce::Label primaryControlsLabel;
    juce::Label advancedControlsLabel;
    juce::Label midiControlsLabel;

    juce::Slider timeStretchSlider;
    juce::Label timeStretchLabel;
    juce::Slider fftSizeSlider;
    juce::Label fftSizeLabel;
    juce::Slider hopSizeSlider;
    juce::Label hopSizeLabel;
    juce::Slider glideSlider;
    juce::Label glideLabel;
    juce::Slider pitchShiftSlider;
    juce::Label pitchShiftLabel;

    juce::Slider hfBoostSlider;
    juce::Label hfBoostLabel;
    juce::Slider microMovementSlider;
    juce::Label microMovementLabel;
    juce::Slider windowTypeSlider;
    juce::Label windowTypeLabel;
    juce::Slider crossfadeLengthSlider;
    juce::Label crossfadeLengthLabel;

    juce::Slider midiStartPosSlider;
    juce::Label midiStartPosLabel;
    juce::Slider midiEndPosSlider;
    juce::Label midiEndPosLabel;
    
    juce::Slider attackSlider;
    juce::Label attackLabel;
    juce::Slider releaseSlider;
    juce::Label releaseLabel;

    std::unique_ptr<juce::FileChooser> fileChooser;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> timeStretchAttachment;
    std::unique_ptr<SliderAttachment> fftSizeAttachment;
    std::unique_ptr<SliderAttachment> hopSizeAttachment;
    std::unique_ptr<SliderAttachment> glideAttachment;
    std::unique_ptr<SliderAttachment> pitchShiftAttachment;
    std::unique_ptr<SliderAttachment> hfBoostAttachment;
    std::unique_ptr<SliderAttachment> microMovementAttachment;
    std::unique_ptr<SliderAttachment> windowTypeAttachment;
    std::unique_ptr<SliderAttachment> crossfadeLengthAttachment;
    std::unique_ptr<SliderAttachment> midiStartPosAttachment;
    std::unique_ptr<SliderAttachment> midiEndPosAttachment;
    std::unique_ptr<SliderAttachment> attackAttachment;
    std::unique_ptr<SliderAttachment> releaseAttachment;
    
    std::unique_ptr<ButtonAttachment> freezeModeAttachment;
    std::unique_ptr<ButtonAttachment> syncToDawAttachment;
    std::unique_ptr<ButtonAttachment> midiModeAttachment;

    void loadAudioFile();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrainfreezeAudioProcessorEditor)
};
