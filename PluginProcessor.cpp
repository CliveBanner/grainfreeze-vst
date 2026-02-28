#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Parameter Layout Creation
//==============================================================================

juce::AudioProcessorValueTreeState::ParameterLayout GrainfreezeAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("timeStretch", 1), "Time Stretch",
        juce::NormalisableRange<float>(0.1f, 4.0f, 0.01f, 0.5f),
        1.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("grainSize", 1), "Grain Size",
        juce::NormalisableRange<float>(512.0f, 8192.0f, 1.0f, 0.3f),
        2048.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("hopSize", 1), "Hop Size",
        juce::NormalisableRange<float>(2.0f, 16.0f, 0.5f),
        4.0f));

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

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("freezeMode", 1), "Freeze Mode",
        false));

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

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("hfBoost", 1), "HF Boost",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        10.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("microMovement", 1), "Micro Movement",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        20.0f));

    juce::StringArray windowChoices;
    windowChoices.add("Hann");
    windowChoices.add("Blackman-Harris");

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("windowType", 1), "Window Type",
        windowChoices,
        1));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("crossfadeLength", 1), "Crossfade Length",
        juce::NormalisableRange<float>(1.0f, 8.0f, 0.5f),
        2.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("midiMode", 1), "MIDI Mode",
        false));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("midiPosMin", 1), "MIDI Min Pos",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("midiPosCenter", 1), "MIDI Center Pos (C4)",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.5f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("midiPosMax", 1), "MIDI Max Pos",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        1.0f));

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
void GrainfreezeAudioProcessor::changeProgramName(int index, const juce::String& newName) { juce::ignoreUnused(index, newName); }

void GrainfreezeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate;
    playbackPosition = 0.0;

    for (auto& voice : voices)
    {
        voice.isActive = false;
        voice.midiNote = -1;
        voice.velocity = 0.0f;
        voice.playbackPosition = 0.0;
        voice.freezeCurrentPosition = 0.0;
        voice.freezeTargetPosition = 0.0;
        voice.smoothedFreezePosition.reset(sampleRate, 0.1);
        voice.smoothedFreezePosition.setCurrentAndTargetValue(0.0);
        voice.freezeMicroMovement = 0.0f;
        voice.freezeMicroCounter = 0;
        voice.previousPhase.assign(static_cast<size_t>(currentFftSize / 2 + 1), 0.0f);
        voice.synthesisPhase.assign(static_cast<size_t>(currentFftSize / 2 + 1), 0.0f);
        voice.outputAccum.assign(static_cast<size_t>(currentFftSize * 8), 0.0f);
        voice.outputWritePos = 0;
        voice.grainCounter = 0;
    }

    smoothedFreezePosition.reset(sampleRate, 0.1);
    smoothedFreezePosition.setCurrentAndTargetValue(0.0);
    freezeCurrentPosition = 0.0;
    freezeTargetPosition = 0.0;
}

void GrainfreezeAudioProcessor::releaseResources() {}
bool GrainfreezeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    return true;
}

void GrainfreezeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (!audioLoaded) return;

    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            int note = msg.getNoteNumber();
            Voice* voice = findVoice(note);
            if (voice == nullptr) voice = findFreeVoice();
            
            if (voice != nullptr)
            {
                voice->isActive = true;
                voice->midiNote = note;
                voice->velocity = msg.getFloatVelocity();
                
                float pos = (note < 60) ? juce::jmap(static_cast<float>(note), 0.0f, 60.0f, midiPosMinParam->get(), midiPosCenterParam->get())
                                        : juce::jmap(static_cast<float>(note), 60.0f, 127.0f, midiPosCenterParam->get(), midiPosMaxParam->get());
                
                double samplePos = static_cast<double>(pos) * static_cast<double>(loadedAudio.getNumSamples());
                voice->playbackPosition = samplePos;
                voice->freezeTargetPosition = samplePos;
                voice->freezeCurrentPosition = samplePos;
                voice->smoothedFreezePosition.setCurrentAndTargetValue(samplePos);
                
                std::fill(voice->previousPhase.begin(), voice->previousPhase.end(), 0.0f);
                std::fill(voice->synthesisPhase.begin(), voice->synthesisPhase.end(), 0.0f);
                std::fill(voice->outputAccum.begin(), voice->outputAccum.end(), 0.0f);
                voice->grainCounter = 0;
            }
        }
        else if (msg.isNoteOff())
        {
            if (Voice* v = findVoice(msg.getNoteNumber())) { v->isActive = false; v->midiNote = -1; }
        }
        else if (msg.isAllNotesOff()) { for (auto& v : voices) v.isActive = false; }
    }

    if (syncToDawParam->get())
    {
        if (auto* ph = getPlayHead())
            if (auto posInfo = ph->getPosition())
                if (posInfo->getIsPlaying() != playing) setPlaying(posInfo->getIsPlaying());
    }

    float currentParam = playheadPosParam->get();
    if (std::abs(currentParam - lastPlayheadParam) > 0.00001f) { setPlayheadPosition(currentParam); lastPlayheadParam = currentParam; }

    processTimeStretch(buffer, buffer.getNumSamples());

    if (!freezeModeParam->get() && !midiModeParam->get() && playing)
    {
        float actualPos = playheadPosition.load();
        *playheadPosParam = actualPos;
        lastPlayheadParam = actualPos;
    }
    else lastPlayheadParam = playheadPosParam->get();
}

void GrainfreezeAudioProcessor::processTimeStretch(juce::AudioBuffer<float>& outputBuffer, int numSamples)
{
    static int lastFftSizeIndex = -1;
    if (fftSizeParam->getIndex() != lastFftSizeIndex) { lastFftSizeIndex = fftSizeParam->getIndex(); updateFftSize(); }

    static float lastHopSizeVal = -1.0f;
    if (hopSizeParam->get() != lastHopSizeVal) { lastHopSizeVal = hopSizeParam->get(); updateHopSize(); }

    static int lastWindowTypeIdx = -1;
    if (windowTypeParam->getIndex() != lastWindowTypeIdx) { lastWindowTypeIdx = windowTypeParam->getIndex(); createWindow(); }

    static float lastGlideVal = -1.0f;
    if (glideParam->get() != lastGlideVal)
    {
        lastGlideVal = glideParam->get();
        double gs = static_cast<double>(lastGlideVal) / 1000.0;
        smoothedFreezePosition.reset(currentSampleRate, gs);
        for (auto& v : voices) v.smoothedFreezePosition.reset(currentSampleRate, gs);
    }

    isInFreezeMode = freezeModeParam->get();
    bool isMidiMode = midiModeParam->get();
    double numSamplesInAudio = static_cast<double>(loadedAudio.getNumSamples());
    double startSample = static_cast<double>(loopStartParam->get()) * numSamplesInAudio;
    double endSample = static_cast<double>(loopEndParam->get()) * numSamplesInAudio;
    if (startSample >= endSample) startSample = std::max(0.0, endSample - 1.0);

    if (isMidiMode)
    {
        for (auto& voice : voices)
        {
            if (!voice.isActive) continue;
            for (int sampleIdx = 0; sampleIdx < numSamples; ++sampleIdx)
            {
                voice.freezeCurrentPosition = voice.smoothedFreezePosition.getNextValue();
                voice.freezeMicroCounter++;
                if (voice.freezeMicroCounter >= currentHopSize / 4)
                {
                    voice.freezeMicroCounter = 0;
                    voice.freezeMicroMovement = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 0.0002f * (microMovementParam->get() / 100.0f);
                }
                voice.playbackPosition = juce::jlimit(startSample, endSample, voice.freezeCurrentPosition + (static_cast<double>(voice.freezeMicroMovement) * numSamplesInAudio));
                if (voice.grainCounter <= 0) { performPhaseVocoder(voice); voice.grainCounter = currentHopSize; }
                float outputSample = 0.0f;
                if (voice.outputWritePos < static_cast<int>(voice.outputAccum.size()))
                {
                    outputSample = voice.outputAccum[static_cast<size_t>(voice.outputWritePos)] * voice.velocity;
                    voice.outputAccum[static_cast<size_t>(voice.outputWritePos)] = 0.0f;
                }
                for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch) outputBuffer.addSample(ch, sampleIdx, outputSample);
                voice.outputWritePos = (voice.outputWritePos + 1) % static_cast<int>(voice.outputAccum.size());
                voice.grainCounter--;
            }
        }
    }
    else if (playing || isInFreezeMode)
    {
        auto& voice = voices[0];
        float playbackSpeed = 1.0f / std::max(0.1f, timeStretch->get());
        if (isInFreezeMode) smoothedFreezePosition.setTargetValue(juce::jlimit(startSample, endSample, freezeTargetPosition));

        for (int sampleIdx = 0; sampleIdx < numSamples; ++sampleIdx)
        {
            if (isInFreezeMode)
            {
                freezeCurrentPosition = smoothedFreezePosition.getNextValue();
                freezeMicroCounter++;
                if (freezeMicroCounter >= currentHopSize / 4)
                {
                    freezeMicroCounter = 0;
                    freezeMicroMovement = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 0.0002f * (microMovementParam->get() / 100.0f);
                }
                voice.playbackPosition = freezeCurrentPosition + (static_cast<double>(freezeMicroMovement) * numSamplesInAudio);
            }
            else voice.playbackPosition += static_cast<double>(playbackSpeed);

            if (voice.playbackPosition >= endSample) voice.playbackPosition = startSample + std::fmod(voice.playbackPosition - startSample, endSample - startSample);
            if (voice.playbackPosition < startSample) voice.playbackPosition = startSample;

            if (voice.grainCounter <= 0) { performPhaseVocoder(voice); voice.grainCounter = currentHopSize; }
            float outputSample = 0.0f;
            if (voice.outputWritePos < static_cast<int>(voice.outputAccum.size()))
            {
                outputSample = voice.outputAccum[static_cast<size_t>(voice.outputWritePos)];
                voice.outputAccum[static_cast<size_t>(voice.outputWritePos)] = 0.0f;
            }
            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch) outputBuffer.setSample(ch, sampleIdx, outputSample);
            voice.outputWritePos = (voice.outputWritePos + 1) % static_cast<int>(voice.outputAccum.size());
            voice.grainCounter--;
            playheadPosition.store(static_cast<float>((isInFreezeMode ? freezeCurrentPosition : voice.playbackPosition) / numSamplesInAudio));
        }
    }
}

void GrainfreezeAudioProcessor::performPhaseVocoder(Voice& voice)
{
    if (!audioLoaded) return;
    int numBins = currentFftSize / 2 + 1;
    int readPos = juce::jlimit(0, loadedAudio.getNumSamples() - currentFftSize, static_cast<int>(voice.playbackPosition));
    const float* src = loadedAudio.getReadPointer(0);
    bool isSt = loadedAudio.getNumChannels() > 1;
    const float* srcR = isSt ? loadedAudio.getReadPointer(1) : nullptr;

    for (int i = 0; i < currentFftSize; ++i)
    {
        int idx = readPos + i;
        float s = (idx < loadedAudio.getNumSamples()) ? src[idx] : 0.0f;
        if (isSt && srcR && idx < loadedAudio.getNumSamples()) s = (s + srcR[idx]) * 0.5f;
        analysisFrame[static_cast<size_t>(i)] = s * window[static_cast<size_t>(i)];
    }

    std::copy(analysisFrame.begin(), analysisFrame.end(), fftBuffer.begin());
    fftAnalysis->performRealOnlyForwardTransform(fftBuffer.data(), true);
    if (spectrumMagnitudes.size() != static_cast<size_t>(numBins)) spectrumMagnitudes.resize(static_cast<size_t>(numBins));

    float expPhaseAdv = juce::MathConstants<float>::twoPi * static_cast<float>(currentHopSize) / static_cast<float>(currentFftSize);
    for (int bin = 0; bin < numBins; ++bin)
    {
        float real = fftBuffer[static_cast<size_t>(bin * 2)], imag = fftBuffer[static_cast<size_t>(bin * 2 + 1)];
        float mag = std::sqrt(real * real + imag * imag), ph = std::atan2(imag, real);
        float d = (ph - voice.previousPhase[static_cast<size_t>(bin)]) - (static_cast<float>(bin) * expPhaseAdv);
        voice.previousPhase[static_cast<size_t>(bin)] = ph;
        d = std::fmod(d + juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi);
        if (d < 0) d += juce::MathConstants<float>::twoPi;
        magnitudeBuffer[static_cast<size_t>(bin)] = mag;
        phaseAdvanceBuffer[static_cast<size_t>(bin)] = (static_cast<float>(bin) * expPhaseAdv) + (d - juce::MathConstants<float>::pi);
    }

    float pf = std::pow(2.0f, pitchShiftParam->get() / 12.0f);
    std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
    for (int bin = 0; bin < numBins; ++bin)
    {
        float srcBin = static_cast<float>(bin) / pf;
        float mag = 0.0f, phAdv = 0.0f;
        if (srcBin < static_cast<float>(numBins - 1))
        {
            int bL = static_cast<int>(srcBin); float wU = srcBin - static_cast<float>(bL);
            mag = (magnitudeBuffer[static_cast<size_t>(bL)] * (1.0f - wU)) + (magnitudeBuffer[static_cast<size_t>(bL + 1)] * wU);
            phAdv = (phaseAdvanceBuffer[static_cast<size_t>(bL)] * (1.0f - wU)) + (phaseAdvanceBuffer[static_cast<size_t>(bL + 1)] * wU);
            phAdv *= pf;
        }
        mag *= (1.0f + (static_cast<float>(bin) / static_cast<float>(numBins - 1) * (hfBoostParam->get() / 100.0f)));
        spectrumMagnitudes[static_cast<size_t>(bin)] = mag;
        voice.synthesisPhase[static_cast<size_t>(bin)] += phAdv;
        voice.synthesisPhase[static_cast<size_t>(bin)] = std::fmod(voice.synthesisPhase[static_cast<size_t>(bin)] + juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi);
        if (voice.synthesisPhase[static_cast<size_t>(bin)] < 0) voice.synthesisPhase[static_cast<size_t>(bin)] += juce::MathConstants<float>::twoPi;
        voice.synthesisPhase[static_cast<size_t>(bin)] -= juce::MathConstants<float>::pi;
        fftBuffer[static_cast<size_t>(bin * 2)] = mag * std::cos(voice.synthesisPhase[static_cast<size_t>(bin)]);
        fftBuffer[static_cast<size_t>(bin * 2 + 1)] = mag * std::sin(voice.synthesisPhase[static_cast<size_t>(bin)]);
    }

    fftSynthesis->performRealOnlyInverseTransform(fftBuffer.data());
    float norm = 2.0f / (static_cast<float>(currentFftSize) / static_cast<float>(currentHopSize));
    for (int i = 0; i < currentFftSize; ++i)
    {
        int outIdx = (voice.outputWritePos + i) % static_cast<int>(voice.outputAccum.size());
        voice.outputAccum[static_cast<size_t>(outIdx)] += fftBuffer[static_cast<size_t>(i)] * window[static_cast<size_t>(i)] * norm;
    }
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
    crossfadeBuffer.assign(static_cast<size_t>(currentFftSize * 2), 0.0f);
    magnitudeBuffer.assign(static_cast<size_t>(currentFftSize / 2 + 1), 0.0f);
    phaseAdvanceBuffer.assign(static_cast<size_t>(currentFftSize / 2 + 1), 0.0f);
    window.resize(static_cast<size_t>(currentFftSize)); createWindow();
    for (auto& v : voices) {
        v.previousPhase.assign(static_cast<size_t>(currentFftSize / 2 + 1), 0.0f);
        v.synthesisPhase.assign(static_cast<size_t>(currentFftSize / 2 + 1), 0.0f);
        v.outputAccum.assign(static_cast<size_t>(currentFftSize * 8), 0.0f);
        v.outputWritePos = 0; v.grainCounter = 0;
        v.smoothedFreezePosition.reset(currentSampleRate, static_cast<double>(glideParam->get()) / 1000.0);
    }
    crossfadeSamples = static_cast<int>(static_cast<float>(currentHopSize) * crossfadeLengthParam->get());
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
            for (auto& v : voices) { v.isActive = false; std::fill(v.previousPhase.begin(), v.previousPhase.end(), 0.0f); std::fill(v.synthesisPhase.begin(), v.synthesisPhase.end(), 0.0f); }
        }
    }
}

void GrainfreezeAudioProcessor::setPlayheadPosition(float np) { float cp = juce::jlimit(0.0f, 1.0f, np); if (isInFreezeMode || freezeModeParam->get()) freezeTargetPosition = static_cast<double>(cp) * static_cast<double>(loadedAudio.getNumSamples()); else { playbackPosition = static_cast<double>(cp) * static_cast<double>(loadedAudio.getNumSamples()); playheadPosition.store(cp); } }
void GrainfreezeAudioProcessor::setPlaying(bool sp) { if (sp && !playing) playbackStartPosition = playbackPosition; else if (!sp && playing) { playbackPosition = playbackStartPosition; freezeCurrentPosition = playbackStartPosition; freezeTargetPosition = playbackStartPosition; smoothedFreezePosition.setCurrentAndTargetValue(playbackStartPosition); float np = (loadedAudio.getNumSamples() > 0) ? static_cast<float>(playbackPosition / static_cast<double>(loadedAudio.getNumSamples())) : 0.0f; playheadPosition.store(np); if (playheadPosParam) { playheadPosParam->beginChangeGesture(); *playheadPosParam = np; playheadPosParam->endChangeGesture(); } lastPlayheadParam = np; } playing = sp; }

GrainfreezeAudioProcessor::Voice* GrainfreezeAudioProcessor::findVoice(int mn) { for (auto& v : voices) if (v.isActive && v.midiNote == mn) return &v; return nullptr; }
GrainfreezeAudioProcessor::Voice* GrainfreezeAudioProcessor::findFreeVoice() { for (auto& v : voices) if (!v.isActive) return &v; return nullptr; }
bool GrainfreezeAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* GrainfreezeAudioProcessor::createEditor() { return new GrainfreezeAudioProcessorEditor(*this); }
void GrainfreezeAudioProcessor::getStateInformation(juce::MemoryBlock& d) { auto s = apvts.copyState(); std::unique_ptr<juce::XmlElement> x(s.createXml()); copyXmlToBinary(*x, d); }
void GrainfreezeAudioProcessor::setStateInformation(const void* d, int s) { std::unique_ptr<juce::XmlElement> x(getXmlFromBinary(d, s)); if (x && x->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*x)); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GrainfreezeAudioProcessor(); }
