/*
 ==============================================================================

 TextParameter - a host-visible parameter carrying a string payload.

 JUCE has no built-in text parameter, so this reports a constant 0.0f as its
 numeric value and keeps the real content in getText()/setKeyText(). It exists
 so the AES passphrases show up in the host's parameter list and travel with
 the plugin's state.

 ==============================================================================
 */

#pragma once

#include <JuceHeader.h>

//==============================================================================
class TextParameter : public juce::AudioProcessorParameter
{
public:
    TextParameter (const juce::String& paramId,
                   const juce::String& name,
                   const juce::String& defaultValue)
    : parameterID (paramId), parameterName (name), value (defaultValue) {}

    float getValue() const override                        { return 0.0f; }
    void setValue (float) override                         {}
    float getDefaultValue() const override                 { return 0.0f; }
    juce::String getName (int) const override              { return parameterName; }
    juce::String getLabel() const override                 { return {}; }

    float getValueForText (const juce::String&) const override { return 0.0f; }
    juce::String getText (float, int maximumLength) const override
    {
        return value.substring (0, maximumLength);
    }

    bool isDiscrete() const override                       { return false; }
    bool isBoolean() const override                        { return false; }
    int getNumSteps() const override                       { return 0; }
    bool isMetaParameter() const override                  { return false; }
    Category getCategory() const override                  { return Category::genericParameter; }

    /** Replaces the string payload and notifies listeners, which is how the
        processor learns that a passphrase changed. */
    void setKeyText (const juce::String& newText)
    {
        value = newText;
        sendValueChangedMessageToListeners (0.0f);
    }

    juce::String getKeyText() const                        { return value; }

private:
    juce::String parameterID;
    juce::String parameterName;
    juce::String value;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TextParameter)
};
