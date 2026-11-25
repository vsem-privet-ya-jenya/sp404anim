#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr int SP_W = 493;
    constexpr int SP_H = 773;
    constexpr int COL_XS[4] = { 66, 144, 221, 297 };
    constexpr int ROW_YS[3] = { 527, 596, 665 };
    constexpr int PAD_W = 56;
    constexpr int PAD_H = 48;

    constexpr float WINDUP_TIP_X = 0.815f;
    constexpr float WINDUP_TIP_Y = 0.042f;
    constexpr float STRIKE_TIP_X = 0.678f;
    constexpr float STRIKE_TIP_Y = 0.018f;

    juce::Rectangle<float> getPadRect (int padId)
    {
        const int idx = juce::jlimit (0, 11, padId - 1);
        return { (float) COL_XS[idx % 4], (float) ROW_YS[idx / 4], (float) PAD_W, (float) PAD_H };
    }

    bool isRightPad (int padId) { return ((padId - 1) % 4) >= 2; }

    const juce::Rectangle<float> DISPLAY_RECT (194.0f, 247.0f, 112.0f, 52.0f);
}

SP404DanceAudioProcessorEditor::SP404DanceAudioProcessorEditor (SP404DanceAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    bgImage = juce::ImageCache::getFromMemory (BinaryData::sp404_jpg, BinaryData::sp404_jpgSize);

    auto makeScaledHand = [] (const void* data, int size)
    {
        auto src = juce::ImageCache::getFromMemory (data, size);
        juce::Image dst (juce::Image::ARGB, 360, 360, true);
        juce::Graphics g (dst);
        g.drawImage (src, 0, 0, 360, 360, 0, 0, src.getWidth(), src.getHeight());
        return dst;
    };

    handWindupImg = makeScaledHand (BinaryData::hand_left_1_png, BinaryData::hand_left_1_pngSize);
    handStrikeImg = makeScaledHand (BinaryData::hand_left_2_png, BinaryData::hand_left_2_pngSize);

    for (size_t i = 0; i < 12; ++i)
        lastSeenCounters[i] = processor.padHitCounters[i].load (std::memory_order_relaxed);

    // Build hidden settings overlay (only visible when user clicks the LED screen)
    addChildComponent (settingsOverlay);
    settingsOverlay.addAndMakeVisible (closeBtn);
    closeBtn.onClick = [this] { settingsVisible = false; settingsOverlay.setVisible (false); repaint(); };

    struct ParamSpec { const char* id; const char* name; };
    const ParamSpec specs[14] = {
        { "sensitivity", "Sensitivity" },
        { "threshLow",   "Low Thresh" },
        { "threshMid",   "Mid Thresh" },
        { "threshHigh",  "High Thresh" },
        { "padLow",      "Low Pad (1-12)" },
        { "padMid",      "Mid Pad (1-12)" },
        { "padHigh",     "High Pad (1-12)" },
        { "cooldownMs",  "Cooldown (ms)" },
        { "pixelScale",  "Pixel Scale" },
        { "shakalGrit",  "Shakal Grit" },
        { "attackMs",    "Attack (ms)" },
        { "releaseMs",   "Release (ms)" },
        { "fpsLock",     "Choppy FPS" },
        { "handScale",   "Hand Scale" }
    };

    for (size_t i = 0; i < 14; ++i)
    {
        auto& item = sliders[i];
        item.label.setText (specs[i].name, juce::dontSendNotification);
        item.label.setFont (juce::FontOptions (12.0f));
        item.label.setColour (juce::Label::textColourId, juce::Colour (0xffcfd6e0));
        settingsOverlay.addAndMakeVisible (item.label);

        item.slider.setSliderStyle (juce::Slider::LinearHorizontal);
        item.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        settingsOverlay.addAndMakeVisible (item.slider);
        item.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, specs[i].id, item.slider);
    }

    settingsOverlay.addAndMakeVisible (mirrorToggle);
    settingsOverlay.addAndMakeVisible (interceptToggle);
    mirrorAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "mirrorRight", mirrorToggle);
    interceptAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "busyIntercept", interceptToggle);

    setSize (440, 690);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) SP_W / (double) SP_H);
    startTimerHz (60);
}

void SP404DanceAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (14);
    settingsOverlay.setBounds (area);

    auto inner = settingsOverlay.getLocalBounds().reduced (14);
    auto top = inner.removeFromTop (28);
    closeBtn.setBounds (top.removeFromRight (70));
    inner.removeFromTop (8);

    for (auto& item : sliders)
    {
        auto row = inner.removeFromTop (32);
        item.label.setBounds (row.removeFromLeft (115));
        item.slider.setBounds (row);
    }

    inner.removeFromTop (6);
    mirrorToggle.setBounds (inner.removeFromTop (26));
    interceptToggle.setBounds (inner.removeFromTop (26));
}

void SP404DanceAudioProcessorEditor::dispatchPadHit (int padId, double nowMs, bool ignoreCooldown)
{
    const bool right = isRightPad (padId);
    auto& hand = right ? rightHand : leftHand;

    const float cooldownMs = processor.apvts.getRawParameterValue ("cooldownMs")->load();
    const bool intercept   = processor.apvts.getRawParameterValue ("busyIntercept")->load() > 0.5f;

    if (! ignoreCooldown && (nowMs - hand.lastTriggerMs) < cooldownMs)
        return;

    if (hand.phase != HandPhase::Idle)
    {
        if (! intercept || hand.phase != HandPhase::Retreat)
            return;
    }

    hand.phase = HandPhase::Approach;
    hand.padId = padId;
    hand.phaseStartMs = nowMs;
    hand.lastTriggerMs = nowMs;
}

void SP404DanceAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (settingsVisible)
        return;

    const float x = (e.position.x / (float) getWidth())  * (float) SP_W;
    const float y = (e.position.y / (float) getHeight()) * (float) SP_H;

    if (DISPLAY_RECT.expanded (4.0f).contains (x, y))
    {
        settingsVisible = true;
        settingsOverlay.setVisible (true);
        repaint();
        return;
    }

    for (int id = 1; id <= 12; ++id)
    {
        if (getPadRect (id).expanded (4.0f).contains (x, y))
        {
            dispatchPadHit (id, juce::Time::getMillisecondCounterHiRes(), true);
            break;
        }
    }
}

void SP404DanceAudioProcessorEditor::timerCallback()
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();

    for (int i = 0; i < 12; ++i)
    {
        const uint32_t cur = processor.padHitCounters[i].load (std::memory_order_relaxed);
        if (cur != lastSeenCounters[i])
        {
            lastSeenCounters[i] = cur;
            dispatchPadHit (i + 1, nowMs, false);
        }
    }

    const float attackMs  = processor.apvts.getRawParameterValue ("attackMs")->load();
    const float releaseMs = processor.apvts.getRawParameterValue ("releaseMs")->load();
    const float holdMs    = juce::jlimit (40.0f, 95.0f, releaseMs * 0.45f);

    for (HandState* h : { &leftHand, &rightHand })
    {
        const double el = nowMs - h->phaseStartMs;
        if (h->phase == HandPhase::Approach && el >= attackMs)
        {
            h->phase = HandPhase::Strike;
            h->phaseStartMs = nowMs;
            padGlow[h->padId - 1] = 1.0f;
            lastPadHit = h->padId;
        }
        else if (h->phase == HandPhase::Strike && el >= holdMs)
        {
            h->phase = HandPhase::Retreat;
            h->phaseStartMs = nowMs;
        }
        else if (h->phase == HandPhase::Retreat && el >= releaseMs)
        {
            h->phase = HandPhase::Idle;
        }
    }

    for (float& g : padGlow)
        if (g > 0.0f) g = juce::jmax (0.0f, g - 0.065f);

    const int fps = (int) processor.apvts.getRawParameterValue ("fpsLock")->load();
    const double interval = 1000.0 / juce::jmax (8, fps);
    if (fps >= 58 || (nowMs - lastQuantizedTick) >= interval)
    {
        lastQuantizedTick = nowMs;
        repaint();
    }
}

void SP404DanceAudioProcessorEditor::drawSevenSegment (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> rect)
{
    g.setColour (juce::Colour (0xff0d0607));
    g.fillRect (rect);

    auto segMaskFor = [] (juce::juce_wchar c) -> int
    {
        switch (c)
        {
            case '0': return 0x3f;
            case '1': return 0x06;
            case '2': return 0x5b;
            case '3': return 0x4f;
            case '4': return 0x66;
            case '5': return 0x6d;
            case '6': return 0x7d;
            case '7': return 0x07;
            case '8': return 0x7f;
            case '9': return 0x6f;
            case 'P': return 0x73;
            default:  return 0;
        }
    };

    auto padded = text.substring (0, 3).paddedLeft (' ', 3);
    const float dw = 20.0f, dh = 38.0f, gap = 8.0f, t = 3.6f;
    const float sx = rect.getX() + (rect.getWidth() - (3.0f * dw + 2.0f * gap)) * 0.5f + 4.0f;
    const float sy = rect.getY() + (rect.getHeight() - dh) * 0.5f;

    for (int idx = 0; idx < 3; ++idx)
    {
        const int mask = segMaskFor (padded[idx]);
        const float ox = sx + idx * (dw + gap);
        const float mid = dh * 0.5f;

        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::shear (-0.14f, 0.0f).translated (ox, sy));

        for (int s = 0; s < 7; ++s)
        {
            const bool lit = (mask & (1 << s)) != 0;
            g.setColour (lit ? juce::Colour (0xffff2a1a) : juce::Colour (0x60481212));
            if (s == 0) g.fillRect (t, 0.0f, dw - 2.0f * t, t);
            if (s == 1) g.fillRect (dw - t, t, t, mid - 1.5f * t);
            if (s == 2) g.fillRect (dw - t, mid + 0.5f * t, t, mid - 1.5f * t);
            if (s == 3) g.fillRect (t, dh - t, dw - 2.0f * t, t);
            if (s == 4) g.fillRect (0.0f, mid + 0.5f * t, t, mid - 1.5f * t);
            if (s == 5) g.fillRect (0.0f, t, t, mid - 1.5f * t);
            if (s == 6) g.fillRect (t, mid - t * 0.5f, dw - 2.0f * t, t);
        }
    }
}

void SP404DanceAudioProcessorEditor::applyShakal (juce::Image& img, float grit, uint32_t seed)
{
    if (grit <= 0.02f) return;
    juce::Image::BitmapData bmp (img, juce::Image::BitmapData::readWrite);
    const int levels = juce::jmax (6, (int) std::round (52.0f - grit * 44.0f));
    const float step = 255.0f / (float) levels;
    const float noiseAmp = grit * 24.0f;

    for (int y = 0; y < img.getHeight(); ++y)
    {
        const float scan = (y % 2 == 0) ? 1.0f : (1.0f - grit * 0.08f);
        for (int x = 0; x < img.getWidth(); ++x)
        {
            seed = seed * 1664525u + 1013904223u;
            const float n = ((float) (seed & 0xff) / 255.0f - 0.5f) * noiseAmp;
            auto px = bmp.getPixelColour (x, y);
            const auto quant = [&] (uint8_t v) -> uint8_t
            {
                const float c = std::round (((float) v + n) * scan / step) * step;
                return (uint8_t) juce::jlimit (0, 255, (int) c);
            };
            bmp.setPixelColour (x, y, juce::Colour::fromRGBA (quant (px.getRed()), quant (px.getGreen()), quant (px.getBlue()), px.getAlpha()));
        }
    }
}

void SP404DanceAudioProcessorEditor::paint (juce::Graphics& g)
{
    const float scale = juce::jmax (1.0f, processor.apvts.getRawParameterValue ("pixelScale")->load());
    const float grit  = processor.apvts.getRawParameterValue ("shakalGrit")->load();
    const float handScale = processor.apvts.getRawParameterValue ("handScale")->load();
    const float attackMs  = processor.apvts.getRawParameterValue ("attackMs")->load();
    const float releaseMs = processor.apvts.getRawParameterValue ("releaseMs")->load();
    const bool mirrorRight = processor.apvts.getRawParameterValue ("mirrorRight")->load() > 0.5f;
    const int fps = (int) processor.apvts.getRawParameterValue ("fpsLock")->load();

    const int offW = juce::jmax (64, (int) std::round ((float) SP_W / scale));
    const int offH = juce::jmax (100, (int) std::round ((float) SP_H / scale));

    if (! offscreenBuffer.isValid() || offscreenBuffer.getWidth() != offW || offscreenBuffer.getHeight() != offH)
        offscreenBuffer = juce::Image (juce::Image::ARGB, offW, offH, true);

    {
        juce::Graphics og (offscreenBuffer);
        og.fillAll (juce::Colours::black);
        og.setImageResamplingQuality (scale <= 1.2f ? juce::Graphics::mediumResamplingQuality : juce::Graphics::lowResamplingQuality);
        og.addTransform (juce::AffineTransform::scale ((float) offW / (float) SP_W, (float) offH / (float) SP_H));

        if (bgImage.isValid())
            og.drawImage (bgImage, 0, 0, SP_W, SP_H, 0, 0, bgImage.getWidth(), bgImage.getHeight());

        juce::String dispText = juce::String (juce::jlimit (1, 999, (int) std::round (processor.hostBpm.load (std::memory_order_relaxed))));
        if (lastPadHit >= 1 && lastPadHit <= 12 && padGlow[lastPadHit - 1] > 0.15f)
            dispText = "P" + juce::String (lastPadHit).paddedLeft ('0', 2);
        drawSevenSegment (og, dispText, DISPLAY_RECT);

        const float peak = processor.masterPeak.load (std::memory_order_relaxed);
        if (peak > 0.28f || leftHand.phase == HandPhase::Strike || rightHand.phase == HandPhase::Strike)
        {
            og.setColour (juce::Colour (0xd0ff3824));
            og.fillEllipse (361.0f, 148.0f, 12.0f, 12.0f);
        }

        for (int id = 1; id <= 12; ++id)
        {
            const float gl = padGlow[id - 1];
            if (gl > 0.02f)
            {
                auto r = getPadRect (id);
                og.setColour (juce::Colour::fromFloatRGBA (1.0f, 0.30f, 0.22f, gl * 0.62f));
                og.fillRect (r.reduced (2.0f));
                og.setColour (juce::Colour::fromFloatRGBA (1.0f, 0.76f, 0.62f, gl * 0.42f));
                og.fillRect (r.reduced (10.0f, 8.0f));
            }
        }

        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const float drawSz = 248.0f * handScale;

        for (HandState* h : { &leftHand, &rightHand })
        {
            if (h->phase == HandPhase::Idle) continue;
            auto pad = getPadRect (h->padId);
            const double el = nowMs - h->phaseStartMs;

            const float restX = h->isRight ? (pad.getCentreX() + 48.0f) : (pad.getCentreX() - 48.0f);
            const float restY = (float) SP_H + 165.0f;
            const float hoverY = pad.getCentreY() + 34.0f;

            float tx = pad.getCentreX(), ty = pad.getCentreY();
            const juce::Image* spr = &handWindupImg;
            float tipX = WINDUP_TIP_X, tipY = WINDUP_TIP_Y;

            if (h->phase == HandPhase::Approach)
            {
                float t = juce::jlimit (0.0f, 1.0f, (float) el / juce::jmax (12.0f, attackMs));
                if (fps <= 24) t = std::round (t * 4.0f) / 4.0f;
                const float e = 1.0f - std::pow (1.0f - t, 3.0f);
                tx = restX + (pad.getCentreX() - restX) * e;
                ty = restY + (hoverY - restY) * e;
            }
            else if (h->phase == HandPhase::Strike)
            {
                spr = &handStrikeImg;
                tipX = STRIKE_TIP_X;
                tipY = STRIKE_TIP_Y;
            }
            else if (h->phase == HandPhase::Retreat)
            {
                float t = juce::jlimit (0.0f, 1.0f, (float) el / juce::jmax (20.0f, releaseMs));
                if (fps <= 24) t = std::round (t * 5.0f) / 5.0f;
                const float e = t * t * t;
                tx = pad.getCentreX() + (restX - pad.getCentreX()) * e;
                ty = hoverY + (restY - hoverY) * e;
            }

            if (spr->isValid())
            {
                juce::AffineTransform xform = juce::AffineTransform::scale (drawSz / (float) spr->getWidth(), drawSz / (float) spr->getHeight())
                    .translated (-tipX * drawSz, -tipY * drawSz);
                if (h->isRight && mirrorRight)
                    xform = xform.scaled (-1.0f, 1.0f);
                xform = xform.translated (tx, ty);
                og.drawImageTransformed (*spr, xform);
            }
        }
    }

    applyShakal (offscreenBuffer, grit, (uint32_t) (lastQuantizedTick * 0.1));

    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (offscreenBuffer, getLocalBounds().toFloat());

    if (settingsVisible)
    {
        g.setColour (juce::Colour (0xee16191d));
        g.fillRoundedRectangle (settingsOverlay.getBounds().toFloat(), 6.0f);
        g.setColour (juce::Colour (0xff2a2f37));
        g.drawRoundedRectangle (settingsOverlay.getBounds().toFloat(), 6.0f, 1.0f);
    }
}
