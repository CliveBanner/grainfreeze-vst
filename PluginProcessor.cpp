#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Parameter Layout Creation
//==============================================================================

juce::AudioProcessorValueTreeState::ParameterLayout GrainfreezeAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Time stretching factor (0.1 = 10x faster, 1.0 = normal, 4.0 = 4x slower)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("timeStretch", 1), "Time Stretch",
        juce::NormalisableRange<float>(0.1f, 4.0f, 0.01f, 0.5f),
        1.0f));

    // Grain size (reserved for future enhancements)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("grainSize", 1), "Grain Size",
        juce::NormalisableRange<float>(512.0f, 8192.0f, 1.0f, 0.3f),
        2048.0f));

    // Hop size as divisor of FFT size (lower = more overlap = smoother)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("hopSize", 1), "Hop Size",
        juce::NormalisableRange<float>(2.0f, 16.0f, 0.5f),
        4.0f));

    // FFT Size (larger = better frequency resolution, more latency)
    juce::StringArray fftSizeChoices;
    fftSizeChoices.add("512");
    fftSizeChoices.add("1024");
    fftSizeChoices.add("2048");
    fftSizeChoices.add("4096");
    fftSizeChoices.add("8192");
    fftSizeChoices.add("16384");
    fftSizeChoices.add("32768");
    fftSizeChoices.add("65536");

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("fftSize", 1), "FFT Size",
        fftSizeChoices,
        3));

    // Freeze mode toggle
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("freezeMode", 1), "Freeze Mode",
        false));

    // Glide time for freeze mode position changes (0-1000ms)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("glide", 1), "Glide",
        juce::NormalisableRange<float>(0.0f, 1000.0f, 1.0f, 0.5f),
        100.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("playheadPos", 1), "Playhead Position",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
        0.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("syncToDaw", 1), "Sync to DAW",
        false));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("loopStart", 1), "Loop Start",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("loopEnd", 1), "Loop End",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        1.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("pitchShift", 1), "Pitch Shift",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
        0.0f));

    // High-frequency boost to compensate for phase vocoder roll-off (0-100%)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("hfBoost", 1), "HF Boost",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        10.0f));

    // Micro-movement amount in freeze mode to reduce artifacts (0-100%)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("microMovement", 1), "Micro Movement",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        20.0f));

    // Window function type selection
    juce::StringArray windowChoices;
    windowChoices.add("Hann");
    windowChoices.add("Blackman-Harris");

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("windowType", 1), "Window Type",
        windowChoices,
        1));

    // Crossfade length for smooth playhead jumps (1-8 hops)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("crossfadeLength", 1), "Crossfade Length",
        juce::NormalisableRange<float>(1.0f, 8.0f, 0.5f),
        2.0f));

    return layout;
}

//==============================================================================
// Constructor
//==============================================================================

GrainfreezeAudioProcessor::GrainfreezeAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Retrieve parameter pointers from APVTS for easier access in audio thread
    timeStretch = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("timeStretch"));
    grainSizeParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("grainSize"));
    hopSizeParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("hopSize"));
    fftSizeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("fftSize"));
    freezeModeParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("freezeMode"));
    glideParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("glide"));
    playheadPosParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("playheadPos"));
    syncToDawParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("syncToDaw"));
    loopStartParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("loopStart"));
    loopEndParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("loopEnd"));
    pitchShiftParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("pitchShift"));
    hfBoostParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("hfBoost"));
    microMovementParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("microMovement"));
    windowTypeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("windowType"));
    crossfadeLengthParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("crossfadeLength"));

    lastPlayheadParam = playheadPosParam->get();

    updateFftSize();
}

GrainfreezeAudioProcessor::~GrainfreezeAudioProcessor()
{
}

//==============================================================================
// Plugin Info
//==============================================================================

const juce::String GrainfreezeAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool GrainfreezeAudioProcessor::acceptsMidi() const { return false; }
bool GrainfreezeAudioProcessor::producesMidi() const { return false; }
bool GrainfreezeAudioProcessor::isMidiEffect() const { return false; }
double GrainfreezeAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int GrainfreezeAudioProcessor::getNumPrograms() { return 1; }
int GrainfreezeAudioProcessor::getCurrentProgram() { return 0; }
void GrainfreezeAudioProcessor::setCurrentProgram(int index) {}
const juce::String GrainfreezeAudioProcessor::getProgramName(int index) { return {}; }
void GrainfreezeAudioProcessor::changeProgramName(int index, const juce::String& newName) {}

//==============================================================================
// Audio Processing Setup
//==============================================================================

void GrainfreezeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    playbackPosition = 0.0;
    outputWritePos = 0;

    // Initialize freeze mode smoothing
    smoothedFreezePosition.reset(sampleRate, 0.1);
    smoothedFreezePosition.setCurrentAndTargetValue(0.0);
    freezeCurrentPosition = 0.0;
    freezeTargetPosition = 0.0;
    freezeMicroMovement = 0.0f;
    freezeMicroCounter = 0;

    // Clear all processing buffers
    std::fill(outputAccum.begin(), outputAccum.end(), 0.0f);
    std::fill(previousPhase.begin(), previousPhase.end(), 0.0f);
    std::fill(synthesisPhase.begin(), synthesisPhase.end(), 0.0f);
}

void GrainfreezeAudioProcessor::releaseResources()
{
}

bool GrainfreezeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Only support stereo output
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

//==============================================================================
// Main Audio Processing
//==============================================================================

void GrainfreezeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();

    // Only sync if audio is loaded
    if (!audioLoaded)
        return;

    // --- DAW Transport Sync ---
    if (syncToDawParam->get())
    {
        if (auto* playHead = getPlayHead())
        {
            if (auto posInfo = playHead->getPosition())
            {
                bool isDawPlaying = posInfo->getIsPlaying();
                if (isDawPlaying != playing)
                {
                    setPlaying(isDawPlaying);
                }
            }
        }
    }

    // --- 1. Parameter -> Internal Sync ---
    // If the parameter was moved (DAW automation or UI), update internal state
    float currentParam = playheadPosParam->get();
    if (std::abs(currentParam - lastPlayheadParam) > 0.00001f)
    {
        setPlayheadPosition(currentParam);
        lastPlayheadParam = currentParam;
    }

    // --- 2. Process audio if playback is active ---
    if (playing)
    {
        processTimeStretch(buffer, buffer.getNumSamples());

        // --- 3. Internal -> Parameter Feedback ---
        // We only push internal position back to the parameter in NORMAL playback.
        // In FREEZE mode, the parameter is the TARGET, so we don't overwrite it
        // with the smoothed current position (which would cause a "snap back" fight).
        if (!freezeModeParam->get())
        {
            float actualPos = playheadPosition.load();
            *playheadPosParam = actualPos;
            lastPlayheadParam = actualPos;
        }
        else
        {
            // In freeze mode, just track the parameter value to avoid re-triggering jumps
            lastPlayheadParam = playheadPosParam->get();
        }
    }
}

//==============================================================================
// Time Stretching and Freeze Mode
//==============================================================================

void GrainfreezeAudioProcessor::processTimeStretch(juce::AudioBuffer<float>& outputBuffer, int numSamples)
{
    // Check if FFT size changed (requires buffer reallocation)
    static int lastFftSizeIndex = -1;
    bool fftSizeChanged = (fftSizeParam->getIndex() != lastFftSizeIndex);

    if (fftSizeChanged)
    {
        lastFftSizeIndex = fftSizeParam->getIndex();
        updateFftSize();
    }

    // Check if hop size changed (updates immediately for responsive control)
    static float lastHopSize = -1.0f;
    float currentHopParam = hopSizeParam->get();

    if (currentHopParam != lastHopSize)
    {
        lastHopSize = currentHopParam;
        updateHopSize();

        // Clear accumulator to prevent artifacts
        std::fill(outputAccum.begin(), outputAccum.end(), 0.0f);
        outputWritePos = 0;
        grainCounter = 0;
    }

    // Check if window type changed (updates window function)
    static int lastWindowType = -1;
    int currentWindowType = windowTypeParam->getIndex();

    if (currentWindowType != lastWindowType)
    {
        lastWindowType = currentWindowType;
        createWindow();
    }

    // Update freeze mode state
    isInFreezeMode = freezeModeParam->get();

    // Update glide time when changed
    static float lastGlideTime = -1.0f;
    float currentGlideTime = glideParam->get();
    if (currentGlideTime != lastGlideTime)
    {
        lastGlideTime = currentGlideTime;
        smoothedFreezePosition.reset(currentSampleRate, currentGlideTime / 1000.0);
    }

    // Get time stretch factor and calculate playback speed
    float stretch = timeStretch->get();
    float playbackSpeed = 1.0f / stretch;

    // Get loop boundaries in samples
    double numSamplesInAudio = static_cast<double>(loadedAudio.getNumSamples());
    double startSample = loopStartParam->get() * numSamplesInAudio;
    double endSample = loopEndParam->get() * numSamplesInAudio;

    // Ensure start < end and valid range
    if (startSample >= endSample)
    {
        if (startSample >= numSamplesInAudio - 1.0)
            startSample = numSamplesInAudio - 2.0;
        endSample = startSample + 1.0;
    }

    // FREEZE MODE processing
    if (isInFreezeMode)
    {
        // Constrain target position to loop boundaries
        double constrainedTarget = juce::jlimit(startSample, endSample, freezeTargetPosition);
        smoothedFreezePosition.setTargetValue(constrainedTarget);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            // Smoothly glide toward target position
            freezeCurrentPosition = smoothedFreezePosition.getNextValue();

            // Add micro-movement to reduce static artifacts
            freezeMicroCounter++;
            if (freezeMicroCounter >= currentHopSize / 4)
            {
                freezeMicroCounter = 0;
                // Scale random movement by parameter (0-100%)
                float movementAmount = microMovementParam->get() / 100.0f;
                freezeMicroMovement = (static_cast<float>(rand()) / RAND_MAX - 0.5f) *
                    0.0002f * movementAmount;
            }

            // Apply playback position with micro-movement
            playbackPosition = freezeCurrentPosition + (freezeMicroMovement * numSamplesInAudio);

            // Wrap position within loop boundaries
            if (playbackPosition >= endSample)
                playbackPosition = startSample + fmod(playbackPosition - startSample, endSample - startSample);
            if (playbackPosition < startSample)
                playbackPosition = startSample;

            // Process new grain when counter reaches zero
            if (grainCounter == 0)
            {
                performPhaseVocoder();
                grainCounter = currentHopSize;
            }

            // Read output sample from accumulator
            float outputSample = 0.0f;
            if (outputWritePos < outputAccum.size())
            {
                outputSample = outputAccum[outputWritePos];
                outputAccum[outputWritePos] = 0.0f;
            }

            // Write to all output channels
            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
                outputBuffer.setSample(ch, sample, outputSample);

            outputWritePos = (outputWritePos + 1) % outputAccum.size();
            grainCounter--;

            // Update UI playhead position (the UI uses this atomic for drawing the glide)
            playheadPosition.store(static_cast<float>(freezeCurrentPosition) / numSamplesInAudio);
        }
    }
    // NORMAL PLAYBACK MODE
    else
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            // Process new grain when counter reaches zero
            if (grainCounter == 0)
            {
                performPhaseVocoder();
                grainCounter = currentHopSize;
            }

            // Read output sample from accumulator
            float outputSample = 0.0f;
            if (outputWritePos < outputAccum.size())
            {
                outputSample = outputAccum[outputWritePos];
                outputAccum[outputWritePos] = 0.0f;
            }

            // Write to all output channels
            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
                outputBuffer.setSample(ch, sample, outputSample);

            outputWritePos = (outputWritePos + 1) % outputAccum.size();
            grainCounter--;

            // Advance playback position
            playbackPosition += playbackSpeed;

            // Wrap playback position within loop boundaries
            if (playbackPosition >= endSample)
                playbackPosition = startSample + fmod(playbackPosition - startSample, endSample - startSample);
            if (playbackPosition < startSample)
                playbackPosition = startSample;

            // Update UI playhead position
            playheadPosition.store(static_cast<float>(playbackPosition) / numSamplesInAudio);
        }
    }
}

//==============================================================================
// Phase Vocoder (main DSP algorithm)
//==============================================================================

void GrainfreezeAudioProcessor::performPhaseVocoder()
{
    if (!audioLoaded)
        return;

    // Calculate read position and clamp to valid bounds
    int readPos = static_cast<int>(playbackPosition);
    readPos = juce::jlimit(0, loadedAudio.getNumSamples() - currentFftSize, readPos);

    // Get source audio pointers
    const float* sourceData = loadedAudio.getReadPointer(0);

    // Mix stereo to mono for richer frequency content if available
    bool isStereo = loadedAudio.getNumChannels() > 1;
    const float* sourceDataR = isStereo ? loadedAudio.getReadPointer(1) : nullptr;

    // Fill analysis frame with windowed samples
    for (int i = 0; i < currentFftSize; ++i)
    {
        int sampleIndex = readPos + i;
        if (sampleIndex < loadedAudio.getNumSamples())
        {
            float sample = sourceData[sampleIndex];

            // Mix stereo to mono
            if (isStereo && sourceDataR != nullptr)
            {
                sample = (sample + sourceDataR[sampleIndex]) * 0.5f;
            }

            // Apply window function
            analysisFrame[i] = sample * window[i];
        }
        else
        {
            analysisFrame[i] = 0.0f;
        }
    }

    // Copy to FFT buffer
    std::copy(analysisFrame.begin(), analysisFrame.end(), fftBuffer.begin());

    // Forward FFT (time domain to frequency domain)
    fftAnalysis->performRealOnlyForwardTransform(fftBuffer.data(), true);

    // Capture spectrum magnitudes for visualization (resize if needed)
    int numBins = currentFftSize / 2 + 1;
    if (spectrumMagnitudes.size() != numBins)
        spectrumMagnitudes.resize(numBins);

    // Get time stretch factor
    float stretch = timeStretch->get();

    // Calculate expected phase advance per bin for one hop
    float expectedPhaseAdvance = juce::MathConstants<float>::twoPi * currentHopSize / currentFftSize;

    // Process each frequency bin to get magnitude and instantaneous frequency
    for (int bin = 0; bin <= currentFftSize / 2; ++bin)
    {
        // Extract real and imaginary components
        float real = fftBuffer[bin * 2];
        float imag = fftBuffer[bin * 2 + 1];

        // Calculate magnitude and phase
        float magnitude = std::sqrt(real * real + imag * imag);
        float phase = std::atan2(imag, real);

        // Calculate phase difference from previous frame
        float phaseDiff = phase - previousPhase[bin];
        previousPhase[bin] = phase;

        // Subtract expected phase advance to get deviation
        float deviation = phaseDiff - (bin * expectedPhaseAdvance);

        // Wrap deviation to [-π, π] range
        deviation = std::fmod(deviation + juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi);
        if (deviation < 0) deviation += juce::MathConstants<float>::twoPi;
        deviation -= juce::MathConstants<float>::pi;

        // Store magnitude and phase advance for resampling
        magnitudeBuffer[bin] = magnitude;
        phaseAdvanceBuffer[bin] = (bin * expectedPhaseAdvance) + deviation;
    }

    // --- Pitch Shifting Implementation ---
    float pitchShiftSemitones = pitchShiftParam->get();
    float pitchFactor = std::pow(2.0f, pitchShiftSemitones / 12.0f);

    // Clear synthesis buffer
    std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);

    for (int bin = 0; bin < numBins; ++bin)
    {
        // Source bin position after pitch shifting
        float sourceBin = static_cast<float>(bin) / pitchFactor;
        
        float magnitude = 0.0f;
        float phaseAdvance = 0.0f;

        if (sourceBin < static_cast<float>(numBins - 1))
        {
            int binLower = static_cast<int>(sourceBin);
            int binUpper = binLower + 1;
            float weightUpper = sourceBin - static_cast<float>(binLower);
            float weightLower = 1.0f - weightUpper;

            // Linear interpolation of magnitude and phase advance
            magnitude = (magnitudeBuffer[binLower] * weightLower) + (magnitudeBuffer[binUpper] * weightUpper);
            phaseAdvance = (phaseAdvanceBuffer[binLower] * weightLower) + (phaseAdvanceBuffer[binUpper] * weightUpper);
            
            // Re-scale phase advance for the target bin
            phaseAdvance *= pitchFactor;
        }

        // Apply high-frequency boost (scaled by parameter)
        float freqRatio = static_cast<float>(bin) / (numBins - 1);
        float hfBoostAmount = hfBoostParam->get() / 100.0f;
        float hfBoost = 1.0f + (freqRatio * hfBoostAmount);
        magnitude *= hfBoost;

        // Store magnitude for visualization
        spectrumMagnitudes[bin] = magnitude;

        // Advance synthesis phase (using the synthesis hop size = analysis hop size here)
        synthesisPhase[bin] += phaseAdvance;

        // Wrap synthesis phase to [-π, π]
        synthesisPhase[bin] = std::fmod(synthesisPhase[bin] + juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi);
        if (synthesisPhase[bin] < 0) synthesisPhase[bin] += juce::MathConstants<float>::twoPi;
        synthesisPhase[bin] -= juce::MathConstants<float>::pi;

        // Reconstruct complex spectrum
        fftBuffer[bin * 2] = magnitude * std::cos(synthesisPhase[bin]);
        fftBuffer[bin * 2 + 1] = magnitude * std::sin(synthesisPhase[bin]);
    }

    // Inverse FFT (frequency domain to time domain)
    fftSynthesis->performRealOnlyInverseTransform(fftBuffer.data());

    // Calculate normalization factor based on overlap
    float overlapFactor = static_cast<float>(currentFftSize) / currentHopSize;
    float normalization = 2.0f / overlapFactor;

    // Overlap-add to output accumulator with windowing and normalization
    for (int i = 0; i < currentFftSize; ++i)
    {
        int outIndex = (outputWritePos + i) % outputAccum.size();
        outputAccum[outIndex] += fftBuffer[i] * window[i] * normalization;
    }
}

//==============================================================================
// FFT Configuration
//==============================================================================

void GrainfreezeAudioProcessor::updateFftSize()
{
    // Map parameter index to actual FFT size
    int fftSizes[] = { 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536 };
    int selectedIndex = fftSizeParam->getIndex();
    currentFftSize = fftSizes[selectedIndex];

    // Update hop size based on new FFT size
    updateHopSize();

    // Calculate FFT order (log2 of size)
    int fftOrder = 0;
    int temp = currentFftSize;
    while (temp > 1)
    {
        temp >>= 1;
        fftOrder++;
    }

    // Create FFT objects
    fftAnalysis = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSynthesis = std::make_unique<juce::dsp::FFT>(fftOrder);

    // Resize all buffers
    fftBuffer.resize(currentFftSize * 2, 0.0f);
    analysisFrame.resize(currentFftSize, 0.0f);
    synthesisFrame.resize(currentFftSize, 0.0f);
    outputAccum.resize(currentFftSize * 8, 0.0f);
    crossfadeBuffer.resize(currentFftSize * 2, 0.0f);

    previousPhase.resize(currentFftSize / 2 + 1, 0.0f);
    synthesisPhase.resize(currentFftSize / 2 + 1, 0.0f);
    magnitudeBuffer.resize(currentFftSize / 2 + 1, 0.0f);
    phaseAdvanceBuffer.resize(currentFftSize / 2 + 1, 0.0f);
    window.resize(currentFftSize);

    // Create window function
    createWindow();

    // Reset all state
    std::fill(outputAccum.begin(), outputAccum.end(), 0.0f);
    std::fill(crossfadeBuffer.begin(), crossfadeBuffer.end(), 0.0f);
    std::fill(previousPhase.begin(), previousPhase.end(), 0.0f);
    std::fill(synthesisPhase.begin(), synthesisPhase.end(), 0.0f);
    std::fill(magnitudeBuffer.begin(), magnitudeBuffer.end(), 0.0f);
    std::fill(phaseAdvanceBuffer.begin(), phaseAdvanceBuffer.end(), 0.0f);

    outputWritePos = 0;
    grainCounter = 0;
    needsCrossfade = false;
    crossfadeCounter = 0;

    // Update crossfade length based on parameter
    crossfadeSamples = static_cast<int>(currentHopSize * crossfadeLengthParam->get());
}

void GrainfreezeAudioProcessor::updateHopSize()
{
    // Calculate hop size from FFT size and divisor parameter
    float hopDivisor = hopSizeParam->get();
    currentHopSize = static_cast<int>(currentFftSize / hopDivisor);

    // Ensure minimum hop size
    currentHopSize = juce::jmax(1, currentHopSize);
}

//==============================================================================
// Window Functions
//==============================================================================

void GrainfreezeAudioProcessor::createWindow()
{
    // Create window based on selected type
    int windowType = windowTypeParam->getIndex();

    if (windowType == 0)
    {
        createHannWindow();
    }
    else
    {
        createBlackmanHarrisWindow();
    }
}

void GrainfreezeAudioProcessor::createHannWindow()
{
    // Standard Hann window (good general purpose)
    for (int i = 0; i < currentFftSize; ++i)
    {
        float n = static_cast<float>(i) / (currentFftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n));
    }
}

void GrainfreezeAudioProcessor::createBlackmanHarrisWindow()
{
    // Blackman-Harris window (better frequency resolution)
    for (int i = 0; i < currentFftSize; ++i)
    {
        float n = static_cast<float>(i) / (currentFftSize - 1);

        // 4-term Blackman-Harris coefficients
        const float a0 = 0.35875f;
        const float a1 = 0.48829f;
        const float a2 = 0.14128f;
        const float a3 = 0.01168f;

        window[i] = a0
            - a1 * std::cos(2.0f * juce::MathConstants<float>::pi * n)
            + a2 * std::cos(4.0f * juce::MathConstants<float>::pi * n)
            - a3 * std::cos(6.0f * juce::MathConstants<float>::pi * n);
    }
}

//==============================================================================
// Audio File Loading
//==============================================================================

void GrainfreezeAudioProcessor::loadAudioFile(const juce::File& file)
{
    // Ensure file exists and isn't empty
    if (!file.existsAsFile())
        return;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));

    if (reader != nullptr)
    {
        // Thread-safe update: temporarily stop processing if needed, 
        // though here we just replace the buffer and flag
        juce::AudioBuffer<float> newBuffer;
        newBuffer.setSize(static_cast<int>(reader->numChannels),
                          static_cast<int>(reader->lengthInSamples));
        
        if (reader->read(&newBuffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true))
        {
            // Successfully read file, now swap into main buffer
            // Use a simple lock or just wait for next block since it's a pointer swap for the data usually
            // but for AudioBuffer we should be careful.
            
            // In a real plugin, we'd use a more sophisticated way to hand this to the audio thread.
            // For now, we'll just set it.
            loadedAudio.makeCopyOf(newBuffer);
            lastLoadedFileName = file.getFileName();
            
            // Reset playback state
            audioLoaded = true;
            playheadPosition.store(0.0f);
            playbackPosition = 0.0;
            freezeCurrentPosition = 0.0;
            freezeTargetPosition = 0.0;
            smoothedFreezePosition.setCurrentAndTargetValue(0.0);
            grainCounter = 0;

            // Reset phase vocoder state
            std::fill(previousPhase.begin(), previousPhase.end(), 0.0f);
            std::fill(synthesisPhase.begin(), synthesisPhase.end(), 0.0f);
        }
    }
}

//==============================================================================
// Playback Control
//==============================================================================

void GrainfreezeAudioProcessor::setPlayheadPosition(float normalizedPosition)
{
    float clampedPosition = juce::jlimit(0.0f, 1.0f, normalizedPosition);

    // In freeze mode, set target for smooth gliding
    if (isInFreezeMode || freezeModeParam->get())
    {
        freezeTargetPosition = clampedPosition * loadedAudio.getNumSamples();
    }
    else
    {
        // In normal mode, jump immediately
        playbackPosition = clampedPosition * loadedAudio.getNumSamples();
        playheadPosition.store(clampedPosition);
    }
}

void GrainfreezeAudioProcessor::setPlaying(bool shouldPlay)
{
    if (shouldPlay && !playing)
    {
        // Capture start position when playback begins
        playbackStartPosition = playbackPosition;
    }
    else if (!shouldPlay && playing)
    {
        // Reset to start position when playback stops
        playbackPosition = playbackStartPosition;
        
        // Also reset freeze positions to ensure consistent state
        freezeCurrentPosition = playbackStartPosition;
        freezeTargetPosition = playbackStartPosition;
        smoothedFreezePosition.setCurrentAndTargetValue(playbackStartPosition);
        
        // Update UI playhead and parameter to reflect the jump
        float normalizedPos = 0.0f;
        if (loadedAudio.getNumSamples() > 0)
            normalizedPos = static_cast<float>(playbackPosition) / loadedAudio.getNumSamples();
            
        playheadPosition.store(normalizedPos);
        
        // Notify parameter system of the jump
        if (playheadPosParam != nullptr)
        {
            playheadPosParam->beginChangeGesture();
            *playheadPosParam = normalizedPos;
            playheadPosParam->endChangeGesture();
        }
        lastPlayheadParam = normalizedPos;
    }

    playing = shouldPlay;
}

//==============================================================================
// Editor
//==============================================================================

bool GrainfreezeAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* GrainfreezeAudioProcessor::createEditor()
{
    return new GrainfreezeAudioProcessorEditor(*this);
}

//==============================================================================
// State Persistence
//==============================================================================

void GrainfreezeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Save all parameter states using APVTS
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void GrainfreezeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Restore all parameter states using APVTS
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
    {
        if (xmlState->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
        }
    }
}

//==============================================================================
// Plugin Factory
//==============================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GrainfreezeAudioProcessor();
}