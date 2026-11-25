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

    enum class HandPhase { Idle, Approach, Strike, Retreat };
    struct HandState
    {
        bool isRight = false;
        HandPhase phase = HandPhase::Idle;
        int padId = 1;
        double phaseStartMs = 0.0;
        double lastTriggerMs = -1000.0;
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
    std::array<LabeledSlider, 14> sliders;
    juce::ToggleButton mirrorToggle { "Mirror Right Hand" };
    juce::ToggleButton interceptToggle { "Intercept on Decay" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mirrorAttach, interceptAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SP404DanceAudioProcessorEditor)
};
