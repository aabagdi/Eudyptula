/*
 ==============================================================================

 Eudyptula - live AES-ECB "encryption" audio effect (processor)

 ==============================================================================
 */

#include "PluginProcessor.h"
#include "PluginEditor.h"

static_assert (ChannelStream::kBlockSamples == CipherEngine::kBlockSamples,
               "ChannelStream and CipherEngine disagree on the AES block size");

//==============================================================================
Eudyptula::Eudyptula()
: AudioProcessor (BusesProperties()
                  .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
  parameters (*this, nullptr, "Parameters",
              {
                  std::make_unique<juce::AudioParameterFloat> (
                      "wetdry", "Mix", 0.0f, 1.0f, 0.5f),
                  std::make_unique<juce::AudioParameterInt> (
                      "quantize", "Quantize", 0, 16, 8,
                      juce::AudioParameterIntAttributes()
                          .withStringFromValueFunction ([] (int v, int)
                          {
                              return v <= 0 ? juce::String ("Off")
                                            : juce::String (juce::jmax (2, v));
                          })
                          .withValueFromStringFunction ([] (const juce::String& text)
                          {
                              return text.trim().equalsIgnoreCase ("off")
                                   ? 0 : text.getIntValue();
                          })),
                  std::make_unique<juce::AudioParameterFloat> (
                      "gain", "Gain",
                      [] {
                          juce::NormalisableRange<float> r (-48.0f, 24.0f);
                          r.setSkewForCentre (0.0f);
                          return r;
                      }(),
                      0.0f),
                  std::make_unique<juce::AudioParameterChoice> (
                      "harmonic", "Harmonic",
                      juce::StringArray { "x0.5", "x1", "x2", "x4", "x8" }, 1),
                  std::make_unique<juce::AudioParameterInt> (
                      "corrupt", "Corrupt", 0, 8, 8),
                  std::make_unique<juce::AudioParameterInt> (
                      "hold", "Hold", 1, 16, 1),
                  std::make_unique<juce::AudioParameterFloat> (
                      "sensitivity", "Sensitivity", 0.0f, 100.0f, 50.0f),
                  std::make_unique<juce::AudioParameterChoice> (
                      "mode", "Mode", juce::StringArray { "Encrypt", "Decrypt" }, 0)
              })
{
    wetDryParameter       = parameters.getRawParameterValue ("wetdry");
    quantizationParameter = parameters.getRawParameterValue ("quantize");
    gainParameter         = parameters.getRawParameterValue ("gain");
    harmonicParameter     = parameters.getRawParameterValue ("harmonic");
    corruptParameter      = parameters.getRawParameterValue ("corrupt");
    holdParameter         = parameters.getRawParameterValue ("hold");
    sensitivityParameter  = parameters.getRawParameterValue ("sensitivity");
    modeParameter         = parameters.getRawParameterValue ("mode");

    encKeyParameter = new TextParameter ("enckey", "Encryption Key", encryptionKey);
    addParameter (encKeyParameter);
    encKeyParameter->addListener (this);

    decKeyParameter = new TextParameter ("deckey", "Decryption Key", decryptionKey);
    addParameter (decKeyParameter);
    decKeyParameter->addListener (this);

    cipher.setKeys (encryptionKey, decryptionKey);
    cipher.primeNow();
}

Eudyptula::~Eudyptula()
{
    if (encKeyParameter != nullptr)
        encKeyParameter->removeListener (this);
    if (decKeyParameter != nullptr)
        decKeyParameter->removeListener (this);
}

//==============================================================================
const juce::String Eudyptula::getName() const               { return JucePlugin_Name; }
bool Eudyptula::acceptsMidi() const                         { return false; }
bool Eudyptula::producesMidi() const                        { return false; }
bool Eudyptula::isMidiEffect() const                        { return false; }
double Eudyptula::getTailLengthSeconds() const              { return 0.0; }

int Eudyptula::getNumPrograms()                             { return 1; }
int Eudyptula::getCurrentProgram()                          { return 0; }
void Eudyptula::setCurrentProgram (int)                     {}
const String Eudyptula::getProgramName (int)                { return {}; }
void Eudyptula::changeProgramName (int, const String&)      {}

//==============================================================================
void Eudyptula::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;

    kWeighting.prepare (sampleRate);

    for (auto& s : streams)
        s.reset();

    pitchTracker.reset();
    kDryLoudSq = 0.0f;
    kOutLoudSq = 0.0f;
    lastTrackedHz = 0.0f;
    smoothedRatio = 0.0;

    setLatencySamples (0);
}

void Eudyptula::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool Eudyptula::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != AudioChannelSet::mono() && mainOut != AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != mainOut)
        return false;

    return true;
}
#endif

void Eudyptula::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    cipher.refreshIfDirty();

    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    const float mix     = wetDryParameter->load();
    const float dryGain = 1.0f - mix;
    const float wetGain = mix;
    const float outGain = std::pow (10.0f, gainParameter->load() / 20.0f);

    const int   quantSetting = (int) quantizationParameter->load();
    const bool  quantise     = quantSetting > 0;
    const int   numLevels    = juce::jmax (2, quantSetting);
    const float qStep        = 1.9f / (float) numLevels;

    const int   corrupt   = juce::jlimit (0, CipherEngine::kBlockSamples, (int) corruptParameter->load());
    const int   hold      = juce::jlimit (1, kMaxHoldRepeats, (int) holdParameter->load());
    const bool  decrypt   = modeParameter->load() > 0.5f;

    const double hostRate = juce::jmax (1.0, currentSampleRate);
    const int channelsToProcess = juce::jmin (numCh, maxChannels);

    static constexpr float kHarmonics[] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
    const int   hIdx     = juce::jlimit (0, 4, (int) harmonicParameter->load());
    const float harmonic = kHarmonics[hIdx];

    {
        const float invCh = 1.0f / (float) juce::jmax (1, channelsToProcess);
        for (int i = 0; i < numSamples; ++i)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < channelsToProcess; ++ch)
                mono += buffer.getReadPointer (ch)[i];
            pitchTracker.push (mono * invCh, hostRate);
        }

        if (pitchTracker.freqHz > 0.0f)
            lastTrackedHz = pitchTracker.freqHz;
    }

    const float baseHz = lastTrackedHz > 0.0f ? lastTrackedHz : kFixedCarrierHz;

    const float targetHz = juce::jlimit (20.0f, 5000.0f, baseHz * harmonic);
    const double targetRatio = juce::jlimit (1.0e-4,
                                             1.0,
                                             (double) CipherEngine::kBlockSamples * (double) targetHz / hostRate);

    if (smoothedRatio <= 0.0)
        smoothedRatio = targetRatio;

    constexpr double kGlideSeconds = 0.040;
    const double glide      = 1.0 - std::exp (-(double) numSamples / (kGlideSeconds * hostRate));
    const double ratioStart = smoothedRatio;
    const double ratioEnd   = ratioStart + (targetRatio - ratioStart) * glide;
    const double ratioInc   = (ratioEnd - ratioStart) / (double) juce::jmax (1, numSamples);
    smoothedRatio = ratioEnd;

    const float sr           = (float) juce::jmax (1.0, currentSampleRate);
    const float gateRelease  = std::exp (-1.0f / (kGateReleaseSeconds * sr));
    const float gateSmooth   = 1.0f - std::exp (-1.0f / (0.003f * sr));
    const float sens = juce::jlimit (0.0f, 100.0f, sensitivityParameter->load()) * 0.01f;
    const float gateThresholdDb = kSensThresholdMinDb
                                + sens * (kSensThresholdMaxDb - kSensThresholdMinDb);
    const float gateThreshold = juce::Decibels::decibelsToGain (gateThresholdDb);

    const float envCoeff = 1.0f - std::exp (-1.0f / (0.030f * sr));

    const float loudCoeff = 1.0f - std::exp (-1.0f / (0.400f * sr));

    const float cipherFrac = (float) corrupt / (float) CipherEngine::kBlockSamples;
    const float plainFrac  = 1.0f - cipherFrac;

    const float expectedWetRms = CipherEngine::kCipherRms;

    const float coherentSum = dryGain + wetGain * std::sqrt (plainFrac);
    const float mixPower    = coherentSum * coherentSum + wetGain * wetGain * cipherFrac;
    const float mixComp     = mixPower > 1.0e-6f ? 1.0f / std::sqrt (mixPower) : 1.0f;

    cipher.beginBlock (decrypt);

    CipherEngine::Block outBlk {};

    float* chanData[maxChannels] = {};
    for (int ch = 0; ch < channelsToProcess; ++ch)
        chanData[ch] = buffer.getWritePointer (ch);

    double ratio = ratioStart;

    for (int i = 0; i < numSamples; ++i)
    {
        float dryIn[maxChannels]  = {};
        float preOut[maxChannels] = {};

        for (int ch = 0; ch < channelsToProcess; ++ch)
        {
            auto& st = streams[(size_t) ch];
            const float dry = chanData[ch][i];
            dryIn[ch] = dry;

            st.phase += ratio;
            while (st.phase >= 1.0)
            {
                st.phase -= 1.0;

                const float level = std::sqrt (st.envSq);
                const float norm  = level > 1.0e-6f
                                  ? dry * (kQuantiseTarget / level)
                                  : 0.0f;

                const float clamped = juce::jlimit (-0.95f, 0.95f, norm);
                const float q = quantise ? std::round (clamped / qStep) * qStep
                                         : clamped;
                const int16_t s = (int16_t) std::round (q * CipherEngine::kSampleScale);

                st.inAccum[(size_t) st.inAccumLen++] = s;
                if (st.inAccumLen == CipherEngine::kBlockSamples)
                {
                    cipher.encode (st.inAccum.data(), corrupt, outBlk);

                    if (hold <= 1)
                    {
                        for (int j = 0; j < CipherEngine::kBlockSamples; ++j)
                            st.push (outBlk[(size_t) j]);
                    }
                    else
                    {
                        if (st.repeatCounter == 0)
                            st.lastBlock = outBlk;

                        for (int j = 0; j < CipherEngine::kBlockSamples; ++j)
                            st.push (st.lastBlock[(size_t) j]);

                        st.repeatCounter = (st.repeatCounter + 1) % hold;
                    }

                    st.inAccumLen = 0;
                }

                st.currentOut = st.pop();
            }

            const float wet = st.currentOut;

            st.envSq += (dry * dry - st.envSq) * envCoeff;
            const float env = std::sqrt (st.envSq);

            const float wetScale = env / expectedWetRms;

            const float mag = std::abs (dry);
            st.gateEnv = juce::jmax (mag, st.gateEnv * gateRelease);

            const float overDb = 20.0f * std::log10 (juce::jmax (1.0e-9f, st.gateEnv / gateThreshold));
            const float gateTarget = juce::jlimit (0.0f, 1.0f, overDb / kGateKneeDb);
            st.gateGain += (gateTarget - st.gateGain) * gateSmooth;
            const float gatedWet = wet * st.gateGain * wetScale;

            preOut[ch] = (dry * dryGain + gatedWet * wetGain) * mixComp;
        }

        ratio += ratioInc;

        float sumDrySq = 0.0f, sumOutSq = 0.0f;
        for (int ch = 0; ch < channelsToProcess; ++ch)
        {
            auto& st = streams[(size_t) ch];
            const float kDry = st.dryK.process (kWeighting, dryIn[ch]);
            const float kOut = st.outK.process (kWeighting, preOut[ch]);
            sumDrySq += kDry * kDry;
            sumOutSq += kOut * kOut;
        }

        kDryLoudSq += (sumDrySq - kDryLoudSq) * loudCoeff;
        kOutLoudSq += (sumOutSq - kOutLoudSq) * loudCoeff;

        const float dryLoud = std::sqrt (kDryLoudSq);
        const float outLoud = std::sqrt (kOutLoudSq);
        const float trim = outLoud > 1.0e-6f
                         ? juce::jlimit (0.5f, 2.0f, dryLoud / outLoud)
                         : 1.0f;

        for (int ch = 0; ch < channelsToProcess; ++ch)
            chanData[ch][i] = SoftLimiter::process (preOut[ch] * trim * outGain);
    }

    for (int ch = channelsToProcess; ch < numCh; ++ch)
        buffer.clear (ch, 0, numSamples);
}

//==============================================================================
bool Eudyptula::hasEditor() const { return true; }

AudioProcessorEditor* Eudyptula::createEditor()
{
    return new EudyptulaEditor (*this);
}

//==============================================================================
void Eudyptula::getStateInformation (MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<XmlElement> xml (state.createXml());

    if (xml != nullptr)
    {
        xml->setAttribute ("encKey", encryptionKey);
        xml->setAttribute ("decKey", decryptionKey);
    }

    copyXmlToBinary (*xml, destData);
}

void Eudyptula::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState == nullptr)
        return;

    if (xmlState->hasAttribute ("encKey"))
        setEncryptionKey (xmlState->getStringAttribute ("encKey", encryptionKey));

    if (xmlState->hasAttribute ("decKey"))
        setDecryptionKey (xmlState->getStringAttribute ("decKey", decryptionKey));

    parameters.replaceState (ValueTree::fromXml (*xmlState));
}

//==============================================================================
void Eudyptula::setEncryptionKey (const String& newKey)
{
    if (encKeyParameter != nullptr && newKey != encKeyParameter->getKeyText())
        encKeyParameter->setKeyText (newKey);
}

void Eudyptula::setDecryptionKey (const String& newKey)
{
    if (decKeyParameter != nullptr && newKey != decKeyParameter->getKeyText())
        decKeyParameter->setKeyText (newKey);
}

void Eudyptula::parameterValueChanged (int parameterIndex, float /*newValue*/)
{
    bool changed = false;

    if (encKeyParameter != nullptr && parameterIndex == encKeyParameter->getParameterIndex())
    {
        encryptionKey = encKeyParameter->getKeyText();
        changed = true;
    }
    else if (decKeyParameter != nullptr && parameterIndex == decKeyParameter->getParameterIndex())
    {
        decryptionKey = decKeyParameter->getKeyText();
        changed = true;
    }

    if (changed)
        cipher.setKeys (encryptionKey, decryptionKey);
}

//==============================================================================
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Eudyptula();
}
