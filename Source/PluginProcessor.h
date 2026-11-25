#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

class SP404DanceAudioProcessor : public juce::AudioProcessor
{
public:
    SP404DanceAudioProcessor();
    ~SP404DanceAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<uint32_t>, 12> padHitCounters {};
    std::atomic<float> masterPeak { 0.0f };
    std::atomic<double> hostBpm { 140.0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct BandDetector
    {
        float fastEnv = 0.0f;
        float slowEnv = 0.0f;
        bool armed = true;
        int cooldownSamples = 0;

        bool step (float sampleAbs, float threshold, float sensitivity, float fastA, float fastR, float slowA, float slowR, int minGapSamples);
    };

    double currentSampleRate = 44100.0;
    float lowLpState = 0.0f;
    float highLpState = 0.0f;
    BandDetector detLow, detMid, detHigh;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SP404DanceAudioProcessor)
};
