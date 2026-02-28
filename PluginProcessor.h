#pragma once

#include <JuceHeader.h>
#include <vector>
#include <complex>

//==============================================================================
// Grainfreeze Audio Processor
// Phase vocoder-based time stretching with freeze mode
//==============================================================================

class GrainfreezeAudioProcessor : public juce::AudioProcessor
{
public:
    GrainfreezeAudioProcessor();
    ~GrainfreezeAudioProcessor() override;

    //==============================================================================
    // Audio Processing

    // Called before playback starts to initialize at given sample rate and buffer size
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;

    // Called when playback stops to release resources
    void releaseResources() override;

    // Checks if a given channel layout configuration is supported
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // Main audio processing callback - processes one block of audio
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    // Editor

    // Creates the plugin's graphical user interface
    juce::AudioProcessorEditor* createEditor() override;

    // Returns true since this plugin has a GUI
    bool hasEditor() const override;

    //==============================================================================
    // Plugin Info

    // Returns the plugin name
    const juce::String getName() const override;

    // Returns true - this plugin now accepts MIDI
    bool acceptsMidi() const override { return true; }

    // Returns false - this plugin doesn't produce MIDI
    bool producesMidi() const override;

    // Returns false - this plugin is not a MIDI effect
    bool isMidiEffect() const override;

    // Returns 0 - this plugin has no tail
    double getTailLengthSeconds() const override;

    //==============================================================================
    // Program/Preset Management

    // Returns number of preset programs (always 1)
    int getNumPrograms() override;

    // Returns current program index (always 0)
    int getCurrentProgram() override;

    // Sets current program (no-op)
    void setCurrentProgram(int index) override;

    // Returns program name at index
    const juce::String getProgramName(int index) override;

    // Changes program name (no-op)
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    // State Persistence

    // Saves plugin state to memory block for DAW project save
    void getStateInformation(juce::MemoryBlock& destData) override;

    // Restores plugin state from memory block when DAW project loads
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // Audio File Management

    // Loads an audio file into the internal buffer
    void loadAudioFile(const juce::File& file);

    // Returns reference to loaded audio buffer
    const juce::AudioBuffer<float>& getLoadedAudio() const { return loadedAudio; }

    // Returns true if audio file is loaded
    bool isAudioLoaded() const { return audioLoaded; }

    // Returns the name of the loaded file
    juce::String getLoadedFileName() const { return lastLoadedFileName; }

    //==============================================================================
    // Playback Control

    // Sets playhead position (0.0 to 1.0 normalized)
    void setPlayheadPosition(float normalizedPosition);

    // Gets current playhead position (0.0 to 1.0 normalized)
    float getPlayheadPosition() const { return playheadPosition.load(); }

    // Starts or stops playback
    void setPlaying(bool shouldPlay);

    // Returns true if currently playing
    bool isPlaying() const { return playing; }

    //==============================================================================
    // Spectrum Data Access

    // Returns the current FFT magnitudes for visualization
    const std::vector<float>& getSpectrumMagnitudes() const { return spectrumMagnitudes; }

    // Returns current FFT size
    int getCurrentFftSize() const { return currentFftSize; }

    // Returns current sample rate
    double getCurrentSampleRate() const { return currentSampleRate; }

    //==============================================================================
    // Parameters - Primary Controls

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioParameterFloat* timeStretch;      // Time stretching factor (0.1-4.0)
    juce::AudioParameterFloat* grainSizeParam;   // Grain size (reserved for future use)
    juce::AudioParameterFloat* hopSizeParam;     // Hop size divisor (2.0-16.0)
    juce::AudioParameterChoice* fftSizeParam;    // FFT window size (512-65536)
    juce::AudioParameterBool* freezeModeParam;   // Freeze mode on/off
    juce::AudioParameterFloat* glideParam;       // Freeze mode glide time (0-1000ms)
    juce::AudioParameterFloat* playheadPosParam; // Playhead position (0.0-1.0)
    juce::AudioParameterBool* syncToDawParam;    // Sync to DAW playback state
    juce::AudioParameterFloat* loopStartParam;   // Loop start position (0.0-1.0)
    juce::AudioParameterFloat* loopEndParam;     // Loop end position (0.0-1.0)
    juce::AudioParameterFloat* pitchShiftParam;  // Pitch shift in semitones (-24 to +24)

    // Parameters - Advanced Controls
    juce::AudioParameterFloat* hfBoostParam;         // High-frequency boost 0-100%
    juce::AudioParameterFloat* microMovementParam;   // Freeze micro-movement 0-100%
    juce::AudioParameterChoice* windowTypeParam;     // Window function type
    juce::AudioParameterFloat* crossfadeLengthParam; // Crossfade length 1-8 hops

    // Parameters - MIDI Controls
    juce::AudioParameterBool* midiModeParam;         // Toggle MIDI-triggered polyphony
    juce::AudioParameterFloat* midiPosMinParam;      // Position for MIDI note 0
    juce::AudioParameterFloat* midiPosCenterParam;   // Position for MIDI note 60
    juce::AudioParameterFloat* midiPosMaxParam;      // Position for MIDI note 127

    // Voice structure for polyphony
    struct Voice
    {
        bool isActive = false;
        int midiNote = -1;
        float velocity = 0.0f;
        
        double playbackPosition = 0.0;
        double freezeCurrentPosition = 0.0;
        double freezeTargetPosition = 0.0;
        juce::SmoothedValue<double> smoothedFreezePosition;
        
        std::vector<float> previousPhase;
        std::vector<float> synthesisPhase;
        std::vector<float> outputAccum;
        int outputWritePos = 0;
        int grainCounter = 0;

        // Micro-movement state per voice
        float freezeMicroMovement = 0.0f;
        int freezeMicroCounter = 0;
    };

    static const int maxVoices = 16;
    std::array<Voice, maxVoices> voices;

    // Helper to find an available or existing voice
    Voice* findVoice(int midiNote);
    Voice* findFreeVoice();

private:
    //==============================================================================
    // Audio Data

    juce::AudioBuffer<float> loadedAudio;  // Buffer containing loaded audio file
    bool audioLoaded = false;              // Flag indicating if audio is loaded
    juce::String lastLoadedFileName;       // Name of the loaded audio file

    //==============================================================================
    // Playback State

    std::atomic<float> playheadPosition{ 0.0f };  // Thread-safe playhead for UI
    bool playing = false;                          // Playback active flag
    double playbackPosition = 0.0;                 // Current playback position in samples
    double playbackStartPosition = 0.0;            // Position where playback last started
    float lastPlayheadParam = -1.0f;               // Tracker for parameter changes

    //==============================================================================
    // Freeze Mode State

    bool isInFreezeMode = false;                          // Current freeze mode state
    double freezeTargetPosition = 0.0;                    // Target position for gliding
    double freezeCurrentPosition = 0.0;                   // Current smoothed position
    juce::SmoothedValue<double> smoothedFreezePosition;   // Smoother for glide effect

    float freezeMicroMovement = 0.0f;  // Current micro-movement offset
    int freezeMicroCounter = 0;        // Counter for micro-movement updates

    //==============================================================================
    // Phase Vocoder Configuration

    int currentFftSize = 4096;      // Current FFT size in samples
    int currentHopSize = 512;       // Current hop size in samples
    double currentSampleRate = 44100.0;  // Current sample rate

    // FFT Processing Objects
    std::unique_ptr<juce::dsp::FFT> fftAnalysis;   // FFT object for analysis
    std::unique_ptr<juce::dsp::FFT> fftSynthesis;  // FFT object for synthesis

    // Processing Buffers
    std::vector<float> analysisFrame;     // Windowed input frame for FFT
    std::vector<float> synthesisFrame;    // Output frame from IFFT
    std::vector<float> fftBuffer;         // FFT work buffer (real/imag interleaved)
    std::vector<float> magnitudeBuffer;    // Magnitudes for pitch shifting
    std::vector<float> phaseAdvanceBuffer; // Phase advances for pitch shifting

    // Spectrum Data for Visualization
    std::vector<float> spectrumMagnitudes; // Current FFT magnitudes

    // Window Function
    std::vector<float> window;            // Window coefficients (Hann or Blackman-Harris)

    // Crossfade State (for smooth playhead jumps)
    bool needsCrossfade = false;  // Flag indicating crossfade in progress
    int crossfadeCounter = 0;     // Current position in crossfade
    int crossfadeSamples = 0;     // Total length of crossfade in samples

    // Shared crossfade/utility buffer
    std::vector<float> crossfadeBuffer;

    //==============================================================================
    // Internal Processing Methods

    // Processes time stretching or freeze mode for a block of samples
    void processTimeStretch(juce::AudioBuffer<float>& outputBuffer, int numSamples);

    // Performs phase vocoder analysis/synthesis on one grain for a specific voice
    void performPhaseVocoder(Voice& voice);

    // Creates window function based on current window type parameter
    void createWindow();

    // Creates Hann window (good general purpose)
    void createHannWindow();

    // Creates Blackman-Harris window (better frequency resolution)
    void createBlackmanHarrisWindow();

    // Updates FFT size and reallocates all related buffers
    void updateFftSize();

    // Updates hop size immediately when parameter changes
    void updateHopSize();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrainfreezeAudioProcessor)
};