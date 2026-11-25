#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SP404DanceAudioProcessor::SP404DanceAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout SP404DanceAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<juce::AudioParameterFloat> ("sensitivity", "Sensitivity", 0.5f, 4.0f, 2.10f));
    p.push_back (std::make_unique<juce::AudioParameterBool>  ("enableLow",   "Low (Kick)",  true));
    p.push_back (std::make_unique<juce::AudioParameterBool>  ("enableMid",   "Mid (Snare)", true));
    p.push_back (std::make_unique<juce::AudioParameterBool>  ("enableHigh",  "High (Hat)",  false));

    p.push_back (std::make_unique<juce::AudioParameterFloat> ("threshLow",   "Low Thresh",  0.05f, 0.85f, 0.34f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("threshMid",   "Mid Thresh",  0.05f, 0.85f, 0.28f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("threshHigh",  "High Thresh", 0.05f, 0.85f, 0.20f));

    p.push_back (std::make_unique<juce::AudioParameterInt> ("padLow",  "Low Pad",  1, 12, 1));
    p.push_back (std::make_unique<juce::AudioParameterInt> ("padMid",  "Mid Pad",  1, 12, 7));
    p.push_back (std::make_unique<juce::AudioParameterInt> ("padHigh", "High Pad", 1, 12, 4));

    p.push_back (std::make_unique<juce::AudioParameterFloat> ("attackMs",   "Attack ms",   16.0f, 250.0f, 42.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("lingerMs",   "Stay on Pad", 150.0f, 2500.0f, 950.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("releaseMs",  "Retreat ms",  60.0f, 600.0f, 240.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("cooldownMs", "Cooldown ms", 25.0f, 300.0f, 75.0f));
    p.push_back (std::make_unique<juce::AudioParameterBool>  ("busyIntercept", "Intercept Mid-Hit", true));

    p.push_back (std::make_unique<juce::AudioParameterFloat> ("pixelScale", "Pixel Scale", 1.0f, 8.0f, 3.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("shakalGrit", "Shakal Grit", 0.0f, 1.0f, 0.45f));
    p.push_back (std::make_unique<juce::AudioParameterInt>   ("fpsLock",    "Choppy FPS",  10, 60, 24));
    p.push_back (std::make_unique<juce::AudioParameterBool>  ("mirrorRight","Mirror Right Hand", true));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("handScale",  "Hand Scale",  0.65f, 1.45f, 1.00f));

    return { p.begin(), p.end() };
}

void SP404DanceAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    lowLpState = 0.0f;
    highLpState = 0.0f;
    detLow = {};
    detMid = {};
    detHigh = {};
}

bool SP404DanceAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet()
        && ! layouts.getMainInputChannelSet().isDisabled();
}

bool SP404DanceAudioProcessor::BandDetector::step (float sampleAbs, float threshold, float sensitivity,
                                                   float fastA, float fastR, float slowA, float slowR, int minGapSamples)
{
    fastEnv += (sampleAbs - fastEnv) * (sampleAbs > fastEnv ? fastA : fastR);
    slowEnv += (sampleAbs - slowEnv) * (sampleAbs > slowEnv ? slowA : slowR);

    const float diff = juce::jmax (0.0f, fastEnv - slowEnv * 0.68f);
    const float transient = juce::jmin (1.0f, diff * sensitivity * 1.45f + juce::jmax (0.0f, sampleAbs - slowEnv * 0.8f) * sensitivity * 0.22f);

    if (! armed && transient < threshold * 0.62f)
        armed = true;

    if (cooldownSamples > 0)
        --cooldownSamples;

    if (armed && transient >= threshold && cooldownSamples == 0)
    {
        armed = false;
        cooldownSamples = minGapSamples;
        return true;
    }
    return false;
}

void SP404DanceAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Audio passes through 100% untouched (zero coloration / zero delay)
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm, std::memory_order_relaxed);
        }
    }

    const int numSamples = buffer.getNumSamples();
    const int numChannels = getTotalNumInputChannels();
    if (numSamples == 0 || numChannels == 0)
        return;

    const float sr = (float) currentSampleRate;
    const float twoPi = 6.2831853f;
    const float lowCoeff  = 1.0f - std::exp (-twoPi * 185.0f / sr);
    const float highCoeff = 1.0f - std::exp (-twoPi * 2600.0f / sr);

    const float fastA = 1.0f - std::exp (-1.0f / (0.0022f * sr));
    const float fastR = 1.0f - std::exp (-1.0f / (0.024f * sr));
    const float slowA = 1.0f - std::exp (-1.0f / (0.038f * sr));
    const float slowR = 1.0f - std::exp (-1.0f / (0.150f * sr));

    const float sens  = apvts.getRawParameterValue ("sensitivity")->load();
    const bool enLow  = apvts.getRawParameterValue ("enableLow")->load() > 0.5f;
    const bool enMid  = apvts.getRawParameterValue ("enableMid")->load() > 0.5f;
    const bool enHigh = apvts.getRawParameterValue ("enableHigh")->load() > 0.5f;

    const float tLow  = apvts.getRawParameterValue ("threshLow")->load();
    const float tMid  = apvts.getRawParameterValue ("threshMid")->load();
    const float tHigh = apvts.getRawParameterValue ("threshHigh")->load();
    const int pLow  = juce::jlimit (1, 12, (int) apvts.getRawParameterValue ("padLow")->load());
    const int pMid  = juce::jlimit (1, 12, (int) apvts.getRawParameterValue ("padMid")->load());
    const int pHigh = juce::jlimit (1, 12, (int) apvts.getRawParameterValue ("padHigh")->load());
    const int minGap = (int) (apvts.getRawParameterValue ("cooldownMs")->load() * 0.00045f * sr);

    const float* ch0 = buffer.getReadPointer (0);
    const float* ch1 = numChannels > 1 ? buffer.getReadPointer (1) : ch0;

    float blockPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float mono = 0.5f * (ch0[i] + ch1[i]);
        const float absMono = std::abs (mono);
        if (absMono > blockPeak) blockPeak = absMono;

        lowLpState  += (mono - lowLpState)  * lowCoeff;
        highLpState += (mono - highLpState) * highCoeff;

        const float bandLow  = lowLpState;
        const float bandHigh = mono - highLpState;
        const float bandMid  = highLpState - lowLpState;

        if (enLow && detLow.step (std::abs (bandLow), tLow, sens, fastA, fastR, slowA, slowR, minGap))
            padHitCounters[pLow - 1].fetch_add (1, std::memory_order_relaxed);
        if (enMid && detMid.step (std::abs (bandMid), tMid, sens, fastA, fastR, slowA, slowR, minGap))
            padHitCounters[pMid - 1].fetch_add (1, std::memory_order_relaxed);
        if (enHigh && detHigh.step (std::abs (bandHigh), tHigh, sens, fastA, fastR, slowA, slowR, minGap))
            padHitCounters[pHigh - 1].fetch_add (1, std::memory_order_relaxed);
    }

    masterPeak.store (blockPeak, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* SP404DanceAudioProcessor::createEditor()
{
    return new SP404DanceAudioProcessorEditor (*this);
}

void SP404DanceAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SP404DanceAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SP404DanceAudioProcessor();
}
