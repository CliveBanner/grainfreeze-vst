#pragma once

#include <JuceHeader.h>
#include <vector>
#include <complex>
#include <map>

//==============================================================================
class GrainfreezeAudioProcessor;

/** A simple sound for our synthesiser. */
class GrainfreezeSound : public juce::SynthesiserSound
{
public:
    GrainfreezeSound() {}
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

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

    // Public for access and visualization
    double playbackPosition = 0.0;
    double freezeCurrentPosition = 0.0;
    float currentVelocity = 0.0f;
    juce::SmoothedValue<double> smoothedFreezePosition;
    
    float freezeMicroMovement = 0.0f;
    int freezeMicroCounter = 0;

    void allocateBuffers(int maxSize);

private:
    GrainfreezeAudioProcessor& processor;
    void performPhaseVocoder();

    std::unique_ptr<juce::dsp::FFT> fftAnalysis;
    std::unique_ptr<juce::dsp::FFT> fftSynthesis;
    int currentVoiceFftSize = 0;

    std::vector<float> previousPhase;
    std::vector<float> synthesisPhase;
    std::vector<float> outputAccum;
    int outputWritePos = 0;
    int grainCounter = 0;

    std::vector<float> analysisFrame;
    std::vector<float> fftBuffer;
    std::vector<float> magnitudeBuffer;
    std::vector<float> phaseAdvanceBuffer;
    
    juce::LinearSmoothedValue<float> envelope;
    bool isStopping = false;

    juce::Random random;
};

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
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override { juce::ignoreUnused(index); }
    const juce::String getProgramName(int index) override { juce::ignoreUnused(index); return {}; }
    void changeProgramName(int index, const juce::String& newName) override { juce::ignoreUnused(index, newName); }

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
    juce::AudioParameterFloat* midiStartPosParam;
    juce::AudioParameterFloat* midiEndPosParam;
    juce::AudioParameterFloat* attackParam;
    juce::AudioParameterFloat* releaseParam;

    const std::vector<float>& getWindow() const { return window; }
    void updateVoiceSpectrum(int bin, float magnitude);

    juce::Synthesiser synth;
    GrainfreezeVoice* getManualVoice();
    std::atomic<float> midiNoteStates[128];

    // Safe FFT access
    juce::dsp::FFT* getAnalysisFft() { return currentAnalysisFft; }
    juce::dsp::FFT* getSynthesisFft() { return currentSynthesisFft; }

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

    int lastFftSizeIndex = -1;
    float lastHopSizeValue = -1.0f;
    int lastWindowTypeIndex = -1;

    std::vector<float> spectrumMagnitudes;
    std::vector<float> window;

    static const int numFftSizes = 8;
    std::unique_ptr<juce::dsp::FFT> analysisFftObjects[numFftSizes];
    std::unique_ptr<juce::dsp::FFT> synthesisFftObjects[numFftSizes];
    juce::dsp::FFT* currentAnalysisFft = nullptr;
    juce::dsp::FFT* currentSynthesisFft = nullptr;

    void createWindow();
    void createHannWindow();
    void createBlackmanHarrisWindow();
    void updateGlobalFftSettings();
    void updateGlobalHopSize();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrainfreezeAudioProcessor)
};
