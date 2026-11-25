#pragma once
#include "PluginProcessor.h"
#include <array>

class SP404DanceAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit SP404DanceAudioProcessorEditor (SP404DanceAudioProcessor&);
    ~SP404DanceAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void dispatchPadHit (int padId, double nowMs, bool ignoreCooldown);
    void drawSevenSegment (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> rect);
    void applyShakal (juce::Image& img, float grit, uint32_t seed);

    SP404DanceAudioProcessor& processor;

    juce::Image bgImage, handWindupImg, handStrikeImg, offscreenBuffer;

    enum class HandPhase { Idle, Approach, Strike, Hover, Retreat };
    struct HandState
    {
        bool isRight = false;
        HandPhase phase = HandPhase::Idle;
        int padId = 1;
        double phaseStartMs = 0.0;
        double lastTriggerMs = -1000.0;
        float startX = 0.0f;
        float startY = 0.0f;
        float curX = 0.0f;
        float curY = 0.0f;
    };

    HandState leftHand { false }, rightHand { true };
    std::array<float, 12> padGlow {};
    std::array<uint32_t, 12> lastSeenCounters {};
    int lastPadHit = 0;
    double lastQuantizedTick = 0.0;

    // Hidden settings overlay (toggled ONLY by clicking the 7-seg LED screen)
    bool settingsVisible = false;
    juce::Component settingsOverlay;
    juce::TextButton closeBtn { "Close" };

    struct LabeledSlider
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    std::array<LabeledSlider, 15> sliders;

    juce::ToggleButton lowBandToggle  { "Low (Kick)" };
    juce::ToggleButton midBandToggle  { "Mid (Snare)" };
    juce::ToggleButton highBandToggle { "High (Hat)" };
    juce::ToggleButton mirrorToggle   { "Mirror Right Hand" };
    juce::ToggleButton interceptToggle{ "Intercept Mid-Hit" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        lowBandAttach, midBandAttach, highBandAttach, mirrorAttach, interceptAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SP404DanceAudioProcessorEditor)
};
