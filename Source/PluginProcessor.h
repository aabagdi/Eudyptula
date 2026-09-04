/*
 ==============================================================================

 Eudyptula - live AES-ECB "encryption" audio effect

 ==============================================================================
 */

#pragma once

#include <JuceHeader.h>
#include <array>

#include "DSP/ChannelStream.h"
#include "DSP/CipherEngine.h"
#include "DSP/KWeightingFilter.h"
#include "DSP/PitchTracker.h"
#include "DSP/SoftLimiter.h"
#include "Parameters/TextParameter.h"

//==============================================================================
/** Resamples the input onto a pitch-tracked carrier, quantises it to 16-bit
    words, runs each 8-sample block through AES-ECB, and mixes the result back
    against the dry signal under a noise gate and a K-weighted loudness match.
*/
class Eudyptula  : public juce::AudioProcessor,
                public juce::AudioProcessorParameter::Listener
{
public:
    //==============================================================================
    Eudyptula();
    ~Eudyptula() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    /** Sets the encrypt-direction passphrase via its TextParameter, so the host
        sees the change and the cipher's key schedules get rebuilt. */
    void setEncryptionKey (const String& newKey);
    String getCurrentKey() const { return encryptionKey; }

    /** As setEncryptionKey(), for the decrypt direction. */
    void setDecryptionKey (const String& newKey);
    String getCurrentDecryptKey() const { return decryptionKey; }

    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int, bool) override {}

    juce::AudioProcessorValueTreeState parameters;

private:
    //==============================================================================
    std::atomic<float>* wetDryParameter = nullptr;

    /** Number of quantisation levels, where 0 bypasses quantisation and passes
        the full 16-bit word to the cipher. */
    std::atomic<float>* quantizationParameter = nullptr;

    std::atomic<float>* gainParameter = nullptr;
    std::atomic<float>* harmonicParameter = nullptr;
    std::atomic<float>* corruptParameter = nullptr;
    std::atomic<float>* holdParameter = nullptr;
    std::atomic<float>* sensitivityParameter = nullptr;
    std::atomic<float>* modeParameter = nullptr;

    static constexpr float kFixedCarrierHz = 220.0f;

    static constexpr float kGateReleaseSeconds = 0.015f;

    static constexpr float kGateKneeDb = 12.0f;

    static constexpr float kSensThresholdMinDb = -20.0f;
    static constexpr float kSensThresholdMaxDb = -80.0f;

    static constexpr float kQuantiseTarget = 0.35f;
    static constexpr int   kMaxHoldRepeats = 16;

    TextParameter* encKeyParameter = nullptr;
    String encryptionKey = "EncKey123";
    TextParameter* decKeyParameter = nullptr;
    String decryptionKey = "DecKey456";

    CipherEngine cipher;
    PitchTracker pitchTracker;

    KWeightingCoeffs kWeighting;

    float kDryLoudSq = 0.0f;
    float kOutLoudSq = 0.0f;

    float lastTrackedHz = 0.0f;
    double smoothedRatio = 0.0;

    static constexpr int maxChannels = 2;
    std::array<ChannelStream, (size_t) maxChannels> streams;

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Eudyptula)
};
