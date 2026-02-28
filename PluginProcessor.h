#pragma once

#include <JuceHeader.h>
#include <vector>
#include <complex>

//==============================================================================
/** A simple sound for our synthesiser. */
class GrainfreezeSound : public juce::SynthesiserSound
{
public:
    GrainfreezeSound() {}
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

class GrainfreezeAudioProcessor;

/** A single voice for our synthesiser. */
class GrainfreezeVoice : public juce::SynthesiserVoice
{
public:
    GrainfreezeVoice(GrainfreezeAudioProcessor& p);

    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

    // Per-voice state
    double playbackPosition = 0.0;
    double freezeCurrentPosition = 0.0;
    double freezeTargetPosition = 0.0;
    juce::SmoothedValue<double> smoothedFreezePosition;
    
    std::vector<float> previousPhase;
    std::vector<float> synthesisPhase;
    std::vector<float> outputAccum;
    int outputWritePos = 0;
    int grainCounter = 0;

    float freezeMicroMovement = 0.0f;
    int freezeMicroCounter = 0;
    float currentVelocity = 0.0f;

    // Per-voice DSP resources (Thread-safe)
    void updateFftSize(int newSize);
    std::unique_ptr<juce::dsp::FFT> fftAnalysis;
    std::unique_ptr<juce::dsp::FFT> fftSynthesis;
    std::vector<float> analysisFrame;
    std::vector<float> fftBuffer;
    std::vector<float> magnitudeBuffer;
    std::vector<float> phaseAdvanceBuffer;
    
    juce::Random random;

private:
    GrainfreezeAudioProcessor& processor;
    void performPhaseVocoder();
    int currentVoiceFftSize = 0;
};

//==============================================================================
// Grainfreeze Audio Processor
//==============================================================================

class GrainfreezeAudioProcessor : public juce::AudioProcessor
{
public:
    GrainfreezeAudioProcessor();
    ~GrainfreezeAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void loadAudioFile(const juce::File& file);
    const juce::AudioBuffer<float>& getLoadedAudio() const { return loadedAudio; }
    bool isAudioLoaded() const { return audioLoaded; }
    juce::String getLoadedFileName() const { return lastLoadedFileName; }

    void setPlayheadPosition(float normalizedPosition);
    float getPlayheadPosition() const { return playheadPosition.load(); }
    void setPlaying(bool shouldPlay);
    bool isPlaying() const { return playing; }

    const std::vector<float>& getSpectrumMagnitudes() const { return spectrumMagnitudes; }
    int getCurrentFftSize() const { return currentFftSize; }
    double getCurrentSampleRate() const { return currentSampleRate; }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioParameterFloat* timeStretch;
    juce::AudioParameterFloat* grainSizeParam;
    juce::AudioParameterFloat* hopSizeParam;
    juce::AudioParameterChoice* fftSizeParam;
    juce::AudioParameterBool* freezeModeParam;
    juce::AudioParameterFloat* glideParam;
    juce::AudioParameterFloat* playheadPosParam;
    juce::AudioParameterBool* syncToDawParam;
    juce::AudioParameterFloat* loopStartParam;
    juce::AudioParameterFloat* loopEndParam;
    juce::AudioParameterFloat* pitchShiftParam;

    juce::AudioParameterFloat* hfBoostParam;
    juce::AudioParameterFloat* microMovementParam;
    juce::AudioParameterChoice* windowTypeParam;
    juce::AudioParameterFloat* crossfadeLengthParam;

    juce::AudioParameterBool* midiModeParam;
    juce::AudioParameterFloat* midiPosMinParam;
    juce::AudioParameterFloat* midiPosCenterParam;
    juce::AudioParameterFloat* midiPosMaxParam;

    const std::vector<float>& getWindow() const { return window; }
    std::vector<float>& getSpectrumMagnitudesRef() { return spectrumMagnitudes; }

    juce::Synthesiser synth;
    GrainfreezeVoice* getManualVoice();
    std::atomic<float> midiNoteStates[128];

private:
    juce::AudioBuffer<float> loadedAudio;
    bool audioLoaded = false;
    juce::String lastLoadedFileName;

    std::atomic<float> playheadPosition{ 0.0f };
    bool playing = false;
    double playbackPosition = 0.0;
    double playbackStartPosition = 0.0;
    float lastPlayheadParam = -1.0f;

    bool isInFreezeMode = false;
    double freezeTargetPosition = 0.0;
    double freezeCurrentPosition = 0.0;
    juce::SmoothedValue<double> smoothedFreezePosition;

    int currentFftSize = 4096;
    int currentHopSize = 512;
    double currentSampleRate = 44100.0;

    // Track parameter changes without statics
    int lastFftSizeIndex = -1;
    float lastHopSizeValue = -1.0f;
    int lastWindowTypeIndex = -1;

    std::vector<float> spectrumMagnitudes;
    std::vector<float> window;

    void createWindow();
    void createHannWindow();
    void createBlackmanHarrisWindow();
    void updateGlobalFftSettings();
    void updateGlobalHopSize();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrainfreezeAudioProcessor)
};
