#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// GrainfreezeVoice Implementation
//==============================================================================

GrainfreezeVoice::GrainfreezeVoice(GrainfreezeAudioProcessor& p) : processor(p)
{
    smoothedFreezePosition.reset(p.getCurrentSampleRate(), 0.1);
}

bool GrainfreezeVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<GrainfreezeSound*>(sound) != nullptr;
}

void GrainfreezeVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int /*currentPitchWheelPosition*/)
{
    currentVelocity = velocity;
    
    double numSamplesInAudio = static_cast<double>(processor.getLoadedAudio().getNumSamples());
    float minPos = processor.midiPosMinParam->get();
    float centerPos = processor.midiPosCenterParam->get();
    float maxPos = processor.midiPosMaxParam->get();
    
    float pos = (midiNoteNumber < 60) ? juce::jmap(static_cast<float>(midiNoteNumber), 0.0f, 60.0f, minPos, centerPos)
                                      : juce::jmap(static_cast<float>(midiNoteNumber), 60.0f, 127.0f, centerPos, maxPos);
    
    double samplePos = static_cast<double>(pos) * numSamplesInAudio;
    
    // For manual mode (dummy note 60), use current processor position
    if (!processor.midiModeParam->get() && midiNoteNumber == 60)
        samplePos = static_cast<double>(processor.getPlayheadPosition()) * numSamplesInAudio;

    playbackPosition = samplePos;
    freezeTargetPosition = samplePos;
    freezeCurrentPosition = samplePos;
    smoothedFreezePosition.setCurrentAndTargetValue(samplePos);
    
    int fftSize = processor.getCurrentFftSize();
    previousPhase.assign(static_cast<size_t>(fftSize / 2 + 1), 0.0f);
    synthesisPhase.assign(static_cast<size_t>(fftSize / 2 + 1), 0.0f);
    outputAccum.assign(static_cast<size_t>(fftSize * 8), 0.0f);
    outputWritePos = 0;
    grainCounter = 0;
}

void GrainfreezeVoice::stopNote(float /*velocity*/, bool allowTailOff)
{
    juce::ignoreUnused(allowTailOff);
    clearCurrentNote();
}

void GrainfreezeVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!processor.isAudioLoaded()) return;

    double numSamplesInAudio = static_cast<double>(processor.getLoadedAudio().getNumSamples());
    double startLim = static_cast<double>(processor.loopStartParam->get()) * numSamplesInAudio;
    double endLim = static_cast<double>(processor.loopEndParam->get()) * numSamplesInAudio;
    if (startLim >= endLim) startLim = std::max(0.0, endLim - 1.0);

    int fftSize = processor.getCurrentFftSize();
    int currentHopSize = fftSize / static_cast<int>(processor.hopSizeParam->get());
    if (currentHopSize < 1) currentHopSize = 1;

    bool isMidiMode = processor.midiModeParam->get();
    bool isFreeze = processor.freezeModeParam->get();
    float speed = 1.0f / std::max(0.1f, processor.timeStretch->get());

    // Update target position in real-time from parameters
    if (isMidiMode)
    {
        int note = getCurrentlyPlayingNote();
        float minPos = processor.midiPosMinParam->get();
        float centerPos = processor.midiPosCenterParam->get();
        float maxPos = processor.midiPosMaxParam->get();
        
        float pos = (note < 60) ? juce::jmap(static_cast<float>(note), 0.0f, 60.0f, minPos, centerPos)
                                : juce::jmap(static_cast<float>(note), 60.0f, 127.0f, centerPos, maxPos);
        
        smoothedFreezePosition.setTargetValue(juce::jlimit(startLim, endLim, static_cast<double>(pos) * numSamplesInAudio));
    }
    else if (isFreeze)
    {
        smoothedFreezePosition.setTargetValue(juce::jlimit(startLim, endLim, static_cast<double>(processor.getPlayheadPosition()) * numSamplesInAudio));
    }

    for (int sampleIdx = 0; sampleIdx < numSamples; ++sampleIdx)
    {
        if (isMidiMode || isFreeze)
        {
            freezeCurrentPosition = smoothedFreezePosition.getNextValue();
            
            // Micro-movement
            freezeMicroCounter++;
            if (freezeMicroCounter >= currentHopSize / 4)
            {
                freezeMicroCounter = 0;
                freezeMicroMovement = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 0.0002f * (processor.microMovementParam->get() / 100.0f);
            }
            playbackPosition = juce::jlimit(startLim, endLim, freezeCurrentPosition + (static_cast<double>(freezeMicroMovement) * numSamplesInAudio));
        }
        else
        {
            playbackPosition += static_cast<double>(speed);
            if (playbackPosition >= endLim) playbackPosition = startLim + std::fmod(playbackPosition - startLim, endLim - startLim);
            if (playbackPosition < startLim) playbackPosition = startLim;
        }

        if (grainCounter <= 0)
        {
            performPhaseVocoder();
            grainCounter = currentHopSize;
        }

        float outS = 0.0f;
        if (outputWritePos < static_cast<int>(outputAccum.size()))
        {
            outS = outputAccum[static_cast<size_t>(outputWritePos)] * currentVelocity;
            outputAccum[static_cast<size_t>(outputWritePos)] = 0.0f;
        }

        for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
            outputBuffer.addSample(ch, startSample + sampleIdx, outS);

        outputWritePos = (outputWritePos + 1) % static_cast<int>(outputAccum.size());
        grainCounter--;
    }
}

void GrainfreezeVoice::performPhaseVocoder()
{
    const auto& audio = processor.getLoadedAudio();
    int fftSize = processor.getCurrentFftSize();
    int hopSize = fftSize / static_cast<int>(processor.hopSizeParam->get());
    if (hopSize < 1) hopSize = 1;

    int numBins = fftSize / 2 + 1;
    int readPos = juce::jlimit(0, audio.getNumSamples() - fftSize, static_cast<int>(playbackPosition));
    
    auto& analysisFrame = processor.getAnalysisFrame();
    auto& fftBuffer = processor.getFftBuffer();
    const auto& window = processor.getWindow();

    const float* src = audio.getReadPointer(0);
    bool isSt = audio.getNumChannels() > 1;
    const float* srcR = isSt ? audio.getReadPointer(1) : nullptr;

    for (int i = 0; i < fftSize; ++i)
    {
        int idx = readPos + i;
        float s = (idx < audio.getNumSamples()) ? src[idx] : 0.0f;
        if (isSt && srcR && idx < audio.getNumSamples()) s = (s + srcR[idx]) * 0.5f;
        analysisFrame[static_cast<size_t>(i)] = s * window[static_cast<size_t>(i)];
    }

    std::copy(analysisFrame.begin(), analysisFrame.begin() + fftSize, fftBuffer.begin());
    processor.getAnalysisFft()->performRealOnlyForwardTransform(fftBuffer.data(), true);

    auto& spectrumMags = processor.getSpectrumMagnitudesRef();
    if (spectrumMags.size() != static_cast<size_t>(numBins)) spectrumMags.resize(static_cast<size_t>(numBins));

    float expPhaseAdv = juce::MathConstants<float>::twoPi * static_cast<float>(hopSize) / static_cast<float>(fftSize);
    auto& magBuf = processor.getMagnitudeBuffer();
    auto& phAdvBuf = processor.getPhaseAdvanceBuffer();

    for (int bin = 0; bin < numBins; ++bin)
    {
        float real = fftBuffer[static_cast<size_t>(bin * 2)], imag = fftBuffer[static_cast<size_t>(bin * 2 + 1)];
        float mag = std::sqrt(real * real + imag * imag), ph = std::atan2(imag, real);
        float d = (ph - previousPhase[static_cast<size_t>(bin)]) - (static_cast<float>(bin) * expPhaseAdv);
        previousPhase[static_cast<size_t>(bin)] = ph;
        d = std::fmod(d + juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi);
        if (d < 0) d += juce::MathConstants<float>::twoPi;
        magBuf[static_cast<size_t>(bin)] = mag;
        phAdvBuf[static_cast<size_t>(bin)] = (static_cast<float>(bin) * expPhaseAdv) + (d - juce::MathConstants<float>::pi);
    }

    float pf = std::pow(2.0f, processor.pitchShiftParam->get() / 12.0f);
    std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
    for (int bin = 0; bin < numBins; ++bin)
    {
        float srcBin = static_cast<float>(bin) / pf;
        float mag = 0.0f, phAdv = 0.0f;
        if (srcBin < static_cast<float>(numBins - 1))
        {
            int bL = static_cast<int>(srcBin); float wU = srcBin - static_cast<float>(bL);
            mag = (magBuf[static_cast<size_t>(bL)] * (1.0f - wU)) + (magBuf[static_cast<size_t>(bL + 1)] * wU);
            phAdv = (phAdvBuf[static_cast<size_t>(bL)] * (1.0f - wU)) + (phAdvBuf[static_cast<size_t>(bL + 1)] * wU);
            phAdv *= pf;
        }
        mag *= (1.0f + (static_cast<float>(bin) / static_cast<float>(numBins - 1) * (processor.hfBoostParam->get() / 100.0f)));
        
        if (this == processor.synth.getVoice(0) || processor.midiModeParam->get())
            spectrumMags[static_cast<size_t>(bin)] = mag;

        synthesisPhase[static_cast<size_t>(bin)] += phAdv;
        synthesisPhase[static_cast<size_t>(bin)] = std::fmod(synthesisPhase[static_cast<size_t>(bin)] + juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi);
        if (synthesisPhase[static_cast<size_t>(bin)] < 0) synthesisPhase[static_cast<size_t>(bin)] += juce::MathConstants<float>::twoPi;
        synthesisPhase[static_cast<size_t>(bin)] -= juce::MathConstants<float>::pi;
        fftBuffer[static_cast<size_t>(bin * 2)] = mag * std::cos(synthesisPhase[static_cast<size_t>(bin)]);
        fftBuffer[static_cast<size_t>(bin * 2 + 1)] = mag * std::sin(synthesisPhase[static_cast<size_t>(bin)]);
    }

    processor.getSynthesisFft()->performRealOnlyInverseTransform(fftBuffer.data());
    float norm = 2.0f / (static_cast<float>(fftSize) / static_cast<float>(hopSize));
    for (int i = 0; i < fftSize; ++i)
    {
        int outIdx = (outputWritePos + i) % static_cast<int>(outputAccum.size());
        outputAccum[static_cast<size_t>(outIdx)] += fftBuffer[static_cast<size_t>(i)] * window[static_cast<size_t>(i)] * norm;
    }
}

//==============================================================================
// GrainfreezeAudioProcessor Implementation
//==============================================================================

juce::AudioProcessorValueTreeState::ParameterLayout GrainfreezeAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("timeStretch", 1), "Time Stretch", juce::NormalisableRange<float>(0.1f, 4.0f, 0.01f, 0.5f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("grainSize", 1), "Grain Size", juce::NormalisableRange<float>(512.0f, 8192.0f, 1.0f, 0.3f), 2048.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("hopSize", 1), "Hop Size", juce::NormalisableRange<float>(2.0f, 16.0f, 0.5f), 4.0f));
    juce::StringArray fs; fs.add("512"); fs.add("1024"); fs.add("2048"); fs.add("4096"); fs.add("8192"); fs.add("16384"); fs.add("32768"); fs.add("65536");
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("fftSize", 1), "FFT Size", fs, 3));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("freezeMode", 1), "Freeze Mode", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("glide", 1), "Glide", juce::NormalisableRange<float>(0.0f, 1000.0f, 1.0f, 0.5f), 100.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("playheadPos", 1), "Playhead Position", juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("syncToDaw", 1), "Sync to DAW", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("loopStart", 1), "Loop Start", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("loopEnd", 1), "Loop End", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("pitchShift", 1), "Pitch Shift", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("hfBoost", 1), "HF Boost", juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 10.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("microMovement", 1), "Micro Movement", juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 20.0f));
    juce::StringArray wc; wc.add("Hann"); wc.add("Blackman-Harris");
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("windowType", 1), "Window Type", wc, 1));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("crossfadeLength", 1), "Crossfade Length", juce::NormalisableRange<float>(1.0f, 8.0f, 0.5f), 2.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("midiMode", 1), "MIDI Mode", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("midiPosMin", 1), "MIDI Min Pos", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("midiPosCenter", 1), "MIDI Center Pos (C4)", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("midiPosMax", 1), "MIDI Max Pos", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
    return layout;
}

GrainfreezeAudioProcessor::GrainfreezeAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
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
    midiModeParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("midiMode"));
    midiPosMinParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("midiPosMin"));
    midiPosCenterParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("midiPosCenter"));
    midiPosMaxParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("midiPosMax"));

    lastPlayheadParam = playheadPosParam->get();
    for (int i = 0; i < 16; ++i) synth.addVoice(new GrainfreezeVoice(*this));
    synth.addSound(new GrainfreezeSound());
    updateFftSize();
}

GrainfreezeAudioProcessor::~GrainfreezeAudioProcessor() {}
const juce::String GrainfreezeAudioProcessor::getName() const { return JucePlugin_Name; }
bool GrainfreezeAudioProcessor::producesMidi() const { return false; }
bool GrainfreezeAudioProcessor::isMidiEffect() const { return false; }
double GrainfreezeAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int GrainfreezeAudioProcessor::getNumPrograms() { return 1; }
int GrainfreezeAudioProcessor::getCurrentProgram() { return 0; }
void GrainfreezeAudioProcessor::setCurrentProgram(int index) { juce::ignoreUnused(index); }
const juce::String GrainfreezeAudioProcessor::getProgramName(int index) { juce::ignoreUnused(index); return {}; }
void GrainfreezeAudioProcessor::changeProgramName(int index, const juce::String& n) { juce::ignoreUnused(index, n); }

void GrainfreezeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate(sampleRate);
    updateFftSize();
}

void GrainfreezeAudioProcessor::releaseResources() {}
bool GrainfreezeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GrainfreezeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (!audioLoaded) return;

    static int lastFftIdx = -1; if (fftSizeParam->getIndex() != lastFftIdx) { lastFftIdx = fftSizeParam->getIndex(); updateFftSize(); }
    static float lastHopVal = -1.0f; if (hopSizeParam->get() != lastHopVal) { lastHopVal = hopSizeParam->get(); updateHopSize(); }
    static int lastWinIdx = -1; if (windowTypeParam->getIndex() != lastWinIdx) { lastWinIdx = windowTypeParam->getIndex(); createWindow(); }

    bool isMidi = midiModeParam->get();
    static bool wasMidi = false;
    
    // Kill notes on transition to ensure clean state
    if (isMidi != wasMidi)
    {
        synth.allNotesOff(0, false);
        wasMidi = isMidi;
    }

    if (isMidi)
    {
        synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
    }
    else
    {
        if (syncToDawParam->get())
            if (auto* ph = getPlayHead())
                if (auto posInfo = ph->getPosition())
                    if (posInfo->getIsPlaying() != playing) setPlaying(posInfo->getIsPlaying());

        float currentParam = playheadPosParam->get();
        if (std::abs(currentParam - lastPlayheadParam) > 0.00001f) { setPlayheadPosition(currentParam); lastPlayheadParam = currentParam; }

        bool shouldBeActive = playing || freezeModeParam->get();
        GrainfreezeVoice* v = getManualVoice();

        if (shouldBeActive && v == nullptr) synth.noteOn(1, 60, 1.0f);
        else if (!shouldBeActive && v != nullptr) synth.noteOff(1, 60, 1.0f, false);

        juce::MidiBuffer dummyMidi;
        synth.renderNextBlock(buffer, dummyMidi, 0, buffer.getNumSamples());

        if (v != nullptr && !freezeModeParam->get() && playing)
        {
            float np = static_cast<float>(v->playbackPosition / static_cast<double>(loadedAudio.getNumSamples()));
            playheadPosition.store(np);
            *playheadPosParam = np;
            lastPlayheadParam = np;
        }
        else lastPlayheadParam = playheadPosParam->get();
    }

    // Global Mute: Silence output if stopped
    if (!playing)
        buffer.clear();
}

GrainfreezeVoice* GrainfreezeAudioProcessor::getManualVoice()
{
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<GrainfreezeVoice*>(synth.getVoice(i)))
            if (v->isVoiceActive() && v->getCurrentlyPlayingNote() == 60) return v;
    return nullptr;
}

void GrainfreezeAudioProcessor::updateFftSize()
{
    int fftSizes[] = { 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536 };
    currentFftSize = fftSizes[fftSizeParam->getIndex()];
    updateHopSize();
    int order = 0; int t = currentFftSize; while (t > 1) { t >>= 1; order++; }
    fftAnalysis = std::make_unique<juce::dsp::FFT>(order); fftSynthesis = std::make_unique<juce::dsp::FFT>(order);
    fftBuffer.assign(static_cast<size_t>(currentFftSize * 2), 0.0f);
    analysisFrame.assign(static_cast<size_t>(currentFftSize), 0.0f);
    synthesisFrame.assign(static_cast<size_t>(currentFftSize), 0.0f);
    magnitudeBuffer.assign(static_cast<size_t>(currentFftSize / 2 + 1), 0.0f);
    phaseAdvanceBuffer.assign(static_cast<size_t>(currentFftSize / 2 + 1), 0.0f);
    window.resize(static_cast<size_t>(currentFftSize)); createWindow();
    
    for (int i = 0; i < synth.getNumVoices(); ++i)
    {
        if (auto* v = dynamic_cast<GrainfreezeVoice*>(synth.getVoice(i)))
        {
            v->previousPhase.assign(static_cast<size_t>(currentFftSize / 2 + 1), 0.0f);
            v->synthesisPhase.assign(static_cast<size_t>(currentFftSize / 2 + 1), 0.0f);
            v->outputAccum.assign(static_cast<size_t>(currentFftSize * 8), 0.0f);
            v->outputWritePos = 0; v->grainCounter = 0;
            v->smoothedFreezePosition.reset(currentSampleRate, static_cast<double>(glideParam->get()) / 1000.0);
        }
    }
}

void GrainfreezeAudioProcessor::updateHopSize() { currentHopSize = std::max(1, static_cast<int>(static_cast<float>(currentFftSize) / hopSizeParam->get())); }
void GrainfreezeAudioProcessor::createWindow() { if (windowTypeParam->getIndex() == 0) createHannWindow(); else createBlackmanHarrisWindow(); }
void GrainfreezeAudioProcessor::createHannWindow() { for (int i = 0; i < currentFftSize; ++i) window[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) / static_cast<float>(currentFftSize - 1))); }
void GrainfreezeAudioProcessor::createBlackmanHarrisWindow() { for (int i = 0; i < currentFftSize; ++i) { float n = static_cast<float>(i) / static_cast<float>(currentFftSize - 1); window[static_cast<size_t>(i)] = 0.35875f - 0.48829f * std::cos(2.0f * juce::MathConstants<float>::pi * n) + 0.14128f * std::cos(4.0f * juce::MathConstants<float>::pi * n) - 0.01168f * std::cos(6.0f * juce::MathConstants<float>::pi * n); } }

void GrainfreezeAudioProcessor::loadAudioFile(const juce::File& file)
{
    if (!file.existsAsFile()) return;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(file));
    if (r) {
        juce::AudioBuffer<float> nb(static_cast<int>(r->numChannels), static_cast<int>(r->lengthInSamples));
        if (r->read(&nb, 0, static_cast<int>(r->lengthInSamples), 0, true, true)) {
            loadedAudio.makeCopyOf(nb); lastLoadedFileName = file.getFileName(); audioLoaded = true;
            playheadPosition.store(0.0f); playbackPosition = 0.0; freezeCurrentPosition = 0.0; freezeTargetPosition = 0.0;
            smoothedFreezePosition.setCurrentAndTargetValue(0.0);
            synth.allNotesOff(0, false);
        }
    }
}

void GrainfreezeAudioProcessor::setPlayheadPosition(float np) { float cp = juce::jlimit(0.0f, 1.0f, np); if (isInFreezeMode || freezeModeParam->get()) freezeTargetPosition = static_cast<double>(cp) * static_cast<double>(loadedAudio.getNumSamples()); else { playbackPosition = static_cast<double>(cp) * static_cast<double>(loadedAudio.getNumSamples()); playheadPosition.store(cp); if (auto* v = getManualVoice()) v->playbackPosition = playbackPosition; } }
void GrainfreezeAudioProcessor::setPlaying(bool sp) { if (sp && !playing) playbackStartPosition = playbackPosition; else if (!sp && playing) { playbackPosition = playbackStartPosition; freezeCurrentPosition = playbackStartPosition; freezeTargetPosition = playbackStartPosition; smoothedFreezePosition.setCurrentAndTargetValue(playbackStartPosition); float np = (loadedAudio.getNumSamples() > 0) ? static_cast<float>(playbackPosition / static_cast<double>(loadedAudio.getNumSamples())) : 0.0f; playheadPosition.store(np); if (playheadPosParam) { playheadPosParam->beginChangeGesture(); *playheadPosParam = np; playheadPosParam->endChangeGesture(); } lastPlayheadParam = np; } playing = sp; }

juce::AudioProcessorEditor* GrainfreezeAudioProcessor::createEditor() { return new GrainfreezeAudioProcessorEditor(*this); }
bool GrainfreezeAudioProcessor::hasEditor() const { return true; }
void GrainfreezeAudioProcessor::getStateInformation(juce::MemoryBlock& d) { auto s = apvts.copyState(); std::unique_ptr<juce::XmlElement> x(s.createXml()); copyXmlToBinary(*x, d); }
void GrainfreezeAudioProcessor::setStateInformation(const void* d, int s) { std::unique_ptr<juce::XmlElement> x(getXmlFromBinary(d, s)); if (x && x->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*x)); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GrainfreezeAudioProcessor(); }
