/*
  ==============================================================================

    Eudyptula - live AES-ECB "encryption" audio effect (editor)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class EudyptulaEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    EudyptulaEditor (Eudyptula&);
    ~EudyptulaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Eudyptula& audioProcessor;

    Label  modeLabel;
    ComboBox modeBox;

    Slider wetDrySlider;    Label wetDryLabel;
    Label  harmonicLabel;
    ComboBox harmonicBox;
    Slider quantizeSlider;  Label quantizeLabel;
    Slider corruptSlider;   Label corruptLabel;
    Slider holdSlider;      Label holdLabel;
    Slider sensSlider;      Label sensLabel;
    Slider gainSlider;      Label gainLabel;

    TextEditor keyInput;    Label keyLabel;
    TextButton randomizeButton;
    TextEditor decKeyInput; Label decKeyLabel;
    TextButton randomizeDecButton;

    Label  creditLabel;

    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> wetDryAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> harmonicAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> quantizeAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> corruptAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> holdAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> sensAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> gainAttachment;

    /** Pushes the edited passphrase to the processor, substituting a default
        rather than letting the key go empty. */
    void keyInputChanged();
    void decKeyInputChanged();

    /** The key fields have no attachment to follow the processor, so poll it
        and rewrite them only when the text differs, which leaves a field being
        typed into alone. */
    void timerCallback() override;
    void refreshKeyFields();

    /** Fills a key field with 16 random printable characters, notifying so the
        onTextChange handler forwards it to the processor. */
    static void randomizeInto (TextEditor& target);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EudyptulaEditor)
};
