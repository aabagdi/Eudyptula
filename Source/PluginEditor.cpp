/*
  ==============================================================================

    Eudyptula - live AES-ECB "encryption" audio effect (editor)

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    /** Applies this plugin's common look to a parameter slider. */
    void styleSlider (Slider& s)
    {
        s.setSliderStyle (Slider::LinearHorizontal);
        s.setTextBoxStyle (Slider::TextBoxRight, false, 70, 20);
        s.setColour (Slider::textBoxTextColourId, Colours::white);
        s.setColour (Slider::trackColourId, Colours::lightblue);
    }
}

EudyptulaEditor::EudyptulaEditor (Eudyptula& p)
: AudioProcessorEditor (&p), audioProcessor (p)
{
    modeBox.addItem ("Encrypt", 1);
    modeBox.addItem ("Decrypt", 2);
    addAndMakeVisible (modeBox);
    modeLabel.setText ("Mode", dontSendNotification);
    modeLabel.setColour (Label::textColourId, Colours::white);
    addAndMakeVisible (modeLabel);
    modeAttachment.reset (new AudioProcessorValueTreeState::ComboBoxAttachment (
        audioProcessor.parameters, "mode", modeBox));

    harmonicBox.addItemList (StringArray { "x0.5", "x1", "x2", "x4", "x8" }, 1);
    addAndMakeVisible (harmonicBox);
    harmonicAttachment.reset (new AudioProcessorValueTreeState::ComboBoxAttachment (
        audioProcessor.parameters, "harmonic", harmonicBox));

    styleSlider (wetDrySlider);    wetDrySlider.setRange (0.0, 1.0, 0.01);
    styleSlider (quantizeSlider);  quantizeSlider.setRange (0, 16, 1);
    styleSlider (corruptSlider);   corruptSlider.setRange (0, 8, 1);
    styleSlider (holdSlider);      holdSlider.setRange (1, 16, 1);
    styleSlider (sensSlider);
    sensSlider.setRange (0.0, 100.0, 1.0);
    sensSlider.setTextValueSuffix (" %");
    styleSlider (gainSlider);      gainSlider.setRange (-48.0, 24.0, 0.1);
    gainSlider.setNumDecimalPlacesToDisplay (1);

    for (auto* s : { &wetDrySlider, &quantizeSlider,
                     &corruptSlider, &holdSlider, &sensSlider, &gainSlider })
        addAndMakeVisible (s);

    auto setupLabel = [this] (Label& label, const String& text)
    {
        label.setText (text, dontSendNotification);
        label.setColour (Label::textColourId, Colours::white);
        addAndMakeVisible (label);
    };
    setupLabel (wetDryLabel,    "Mix");
    setupLabel (harmonicLabel,  "Harmonic");
    setupLabel (quantizeLabel,  "Quantize");
    setupLabel (corruptLabel,   "Corrupt");
    setupLabel (holdLabel,      "Hold");
    setupLabel (sensLabel,      "Sensitivity");
    setupLabel (gainLabel,      "Gain (dB)");

    wetDryAttachment.reset    (new AudioProcessorValueTreeState::SliderAttachment (audioProcessor.parameters, "wetdry",    wetDrySlider));
    quantizeAttachment.reset  (new AudioProcessorValueTreeState::SliderAttachment (audioProcessor.parameters, "quantize",  quantizeSlider));
    corruptAttachment.reset   (new AudioProcessorValueTreeState::SliderAttachment (audioProcessor.parameters, "corrupt",   corruptSlider));
    holdAttachment.reset      (new AudioProcessorValueTreeState::SliderAttachment (audioProcessor.parameters, "hold",      holdSlider));
    sensAttachment.reset      (new AudioProcessorValueTreeState::SliderAttachment (audioProcessor.parameters, "sensitivity", sensSlider));
    gainAttachment.reset      (new AudioProcessorValueTreeState::SliderAttachment (audioProcessor.parameters, "gain",      gainSlider));

    keyInput.setMultiLine (false);
    keyInput.setReturnKeyStartsNewLine (false);
    keyInput.setScrollbarsShown (false);
    keyInput.setInputRestrictions (32);
    keyInput.setText (audioProcessor.getCurrentKey(), dontSendNotification);
    keyInput.onTextChange = [this] { keyInputChanged(); };
    keyInput.setColour (TextEditor::textColourId, Colours::white);
    keyInput.setColour (TextEditor::backgroundColourId, Colours::darkgrey);
    addAndMakeVisible (keyInput);

    keyLabel.setText ("Enc Key", dontSendNotification);
    keyLabel.setColour (Label::textColourId, Colours::white);
    addAndMakeVisible (keyLabel);

    randomizeButton.setButtonText ("Random");
    randomizeButton.onClick = [this] { randomizeInto (keyInput); };
    addAndMakeVisible (randomizeButton);

    decKeyInput.setMultiLine (false);
    decKeyInput.setReturnKeyStartsNewLine (false);
    decKeyInput.setScrollbarsShown (false);
    decKeyInput.setInputRestrictions (32);
    decKeyInput.setText (audioProcessor.getCurrentDecryptKey(), dontSendNotification);
    decKeyInput.onTextChange = [this] { decKeyInputChanged(); };
    decKeyInput.setColour (TextEditor::textColourId, Colours::white);
    decKeyInput.setColour (TextEditor::backgroundColourId, Colours::darkgrey);
    addAndMakeVisible (decKeyInput);

    decKeyLabel.setText ("Dec Key", dontSendNotification);
    decKeyLabel.setColour (Label::textColourId, Colours::white);
    addAndMakeVisible (decKeyLabel);

    randomizeDecButton.setButtonText ("Random");
    randomizeDecButton.onClick = [this] { randomizeInto (decKeyInput); };
    addAndMakeVisible (randomizeDecButton);

    creditLabel.setText ("AES-256 via tiny-AES-c by kokke (public domain)\n"
                         "K-weighting adapted from the LUFS Meter by Samuel Gaehwiler, "
                         "Klangfreund (MIT)",
                         dontSendNotification);
    creditLabel.setJustificationType (Justification::centred);
    creditLabel.setFont (Font (FontOptions (11.0f)));
    creditLabel.setColour (Label::textColourId, Colours::lightgrey);
    addAndMakeVisible (creditLabel);

    setSize (420, 500);

    startTimerHz (10);
}

EudyptulaEditor::~EudyptulaEditor()
{
    stopTimer();
}

void EudyptulaEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::darkgrey);

    g.setColour (Colours::white);
    g.setFont (16.0f);
    g.drawText ("Eudyptula", getLocalBounds().removeFromTop (30),
                Justification::centred, true);
}

void EudyptulaEditor::resized()
{
    const int padding = 20;
    const int rowHeight = 28;
    const int labelWidth = 100;
    const int spacing = 8;

    auto area = getLocalBounds().reduced (padding);

    creditLabel.setBounds (area.removeFromBottom (44));
    area.removeFromBottom (6);

    area.removeFromTop (30);

    auto layoutRow = [&] (Label& label, Component& control)
    {
        auto row = area.removeFromTop (rowHeight);
        label.setBounds (row.removeFromLeft (labelWidth));
        control.setBounds (row);
        area.removeFromTop (spacing);
    };

    layoutRow (modeLabel,      modeBox);
    layoutRow (wetDryLabel,    wetDrySlider);
    layoutRow (harmonicLabel,  harmonicBox);
    layoutRow (quantizeLabel,  quantizeSlider);
    layoutRow (corruptLabel,   corruptSlider);
    layoutRow (holdLabel,      holdSlider);
    layoutRow (sensLabel,      sensSlider);
    layoutRow (gainLabel,      gainSlider);

    auto layoutKeyRow = [&] (Label& label, TextEditor& field, TextButton& button)
    {
        auto row = area.removeFromTop (rowHeight);
        label.setBounds (row.removeFromLeft (labelWidth));
        button.setBounds (row.removeFromRight (80));
        row.removeFromRight (spacing);
        field.setBounds (row);
        area.removeFromTop (spacing);
    };

    layoutKeyRow (keyLabel,    keyInput,    randomizeButton);
    layoutKeyRow (decKeyLabel, decKeyInput, randomizeDecButton);
}

void EudyptulaEditor::timerCallback()
{
    refreshKeyFields();
}

void EudyptulaEditor::refreshKeyFields()
{
    const auto encKey = audioProcessor.getCurrentKey();
    if (keyInput.getText() != encKey)
        keyInput.setText (encKey, dontSendNotification);

    const auto decKey = audioProcessor.getCurrentDecryptKey();
    if (decKeyInput.getText() != decKey)
        decKeyInput.setText (decKey, dontSendNotification);
}

void EudyptulaEditor::keyInputChanged()
{
    String newKey = keyInput.getText();
    if (newKey.isNotEmpty())
    {
        audioProcessor.setEncryptionKey (newKey);
    }
    else
    {
        keyInput.setText ("DefaultKey123", dontSendNotification);
        audioProcessor.setEncryptionKey ("DefaultKey123");
    }
}

void EudyptulaEditor::decKeyInputChanged()
{
    String newKey = decKeyInput.getText();
    if (newKey.isNotEmpty())
    {
        audioProcessor.setDecryptionKey (newKey);
    }
    else
    {
        decKeyInput.setText ("WrongKey456", dontSendNotification);
        audioProcessor.setDecryptionKey ("WrongKey456");
    }
}

void EudyptulaEditor::randomizeInto (TextEditor& target)
{
    static constexpr char chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
        "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

    static constexpr int numChars = (int) (sizeof (chars) - 1);

    juce::Random rng;
    String k;
    for (int i = 0; i < 16; ++i)
        k += chars[rng.nextInt (numChars)];

    target.setText (k, sendNotification);
}
