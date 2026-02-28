#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// WaveformDisplay Implementation
//==============================================================================

void WaveformDisplay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    if (!processor.isAudioLoaded())
    {
        g.setColour(juce::Colours::grey);
        g.drawText("Load an audio file to begin", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto& audio = processor.getLoadedAudio();
    int numSamples = audio.getNumSamples();
    if (numSamples == 0) return;

    int width = getWidth();
    int height = getHeight();
    int centerY = height / 2;

    float loopStartX = processor.loopStartParam->get() * static_cast<float>(width);
    float loopEndX = processor.loopEndParam->get() * static_cast<float>(width);
    
    g.setColour(juce::Colours::darkgrey.withAlpha(0.2f));
    g.fillRect(loopStartX, 0.0f, loopEndX - loopStartX, static_cast<float>(height));

    g.setColour(juce::Colours::lightblue);
    juce::Path waveformPath;
    bool firstPoint = true;
    const float* channelData = audio.getReadPointer(0);

    for (int x = 0; x < width; ++x)
    {
        float position = static_cast<float>(x) / static_cast<float>(width);
        int sampleIndex = static_cast<int>(position * static_cast<float>(numSamples));
        if (sampleIndex >= 0 && sampleIndex < numSamples)
        {
            float sample = channelData[sampleIndex];
            float y = static_cast<float>(centerY) - (sample * static_cast<float>(centerY) * 0.8f);
            if (firstPoint) { waveformPath.startNewSubPath(static_cast<float>(x), y); firstPoint = false; }
            else waveformPath.lineTo(static_cast<float>(x), y);
        }
    }
    g.strokePath(waveformPath, juce::PathStrokeType(1.5f));

    g.setColour(juce::Colours::orange.withAlpha(0.7f));
    g.drawLine(loopStartX, 0.0f, loopStartX, static_cast<float>(height), 1.5f);
    g.drawLine(loopEndX, 0.0f, loopEndX, static_cast<float>(height), 1.5f);
    
    juce::Path startTri, endTri;
    startTri.addTriangle(loopStartX - 6.0f, 0.0f, loopStartX + 6.0f, 0.0f, loopStartX, 12.0f);
    endTri.addTriangle(loopEndX - 6.0f, 0.0f, loopEndX + 6.0f, 0.0f, loopEndX, 12.0f);
    g.fillPath(startTri); g.fillPath(endTri);

    // Draw playheads
    bool isMidiMode = processor.midiModeParam->get();
    
    if (isMidiMode)
    {
        for (int i = 0; i < processor.synth.getNumVoices(); ++i)
        {
            if (auto* voice = dynamic_cast<GrainfreezeVoice*>(processor.synth.getVoice(i)))
            {
                if (voice->isVoiceActive())
                {
                    float px = (static_cast<float>(voice->freezeCurrentPosition) / static_cast<float>(numSamples)) * static_cast<float>(width);
                    g.setColour(juce::Colours::cyan.withAlpha(voice->currentVelocity * 0.8f + 0.2f));
                    g.drawLine(px, 0.0f, px, static_cast<float>(height), 1.5f);
                    g.fillEllipse(px - 3.0f, static_cast<float>(centerY) - 3.0f, 6.0f, 6.0f);
                }
            }
        }
    }
    else
    {
        float playheadX = processor.getPlayheadPosition() * static_cast<float>(width);
        g.setColour(processor.isPlaying() ? juce::Colours::green : juce::Colours::yellow);
        g.drawLine(playheadX, 0.0f, playheadX, static_cast<float>(height), 2.0f);
        g.fillEllipse(playheadX - 4.0f, static_cast<float>(centerY) - 4.0f, 8.0f, 8.0f);
    }
}

void WaveformDisplay::mouseDown(const juce::MouseEvent& event)
{
    float width = static_cast<float>(getWidth());
    float mouseX = static_cast<float>(event.x);
    float ls = processor.loopStartParam->get() * width;
    float le = processor.loopEndParam->get() * width;

    if (std::abs(mouseX - ls) < 12.0f) { dragMode = DragMode::LoopStart; processor.loopStartParam->beginChangeGesture(); }
    else if (std::abs(mouseX - le) < 12.0f) { dragMode = DragMode::LoopEnd; processor.loopEndParam->beginChangeGesture(); }
    else { dragMode = DragMode::Playhead; if (processor.playheadPosParam) processor.playheadPosParam->beginChangeGesture(); }
    updateFromMouse(event);
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& event) { updateFromMouse(event); repaint(); }
void WaveformDisplay::mouseUp(const juce::MouseEvent& event) {
    juce::ignoreUnused(event);
    if (dragMode == DragMode::LoopStart) processor.loopStartParam->endChangeGesture();
    else if (dragMode == DragMode::LoopEnd) processor.loopEndParam->endChangeGesture();
    else if (dragMode == DragMode::Playhead && processor.playheadPosParam) processor.playheadPosParam->endChangeGesture();
    dragMode = DragMode::None;
}

void WaveformDisplay::updateFromMouse(const juce::MouseEvent& event)
{
    float np = juce::jlimit(0.0f, 1.0f, static_cast<float>(event.x) / static_cast<float>(getWidth()));
    if (dragMode == DragMode::LoopStart) { float ep = processor.loopEndParam->get(); if (np >= ep) np = ep - 0.001f; *processor.loopStartParam = np; if (processor.playheadPosParam) *processor.playheadPosParam = np; processor.setPlayheadPosition(np); }
    else if (dragMode == DragMode::LoopEnd) { float sp = processor.loopStartParam->get(); if (np <= sp) np = sp + 0.001f; *processor.loopEndParam = np; }
    else if (dragMode == DragMode::Playhead) { if (processor.playheadPosParam) *processor.playheadPosParam = np; processor.setPlayheadPosition(np); }
}

void SpectrumVisualizer::updateSpectrum(const std::vector<float>& magnitudes, int fftSize, double sampleRate)
{
    if (noteMagnitudes.empty()) noteMagnitudes.resize(numNotes, 0.0f);
    std::fill(noteMagnitudes.begin(), noteMagnitudes.end(), 0.0f);
    int nb = fftSize / 2 + 1;
    for (int bin = 1; bin < nb; ++bin) {
        float f = (static_cast<float>(bin) * static_cast<float>(sampleRate)) / static_cast<float>(fftSize);
        int mn = frequencyToMidiNote(f);
        if (mn >= lowestNote && mn < lowestNote + numNotes) { size_t idx = static_cast<size_t>(mn - lowestNote); noteMagnitudes[idx] = juce::jmax(noteMagnitudes[idx], magnitudes[static_cast<size_t>(bin)]); }
    }
    repaint();
}

void SpectrumVisualizer::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black); if (noteMagnitudes.empty()) return;
    int w = getWidth(); int h = getHeight(); float bw = static_cast<float>(w) / static_cast<float>(numNotes);
    float maxM = 0.0001f; for (float m : noteMagnitudes) maxM = juce::jmax(maxM, m);
    struct NI { int i; float m; }; std::vector<NI> tn;
    for (int i = 0; i < numNotes; ++i) if (noteMagnitudes[static_cast<size_t>(i)] > 0.0f) tn.push_back({i, noteMagnitudes[static_cast<size_t>(i)]});
    std::sort(tn.begin(), tn.end(), [](const NI& a, const NI& b) { return a.m > b.m; });
    if (tn.size() > 10) tn.resize(10);
    for (int i = 0; i < numNotes; ++i) {
        float x = static_cast<float>(i) * bw; float nm = noteMagnitudes[static_cast<size_t>(i)] / maxM; float bh = nm * (static_cast<float>(h) - 20.0f);
        if (bh > 1.0f) { juce::Colour c = (nm < 0.5f) ? juce::Colours::blue.interpolatedWith(juce::Colours::cyan, nm * 2.0f) : juce::Colours::cyan.interpolatedWith(juce::Colours::yellow, (nm - 0.5f) * 2.0f); g.setColour(c); g.fillRect(x, static_cast<float>(h) - bh, bw - 1.0f, bh); }
    }
    g.setFont(10.0f);
    for (const auto& ni : tn) {
        int mn = lowestNote + ni.i; juce::String name = midiNoteToName(mn);
        float x = static_cast<float>(ni.i) * bw; float nm = ni.m / maxM; float bh = nm * (static_cast<float>(h) - 20.0f);
        if (bh > 15.0f) { g.setColour(juce::Colours::white); float tw = bw * 3.0f; float tx = x - tw / 2.0f + bw / 2.0f; g.drawText(name, static_cast<int>(tx), static_cast<int>(static_cast<float>(h) - bh - 14.0f), static_cast<int>(tw), 12, juce::Justification::centred); }
    }
}

int SpectrumVisualizer::frequencyToMidiNote(float f) { return (f <= 0.0f) ? -1 : static_cast<int>(std::round(69.0f + 12.0f * std::log2(f / 440.0f))); }
juce::String SpectrumVisualizer::midiNoteToName(int mn) { const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String(names[mn % 12]) + juce::String((mn / 12) - 1); }

GrainfreezeAudioProcessorEditor::GrainfreezeAudioProcessorEditor(GrainfreezeAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), waveformDisplay(p), spectrumVisualizer(p)
{
    setSize(900, 750);

    addAndMakeVisible(waveformDisplay);
    addAndMakeVisible(spectrumVisualizer);

    addAndMakeVisible(loadButton); loadButton.setButtonText("Load Audio"); loadButton.onClick = [this] { loadAudioFile(); };
    addAndMakeVisible(playButton); playButton.setButtonText("Play / Stop"); playButton.onClick = [this] { audioProcessor.setPlaying(!audioProcessor.isPlaying()); };
    addAndMakeVisible(freezeButton); freezeButton.setButtonText("Freeze"); freezeButton.setClickingTogglesState(true);
    freezeModeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "freezeMode", freezeButton);
    addAndMakeVisible(syncToDawButton); syncToDawButton.setButtonText("Sync DAW");
    syncToDawAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "syncToDaw", syncToDawButton);
    addAndMakeVisible(midiModeButton); midiModeButton.setButtonText("MIDI Mode"); midiModeButton.setClickingTogglesState(true);
    midiModeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "midiMode", midiModeButton);

    addAndMakeVisible(statusLabel); statusLabel.setText("No audio", juce::dontSendNotification); statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(recommendedLabel); recommendedLabel.setText("Recommended: Center 0.5 | Min 0.0 | Max 1.0", juce::dontSendNotification);
    recommendedLabel.setJustificationType(juce::Justification::centredRight); recommendedLabel.setFont(juce::FontOptions(11.0f).withStyle("Italic"));
    recommendedLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(primaryControlsLabel); primaryControlsLabel.setText("Primary", juce::dontSendNotification); primaryControlsLabel.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
    addAndMakeVisible(advancedControlsLabel); advancedControlsLabel.setText("Advanced", juce::dontSendNotification); advancedControlsLabel.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
    addAndMakeVisible(midiControlsLabel); midiControlsLabel.setText("MIDI Mapping", juce::dontSendNotification); midiControlsLabel.setFont(juce::FontOptions(14.0f).withStyle("Bold"));

    addAndMakeVisible(timeStretchSlider); timeStretchSlider.setSliderStyle(juce::Slider::LinearHorizontal); timeStretchSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    timeStretchAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "timeStretch", timeStretchSlider); addAndMakeVisible(timeStretchLabel); timeStretchLabel.setText("Stretch", juce::dontSendNotification);
    addAndMakeVisible(fftSizeSlider); fftSizeSlider.setSliderStyle(juce::Slider::LinearHorizontal); fftSizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    fftSizeAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "fftSize", fftSizeSlider); addAndMakeVisible(fftSizeLabel); fftSizeLabel.setText("FFT Size", juce::dontSendNotification);
    addAndMakeVisible(hopSizeSlider); hopSizeSlider.setSliderStyle(juce::Slider::LinearHorizontal); hopSizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    hopSizeAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "hopSize", hopSizeSlider); addAndMakeVisible(hopSizeLabel); hopSizeLabel.setText("Hop Div", juce::dontSendNotification);
    addAndMakeVisible(glideSlider); glideSlider.setSliderStyle(juce::Slider::LinearHorizontal); glideSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    glideSlider.setTextValueSuffix(" ms"); glideAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "glide", glideSlider);
    addAndMakeVisible(glideLabel); glideLabel.setText("Glide", juce::dontSendNotification);
    addAndMakeVisible(pitchShiftSlider); pitchShiftSlider.setSliderStyle(juce::Slider::LinearHorizontal); pitchShiftSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    pitchShiftSlider.setTextValueSuffix(" st"); pitchShiftAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "pitchShift", pitchShiftSlider);
    addAndMakeVisible(pitchShiftLabel); pitchShiftLabel.setText("Pitch", juce::dontSendNotification);

    addAndMakeVisible(hfBoostSlider); hfBoostSlider.setSliderStyle(juce::Slider::LinearHorizontal); hfBoostSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    hfBoostSlider.setTextValueSuffix(" %"); hfBoostAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "hfBoost", hfBoostSlider);
    addAndMakeVisible(hfBoostLabel); hfBoostLabel.setText("HF Boost", juce::dontSendNotification);
    addAndMakeVisible(microMovementSlider); microMovementSlider.setSliderStyle(juce::Slider::LinearHorizontal); microMovementSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    microMovementSlider.setTextValueSuffix(" %"); microMovementAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "microMovement", microMovementSlider);
    addAndMakeVisible(microMovementLabel); microMovementLabel.setText("MicroMove", juce::dontSendNotification);
    addAndMakeVisible(windowTypeSlider); windowTypeSlider.setSliderStyle(juce::Slider::LinearHorizontal); windowTypeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    windowTypeAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "windowType", windowTypeSlider);
    addAndMakeVisible(windowTypeLabel); windowTypeLabel.setText("Window", juce::dontSendNotification);
    addAndMakeVisible(crossfadeLengthSlider); crossfadeLengthSlider.setSliderStyle(juce::Slider::LinearHorizontal); crossfadeLengthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    crossfadeLengthAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "crossfadeLength", crossfadeLengthSlider);
    addAndMakeVisible(crossfadeLengthLabel); crossfadeLengthLabel.setText("X-Fade", juce::dontSendNotification);

    addAndMakeVisible(midiPosMinSlider); midiPosMinSlider.setSliderStyle(juce::Slider::LinearHorizontal); midiPosMinSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    midiPosMinAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "midiPosMin", midiPosMinSlider);
    addAndMakeVisible(midiPosMinLabel); midiPosMinLabel.setText("Min Pos", juce::dontSendNotification);
    addAndMakeVisible(midiPosCenterSlider); midiPosCenterSlider.setSliderStyle(juce::Slider::LinearHorizontal); midiPosCenterSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    midiPosCenterAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "midiPosCenter", midiPosCenterSlider);
    addAndMakeVisible(midiPosCenterLabel); midiPosCenterLabel.setText("Center (C4)", juce::dontSendNotification);
    addAndMakeVisible(midiPosMaxSlider); midiPosMaxSlider.setSliderStyle(juce::Slider::LinearHorizontal); midiPosMaxSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    midiPosMaxAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "midiPosMax", midiPosMaxSlider);
    addAndMakeVisible(midiPosMaxLabel); midiPosMaxLabel.setText("Max Pos", juce::dontSendNotification);

    startTimerHz(30);
}

GrainfreezeAudioProcessorEditor::~GrainfreezeAudioProcessorEditor() {}
void GrainfreezeAudioProcessorEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colours::darkgrey); }

void GrainfreezeAudioProcessorEditor::resized()
{
    auto b = getLocalBounds();
    auto top = b.removeFromTop(250); top.reduce(10, 10);
    auto ba = top.removeFromLeft(120);
    loadButton.setBounds(ba.removeFromTop(25)); ba.removeFromTop(5);
    playButton.setBounds(ba.removeFromTop(25)); ba.removeFromTop(5);
    freezeButton.setBounds(ba.removeFromTop(25)); ba.removeFromTop(5);
    syncToDawButton.setBounds(ba.removeFromTop(25)); ba.removeFromTop(5);
    midiModeButton.setBounds(ba.removeFromTop(25));
    top.removeFromLeft(15); int cw = (top.getWidth() - 30) / 3;
    auto lc = top.removeFromLeft(cw); primaryControlsLabel.setBounds(lc.removeFromTop(20)); lc.removeFromTop(5);
    auto r1 = lc.removeFromTop(30); timeStretchLabel.setBounds(r1.removeFromLeft(60)); timeStretchSlider.setBounds(r1); lc.removeFromTop(2);
    auto r2 = lc.removeFromTop(30); fftSizeLabel.setBounds(r2.removeFromLeft(60)); fftSizeSlider.setBounds(r2); lc.removeFromTop(2);
    auto r3 = lc.removeFromTop(30); hopSizeLabel.setBounds(r3.removeFromLeft(60)); hopSizeSlider.setBounds(r3); lc.removeFromTop(2);
    auto r4 = lc.removeFromTop(30); glideLabel.setBounds(r4.removeFromLeft(60)); glideSlider.setBounds(r4); lc.removeFromTop(2);
    auto r5 = lc.removeFromTop(30); pitchShiftLabel.setBounds(r5.removeFromLeft(60)); pitchShiftSlider.setBounds(r5);
    top.removeFromLeft(15);
    auto cc = top.removeFromLeft(cw); advancedControlsLabel.setBounds(cc.removeFromTop(20)); cc.removeFromTop(5);
    auto r6 = cc.removeFromTop(30); hfBoostLabel.setBounds(r6.removeFromLeft(75)); hfBoostSlider.setBounds(r6); cc.removeFromTop(2);
    auto r7 = cc.removeFromTop(30); microMovementLabel.setBounds(r7.removeFromLeft(75)); microMovementSlider.setBounds(r7); cc.removeFromTop(2);
    auto r8 = cc.removeFromTop(30); windowTypeLabel.setBounds(r8.removeFromLeft(75)); windowTypeSlider.setBounds(r8); cc.removeFromTop(2);
    auto r9 = cc.removeFromTop(30); crossfadeLengthLabel.setBounds(r9.removeFromLeft(75)); crossfadeLengthSlider.setBounds(r9);
    top.removeFromLeft(15);
    auto rc = top; midiControlsLabel.setBounds(rc.removeFromTop(20)); rc.removeFromTop(5);
    auto r10 = rc.removeFromTop(30); midiPosMinLabel.setBounds(r10.removeFromLeft(80)); midiPosMinSlider.setBounds(r10); rc.removeFromTop(2);
    auto r11 = rc.removeFromTop(30); midiPosCenterLabel.setBounds(r11.removeFromLeft(80)); midiPosCenterSlider.setBounds(r11); rc.removeFromTop(2);
    auto r12 = rc.removeFromTop(30); midiPosMaxLabel.setBounds(r12.removeFromLeft(80)); midiPosMaxSlider.setBounds(r12);
    auto sa = b.removeFromBottom(40);
    recommendedLabel.setBounds(sa.removeFromRight(350));
    statusLabel.setBounds(sa);

    spectrumVisualizer.setBounds(b.removeFromBottom(120).reduced(10, 5));
    waveformDisplay.setBounds(b.reduced(10, 10));
}

void GrainfreezeAudioProcessorEditor::timerCallback()
{
    waveformDisplay.repaint();
    const auto& m = audioProcessor.getSpectrumMagnitudes();
    if (!m.empty()) spectrumVisualizer.updateSpectrum(m, audioProcessor.getCurrentFftSize(), audioProcessor.getCurrentSampleRate());
    bool isF = audioProcessor.freezeModeParam->get();
    freezeButton.setToggleState(isF, juce::dontSendNotification);
    freezeButton.setColour(juce::TextButton::buttonColourId, isF ? juce::Colours::orange : juce::Colours::grey);
    bool isM = audioProcessor.midiModeParam->get();
    midiModeButton.setToggleState(isM, juce::dontSendNotification);
    midiModeButton.setColour(juce::TextButton::buttonColourId, isM ? juce::Colours::cyan : juce::Colours::grey);
    if (audioProcessor.isAudioLoaded()) {
        juce::String s = "Loaded: " + audioProcessor.getLoadedFileName() + " | ";
        if (isM) s += "MIDI POLY"; else if (isF) s += "FREEZE"; else if (audioProcessor.isPlaying()) s += "PLAYING"; else s += "STOPPED";
        statusLabel.setText(s, juce::dontSendNotification);
        playButton.setColour(juce::TextButton::buttonColourId, audioProcessor.isPlaying() ? juce::Colours::green : juce::Colours::grey);
        glideSlider.setEnabled(isF || isM);
    } else statusLabel.setText("No audio", juce::dontSendNotification);
}

void GrainfreezeAudioProcessorEditor::loadAudioFile()
{
    fileChooser = std::make_unique<juce::FileChooser>("Select audio...", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.wav;*.mp3;*.aif;*.aiff;*.flac");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& c) {
        auto f = c.getResult(); if (f.existsAsFile()) { audioProcessor.loadAudioFile(f); audioProcessor.setPlaying(false); waveformDisplay.repaint(); }
    });
}
