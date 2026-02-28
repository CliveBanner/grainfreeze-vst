#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// WaveformDisplay Implementation
//==============================================================================

void WaveformDisplay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    // Show message if no audio is loaded
    if (!processor.isAudioLoaded())
    {
        g.setColour(juce::Colours::grey);
        g.drawText("Load an audio file to begin", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto& audio = processor.getLoadedAudio();
    int numSamples = audio.getNumSamples();

    if (numSamples == 0)
        return;

    int width = getWidth();
    int height = getHeight();
    int centerY = height / 2;

    // Draw loop region background
    float loopStartX = processor.loopStartParam->get() * width;
    float loopEndX = processor.loopEndParam->get() * width;
    
    g.setColour(juce::Colours::darkgrey.withAlpha(0.2f));
    g.fillRect(loopStartX, 0.0f, loopEndX - loopStartX, static_cast<float>(height));

    // Draw waveform
    g.setColour(juce::Colours::lightblue);

    juce::Path waveformPath;
    bool firstPoint = true;

    const float* channelData = audio.getReadPointer(0);

    // Sample waveform across display width
    for (int x = 0; x < width; ++x)
    {
        float position = static_cast<float>(x) / width;
        int sampleIndex = static_cast<int>(position * numSamples);

        if (sampleIndex >= 0 && sampleIndex < numSamples)
        {
            float sample = channelData[sampleIndex];
            float y = centerY - (sample * centerY * 0.8f);

            if (firstPoint)
            {
                waveformPath.startNewSubPath(static_cast<float>(x), y);
                firstPoint = false;
            }
            else
            {
                waveformPath.lineTo(static_cast<float>(x), y);
            }
        }
    }

    g.strokePath(waveformPath, juce::PathStrokeType(1.5f));

    // Draw loop markers
    g.setColour(juce::Colours::orange.withAlpha(0.7f));
    g.drawLine(loopStartX, 0, loopStartX, height, 1.5f);
    g.drawLine(loopEndX, 0, loopEndX, height, 1.5f);
    
    // Draw marker handles (triangles at the top)
    juce::Path startTri, endTri;
    startTri.addTriangle(loopStartX - 6, 0, loopStartX + 6, 0, loopStartX, 12);
    endTri.addTriangle(loopEndX - 6, 0, loopEndX + 6, 0, loopEndX, 12);
    g.fillPath(startTri);
    g.fillPath(endTri);

    // Draw playhead line
    float playheadX = processor.getPlayheadPosition() * width;
    g.setColour(processor.isPlaying() ? juce::Colours::green : juce::Colours::yellow);
    g.drawLine(playheadX, 0, playheadX, static_cast<float>(height), 2.0f);

    // Draw playhead circle
    g.fillEllipse(playheadX - 4, centerY - 4, 8, 8);
}

void WaveformDisplay::mouseDown(const juce::MouseEvent& event)
{
    float width = static_cast<float>(getWidth());
    float mouseX = static_cast<float>(event.x);
    
    float loopStartX = processor.loopStartParam->get() * width;
    float loopEndX = processor.loopEndParam->get() * width;

    // Check hit on markers (with 12px tolerance)
    if (std::abs(mouseX - loopStartX) < 12.0f)
    {
        dragMode = DragMode::LoopStart;
        processor.loopStartParam->beginChangeGesture();
    }
    else if (std::abs(mouseX - loopEndX) < 12.0f)
    {
        dragMode = DragMode::LoopEnd;
        processor.loopEndParam->beginChangeGesture();
    }
    else
    {
        dragMode = DragMode::Playhead;
        if (processor.playheadPosParam != nullptr)
            processor.playheadPosParam->beginChangeGesture();
    }

    updateFromMouse(event);
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& event)
{
    updateFromMouse(event);
    repaint();
}

void WaveformDisplay::mouseUp(const juce::MouseEvent& event)
{
    if (dragMode == DragMode::LoopStart)
        processor.loopStartParam->endChangeGesture();
    else if (dragMode == DragMode::LoopEnd)
        processor.loopEndParam->endChangeGesture();
    else if (dragMode == DragMode::Playhead)
    {
        if (processor.playheadPosParam != nullptr)
            processor.playheadPosParam->endChangeGesture();
    }
    
    dragMode = DragMode::None;
}

void WaveformDisplay::updateFromMouse(const juce::MouseEvent& event)
{
    float normalizedPosition = juce::jlimit(0.0f, 1.0f,
        static_cast<float>(event.x) / getWidth());

    if (dragMode == DragMode::LoopStart)
    {
        float endPos = processor.loopEndParam->get();
        if (normalizedPosition >= endPos)
            normalizedPosition = endPos - 0.001f;
        
        *processor.loopStartParam = normalizedPosition;

        // Also jump playhead to the new loop start
        if (processor.playheadPosParam != nullptr)
            *processor.playheadPosParam = normalizedPosition;
        processor.setPlayheadPosition(normalizedPosition);
    }
    else if (dragMode == DragMode::LoopEnd)
    {
        float startPos = processor.loopStartParam->get();
        if (normalizedPosition <= startPos)
            normalizedPosition = startPos + 0.001f;
            
        *processor.loopEndParam = normalizedPosition;
    }
    else if (dragMode == DragMode::Playhead)
    {
        if (processor.playheadPosParam != nullptr)
            *processor.playheadPosParam = normalizedPosition;

        processor.setPlayheadPosition(normalizedPosition);
    }
}

//==============================================================================
// SpectrumVisualizer Implementation
//==============================================================================

void SpectrumVisualizer::updateSpectrum(const std::vector<float>& magnitudes, int fftSize, double sampleRate)
{
    // Initialize note magnitudes vector if needed
    if (noteMagnitudes.empty())
    {
        noteMagnitudes.resize(numNotes, 0.0f);
    }

    // Reset all note magnitudes
    std::fill(noteMagnitudes.begin(), noteMagnitudes.end(), 0.0f);

    // Map FFT bins to musical notes
    int numBins = fftSize / 2 + 1;
    for (int bin = 1; bin < numBins; ++bin)  // Skip DC bin
    {
        // Calculate frequency for this bin
        float frequency = (bin * sampleRate) / fftSize;

        // Convert frequency to MIDI note
        int midiNote = frequencyToMidiNote(frequency);

        // If note is in piano range, accumulate magnitude
        if (midiNote >= lowestNote && midiNote < lowestNote + numNotes)
        {
            int noteIndex = midiNote - lowestNote;
            noteMagnitudes[noteIndex] = juce::jmax(noteMagnitudes[noteIndex], magnitudes[bin]);
        }
    }

    repaint();
}

void SpectrumVisualizer::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    if (noteMagnitudes.empty())
        return;

    int width = getWidth();
    int height = getHeight();

    // Calculate bar width
    float barWidth = static_cast<float>(width) / numNotes;

    // Find maximum magnitude for scaling
    float maxMagnitude = 0.0001f;  // Prevent division by zero
    for (float mag : noteMagnitudes)
    {
        maxMagnitude = juce::jmax(maxMagnitude, mag);
    }

    // Find top 10 loudest notes
    struct NoteInfo
    {
        int index;
        float magnitude;
    };
    std::vector<NoteInfo> topNotes;

    for (int i = 0; i < numNotes; ++i)
    {
        if (noteMagnitudes[i] > 0.0f)
        {
            topNotes.push_back({i, noteMagnitudes[i]});
        }
    }

    // Sort by magnitude (descending)
    std::sort(topNotes.begin(), topNotes.end(), 
        [](const NoteInfo& a, const NoteInfo& b) { return a.magnitude > b.magnitude; });

    // Keep only top 10
    if (topNotes.size() > 10)
        topNotes.resize(10);

    // Draw bars for each note
    for (int i = 0; i < numNotes; ++i)
    {
        float x = i * barWidth;
        float normalizedMag = noteMagnitudes[i] / maxMagnitude;
        float barHeight = normalizedMag * (height - 20);  // Leave space for labels

        if (barHeight > 1.0f)
        {
            // Color based on magnitude (blue to cyan to yellow)
            juce::Colour barColor;
            if (normalizedMag < 0.5f)
            {
                barColor = juce::Colours::blue.interpolatedWith(juce::Colours::cyan, normalizedMag * 2.0f);
            }
            else
            {
                barColor = juce::Colours::cyan.interpolatedWith(juce::Colours::yellow, (normalizedMag - 0.5f) * 2.0f);
            }

            g.setColour(barColor);
            g.fillRect(x, height - barHeight, barWidth - 1.0f, barHeight);
        }
    }

    // Draw labels for top 10 notes
    g.setFont(10.0f);
    for (const auto& noteInfo : topNotes)
    {
        int midiNote = lowestNote + noteInfo.index;
        juce::String noteName = midiNoteToName(midiNote);

        float x = noteInfo.index * barWidth;
        float normalizedMag = noteInfo.magnitude / maxMagnitude;
        float barHeight = normalizedMag * (height - 20);

        // Draw text above the bar
        if (barHeight > 15.0f)
        {
            g.setColour(juce::Colours::white);
            // Make text box wider (3x bar width) so note names display fully
            float textBoxWidth = barWidth * 3.0f;
            float textBoxX = x - textBoxWidth / 2.0f + barWidth / 2.0f;  // Center over bar
            g.drawText(noteName, 
                static_cast<int>(textBoxX), 
                static_cast<int>(height - barHeight - 14), 
                static_cast<int>(textBoxWidth), 
                12, 
                juce::Justification::centred);
        }
    }
}

int SpectrumVisualizer::frequencyToMidiNote(float frequency)
{
    // Convert frequency to MIDI note number
    // MIDI note 69 = A4 = 440 Hz
    if (frequency <= 0.0f)
        return -1;

    return static_cast<int>(std::round(69.0f + 12.0f * std::log2(frequency / 440.0f)));
}

juce::String SpectrumVisualizer::midiNoteToName(int midiNote)
{
    const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    
    int noteInOctave = midiNote % 12;
    int octave = (midiNote / 12) - 1;

    return juce::String(noteNames[noteInOctave]) + juce::String(octave);
}

//==============================================================================
// GrainfreezeAudioProcessorEditor Implementation
//==============================================================================

GrainfreezeAudioProcessorEditor::GrainfreezeAudioProcessorEditor(GrainfreezeAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p),
    waveformDisplay(p),
    spectrumVisualizer(p)
{
    // Set editor size (taller to accommodate spectrum and extra control)
    setSize(900, 700);

    //==========================================================================
    // Control Buttons Setup
    //==========================================================================

    addAndMakeVisible(loadButton);
    loadButton.setButtonText("Load Audio");
    loadButton.onClick = [this] { loadAudioFile(); };

    addAndMakeVisible(playButton);
    playButton.setButtonText("Play / Stop");
    playButton.onClick = [this]
        {
            audioProcessor.setPlaying(!audioProcessor.isPlaying());
        };

    addAndMakeVisible(freezeButton);
    freezeButton.setButtonText("Freeze");
    freezeButton.setClickingTogglesState(true);
    
    // Attach freeze button to APVTS
    freezeModeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "freezeMode", freezeButton);

    addAndMakeVisible(syncToDawButton);
    syncToDawButton.setButtonText("Sync to DAW");
    syncToDawAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "syncToDaw", syncToDawButton);

    //==========================================================================
    // Status Labels Setup
    //==========================================================================

    addAndMakeVisible(statusLabel);
    statusLabel.setText("No audio loaded", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(recommendedLabel);
    recommendedLabel.setText("Grainfreeze by aquanode\nRecommended settings for freeze mode:\nStretch 1.5 | Pitch 0 | FFT 8192 | Hop 6.5 | Micro Move 100%",
        juce::dontSendNotification);
    recommendedLabel.setJustificationType(juce::Justification::centredRight);
    recommendedLabel.setFont(juce::Font(11.0f, juce::Font::italic));
    recommendedLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    //==========================================================================
    // Column Headers Setup
    //==========================================================================

    addAndMakeVisible(primaryControlsLabel);
    primaryControlsLabel.setText("Primary Controls", juce::dontSendNotification);
    primaryControlsLabel.setJustificationType(juce::Justification::centredLeft);
    primaryControlsLabel.setFont(juce::Font(14.0f, juce::Font::bold));

    addAndMakeVisible(advancedControlsLabel);
    advancedControlsLabel.setText("Advanced Controls", juce::dontSendNotification);
    advancedControlsLabel.setJustificationType(juce::Justification::centredLeft);
    advancedControlsLabel.setFont(juce::Font(14.0f, juce::Font::bold));

    //==========================================================================
    // PRIMARY CONTROLS Setup (Left Column)
    //==========================================================================

    // Time Stretch slider
    addAndMakeVisible(timeStretchSlider);
    timeStretchSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    timeStretchSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    timeStretchAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "timeStretch", timeStretchSlider);

    addAndMakeVisible(timeStretchLabel);
    timeStretchLabel.setText("Time Stretch", juce::dontSendNotification);
    timeStretchLabel.setJustificationType(juce::Justification::centredLeft);

    // FFT Size slider
    addAndMakeVisible(fftSizeSlider);
    fftSizeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fftSizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    fftSizeAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "fftSize", fftSizeSlider);

    addAndMakeVisible(fftSizeLabel);
    fftSizeLabel.setText("FFT Size", juce::dontSendNotification);
    fftSizeLabel.setJustificationType(juce::Justification::centredLeft);

    // Hop Size slider
    addAndMakeVisible(hopSizeSlider);
    hopSizeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    hopSizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    hopSizeAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "hopSize", hopSizeSlider);

    addAndMakeVisible(hopSizeLabel);
    hopSizeLabel.setText("Hop Div", juce::dontSendNotification);
    hopSizeLabel.setJustificationType(juce::Justification::centredLeft);

    // Glide slider (only active in freeze mode)
    addAndMakeVisible(glideSlider);
    glideSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    glideSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    glideSlider.setTextValueSuffix(" ms");
    glideAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "glide", glideSlider);

    addAndMakeVisible(glideLabel);
    glideLabel.setText("Freeze Glide", juce::dontSendNotification);
    glideLabel.setJustificationType(juce::Justification::centredLeft);

    // Pitch Shift slider
    addAndMakeVisible(pitchShiftSlider);
    pitchShiftSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    pitchShiftSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    pitchShiftSlider.setTextValueSuffix(" st");
    pitchShiftAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "pitchShift", pitchShiftSlider);

    addAndMakeVisible(pitchShiftLabel);
    pitchShiftLabel.setText("Pitch Shift", juce::dontSendNotification);
    pitchShiftLabel.setJustificationType(juce::Justification::centredLeft);

    //==========================================================================
    // ADVANCED CONTROLS Setup (Right Column)
    //==========================================================================

    // HF Boost slider
    addAndMakeVisible(hfBoostSlider);
    hfBoostSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    hfBoostSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    hfBoostSlider.setTextValueSuffix(" %");
    hfBoostAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "hfBoost", hfBoostSlider);

    addAndMakeVisible(hfBoostLabel);
    hfBoostLabel.setText("HF Boost", juce::dontSendNotification);
    hfBoostLabel.setJustificationType(juce::Justification::centredLeft);

    // Micro Movement slider
    addAndMakeVisible(microMovementSlider);
    microMovementSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    microMovementSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    microMovementSlider.setTextValueSuffix(" %");
    microMovementAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "microMovement", microMovementSlider);

    addAndMakeVisible(microMovementLabel);
    microMovementLabel.setText("Micro Move", juce::dontSendNotification);
    microMovementLabel.setJustificationType(juce::Justification::centredLeft);

    // Window Type slider
    addAndMakeVisible(windowTypeSlider);
    windowTypeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    windowTypeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 100, 20);
    windowTypeAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "windowType", windowTypeSlider);

    addAndMakeVisible(windowTypeLabel);
    windowTypeLabel.setText("Window", juce::dontSendNotification);
    windowTypeLabel.setJustificationType(juce::Justification::centredLeft);

    // Crossfade Length slider
    addAndMakeVisible(crossfadeLengthSlider);
    crossfadeLengthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    crossfadeLengthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    crossfadeLengthSlider.setTextValueSuffix(" hops");
    crossfadeLengthAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "crossfadeLength", crossfadeLengthSlider);

    addAndMakeVisible(crossfadeLengthLabel);
    crossfadeLengthLabel.setText("X-Fade Len", juce::dontSendNotification);
    crossfadeLengthLabel.setJustificationType(juce::Justification::centredLeft);

    //==========================================================================
    // Waveform Display Setup
    //==========================================================================

    addAndMakeVisible(waveformDisplay);

    //==========================================================================
    // Spectrum Visualizer Setup
    //==========================================================================

    addAndMakeVisible(spectrumVisualizer);

    // Start timer for UI updates (30Hz)
    startTimerHz(30);
}

GrainfreezeAudioProcessorEditor::~GrainfreezeAudioProcessorEditor()
{
}

void GrainfreezeAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Fill background
    g.fillAll(juce::Colours::darkgrey);
}

void GrainfreezeAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    //==========================================================================
    // Top Control Bar Layout
    //==========================================================================

    auto topBar = bounds.removeFromTop(210);
    topBar.reduce(10, 10);

    // Buttons area (left side)
    auto buttonArea = topBar.removeFromLeft(120);
    loadButton.setBounds(buttonArea.removeFromTop(25));
    buttonArea.removeFromTop(5);
    playButton.setBounds(buttonArea.removeFromTop(25));
    buttonArea.removeFromTop(5);
    freezeButton.setBounds(buttonArea.removeFromTop(25));
    buttonArea.removeFromTop(5);
    syncToDawButton.setBounds(buttonArea.removeFromTop(25));

    topBar.removeFromLeft(15);

    // Parameters area (two columns)
    auto paramsArea = topBar;

    // Calculate column width
    int columnWidth = (paramsArea.getWidth() - 15) / 2;

    // Left column (Primary Controls)
    auto leftColumn = paramsArea.removeFromLeft(columnWidth);

    // Column header
    primaryControlsLabel.setBounds(leftColumn.removeFromTop(20));
    leftColumn.removeFromTop(3);

    // Time Stretch row
    auto stretchRow = leftColumn.removeFromTop(30);
    timeStretchLabel.setBounds(stretchRow.removeFromLeft(90));
    stretchRow.removeFromLeft(5);
    timeStretchSlider.setBounds(stretchRow);
    leftColumn.removeFromTop(2);

    // FFT Size row
    auto fftRow = leftColumn.removeFromTop(30);
    fftSizeLabel.setBounds(fftRow.removeFromLeft(90));
    fftRow.removeFromLeft(5);
    fftSizeSlider.setBounds(fftRow);
    leftColumn.removeFromTop(2);

    // Hop Size row
    auto hopRow = leftColumn.removeFromTop(30);
    hopSizeLabel.setBounds(hopRow.removeFromLeft(90));
    hopRow.removeFromLeft(5);
    hopSizeSlider.setBounds(hopRow);
    leftColumn.removeFromTop(2);

    // Glide row
    auto glideRow = leftColumn.removeFromTop(30);
    glideLabel.setBounds(glideRow.removeFromLeft(90));
    glideRow.removeFromLeft(5);
    glideSlider.setBounds(glideRow);
    leftColumn.removeFromTop(2);

    // Pitch row
    auto pitchRow = leftColumn.removeFromTop(30);
    pitchShiftLabel.setBounds(pitchRow.removeFromLeft(90));
    pitchRow.removeFromLeft(5);
    pitchShiftSlider.setBounds(pitchRow);

    // Gap between columns
    paramsArea.removeFromLeft(15);

    // Right column (Advanced Controls)
    auto rightColumn = paramsArea;

    // Column header
    advancedControlsLabel.setBounds(rightColumn.removeFromTop(20));
    rightColumn.removeFromTop(3);

    // HF Boost row
    auto hfRow = rightColumn.removeFromTop(30);
    hfBoostLabel.setBounds(hfRow.removeFromLeft(90));
    hfRow.removeFromLeft(5);
    hfBoostSlider.setBounds(hfRow);
    rightColumn.removeFromTop(2);

    // Micro Movement row
    auto microRow = rightColumn.removeFromTop(30);
    microMovementLabel.setBounds(microRow.removeFromLeft(90));
    microRow.removeFromLeft(5);
    microMovementSlider.setBounds(microRow);
    rightColumn.removeFromTop(2);

    // Window Type row
    auto windowRow = rightColumn.removeFromTop(30);
    windowTypeLabel.setBounds(windowRow.removeFromLeft(90));
    windowRow.removeFromLeft(5);
    windowTypeSlider.setBounds(windowRow);
    rightColumn.removeFromTop(2);

    // Crossfade Length row
    auto xfadeRow = rightColumn.removeFromTop(30);
    crossfadeLengthLabel.setBounds(xfadeRow.removeFromLeft(90));
    xfadeRow.removeFromLeft(5);
    crossfadeLengthSlider.setBounds(xfadeRow);

    //==========================================================================
    // Status Bar Layout (Bottom of controls)
    //==========================================================================

    auto statusArea = bounds.removeFromBottom(40);
    auto recommendedArea = statusArea.removeFromRight(280);
    statusLabel.setBounds(statusArea);
    recommendedLabel.setBounds(recommendedArea);

    //==========================================================================
    // Spectrum Visualizer Layout (Very Bottom)
    //==========================================================================

    auto spectrumArea = bounds.removeFromBottom(120);
    spectrumArea.reduce(10, 5);
    spectrumVisualizer.setBounds(spectrumArea);

    //==========================================================================
    // Waveform Display Layout (Center)
    //==========================================================================

    bounds.reduce(10, 10);
    waveformDisplay.setBounds(bounds);
}

void GrainfreezeAudioProcessorEditor::timerCallback()
{
    // Update waveform display
    waveformDisplay.repaint();

    // Update spectrum visualizer with current FFT data
    const auto& magnitudes = audioProcessor.getSpectrumMagnitudes();
    if (!magnitudes.empty())
    {
        spectrumVisualizer.updateSpectrum(magnitudes, 
            audioProcessor.getCurrentFftSize(), 
            audioProcessor.getCurrentSampleRate());
    }

    // Update freeze button visual state
    bool isFreezeMode = audioProcessor.freezeModeParam->get();
    freezeButton.setToggleState(isFreezeMode, juce::dontSendNotification);
    freezeButton.setColour(juce::TextButton::buttonColourId,
        isFreezeMode ? juce::Colours::orange : juce::Colours::grey);

    // Update status text
    if (audioProcessor.isAudioLoaded())
    {
        float stretch = audioProcessor.timeStretch->get();
        juce::String status = "Loaded: " + audioProcessor.getLoadedFileName() + " | ";

        if (isFreezeMode)
        {
            status += "FREEZE MODE";
        }
        else if (audioProcessor.isPlaying())
        {
            status += "PLAYING at ";
            if (stretch < 1.0f)
                status += juce::String(1.0f / stretch, 2) + "x speed";
            else if (stretch > 1.0f)
                status += juce::String(stretch, 2) + "x slower";
            else
                status += "normal speed";
        }
        else
        {
            status += "STOPPED";
        }

        statusLabel.setText(status, juce::dontSendNotification);

        // Update play button color
        playButton.setColour(juce::TextButton::buttonColourId,
            audioProcessor.isPlaying() ? juce::Colours::green : juce::Colours::grey);
            
        // Enable/disable glide slider based on freeze mode
        glideSlider.setEnabled(isFreezeMode);
        glideLabel.setEnabled(isFreezeMode);
    }
    else
    {
        statusLabel.setText("No audio loaded", juce::dontSendNotification);
    }
}

void GrainfreezeAudioProcessorEditor::loadAudioFile()
{
    // Create file chooser for audio files
    fileChooser = std::make_unique<juce::FileChooser>("Select an audio file to load...",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.wav;*.mp3;*.aif;*.aiff;*.flac");

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    // Open file chooser asynchronously
    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file.existsAsFile())
            {
                // Load selected file and stop playback
                audioProcessor.loadAudioFile(file);
                
                if (audioProcessor.isAudioLoaded())
                {
                    audioProcessor.setPlaying(false);
                    waveformDisplay.repaint();
                }
            }
        });
}