#include "PluginEditor.h"

using namespace juce;

//==============================================================================
//  Small helpers
//==============================================================================
namespace
{
String noteLabel (double hz)
{
    if (hz <= 0.0) return "--";
    const int mi = (int) std::lround (pf::freqToMidi (hz));
    return String (pf::noteName (mi)) + String (pf::midiOctave (mi));
}

String percent (float v) { return String (roundToInt (v * 100.0f)) + "%"; }

float wrapDeg (float d)
{
    while (d >  180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

String degText (float d)
{
    return String (d >= 0.0f ? "+" : "") + String (roundToInt (d)) + String::fromUTF8 ("\xC2\xB0");
}

String signedMs (double ms)
{
    return String (ms >= 0.0 ? "+" : "-") + String (std::abs (ms), 2) + " ms";
}

const char* const kZoomLabels[4] = { "12 ms", "25 ms", "50 ms", "100 ms" };
const double kNudgeDeltas[6] = { -1.0, -0.1, -0.01, 0.01, 0.1, 1.0 };
const char* const kNudgeLabels[6] = { "-1.0", "-0.1", "-0.01", "+0.01", "+0.1", "+1.0" };
}

//==============================================================================
//  Look & feel: chunky, sticker-like, friendly
//==============================================================================
SunLookAndFeel::SunLookAndFeel()
{
    setColour (ComboBox::backgroundColourId,   pfui::ink());
    setColour (ComboBox::outlineColourId,      pfui::sun().withAlpha (0.8f));
    setColour (ComboBox::textColourId,         pfui::cream());
    setColour (ComboBox::arrowColourId,        pfui::sun());
    setColour (PopupMenu::backgroundColourId,  pfui::ink2());
    setColour (PopupMenu::textColourId,        pfui::cream());
    setColour (PopupMenu::highlightedBackgroundColourId, pfui::sun());
    setColour (PopupMenu::highlightedTextColourId,       pfui::ink());
    setColour (Slider::textBoxTextColourId,    pfui::cream());
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
}

void SunLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                                       float pos, float startAngle, float endAngle, Slider& s)
{
    auto b = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (8.0f);
    const float radius = jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const auto accent = s.findColour (Slider::rotarySliderFillColourId);

    Path track;
    track.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (pfui::cream().withAlpha (0.14f));
    g.strokePath (track, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));

    Path val;
    val.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, angle, true);
    g.setColour (accent);
    g.strokePath (val, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));

    const float bodyR = radius * 0.68f;
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - bodyR, c.y - bodyR + 3.0f, bodyR * 2.0f, bodyR * 2.0f);
    g.setGradientFill (ColourGradient (pfui::cream(), c.x, c.y - bodyR, pfui::cream().darker (0.35f), c.x, c.y + bodyR, false));
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (pfui::ink());
    g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 3.0f);

    const auto p1 = c.getPointOnCircumference (bodyR * 0.30f, angle);
    const auto p2 = c.getPointOnCircumference (bodyR * 0.86f, angle);
    g.setColour (pfui::ink());
    g.drawLine (p1.x, p1.y, p2.x, p2.y, 4.0f);
    g.setColour (accent);
    g.fillEllipse (p2.x - 3.5f, p2.y - 3.5f, 7.0f, 7.0f);
}

void SunLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float, float, Slider::SliderStyle, Slider&)
{
    const Rectangle<float> r ((float) x, (float) y, (float) width, (float) height);
    const float cy = r.getCentreY();
    const float trackH = 12.0f;
    const Rectangle<float> track (r.getX() + 8.0f, cy - trackH * 0.5f, r.getWidth() - 16.0f, trackH);

    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (track.translated (0.0f, 3.0f), trackH * 0.5f);
    g.setColour (pfui::ink());
    g.fillRoundedRectangle (track, trackH * 0.5f);

    const float midX = track.getCentreX();
    const float x0 = jmin (midX, sliderPos), x1 = jmax (midX, sliderPos);
    g.setColour (pfui::sun());
    g.fillRoundedRectangle (Rectangle<float> (x0, track.getY() + 2.0f, jmax (1.0f, x1 - x0), trackH - 4.0f), 4.0f);

    g.setColour (pfui::cream().withAlpha (0.35f));
    g.drawRoundedRectangle (track, trackH * 0.5f, 1.5f);
    g.setColour (pfui::cream().withAlpha (0.8f));                       // "no shift" notch
    g.drawLine (midX, cy - 11.0f, midX, cy + 11.0f, 2.0f);

    const float tr = 13.0f;
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillEllipse (sliderPos - tr, cy - tr + 3.0f, tr * 2.0f, tr * 2.0f);
    g.setColour (pfui::sun());
    g.fillEllipse (sliderPos - tr, cy - tr, tr * 2.0f, tr * 2.0f);
    g.setColour (pfui::ink());
    g.drawEllipse (sliderPos - tr, cy - tr, tr * 2.0f, tr * 2.0f, 3.0f);
    g.drawLine (sliderPos - 4.0f, cy - 4.0f, sliderPos - 4.0f, cy + 4.0f, 2.0f);
    g.drawLine (sliderPos + 4.0f, cy - 4.0f, sliderPos + 4.0f, cy + 4.0f, 2.0f);
}

void SunLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    const auto accent = b.findColour (ToggleButton::tickColourId);
    const float cr = jmin (r.getHeight() * 0.5f, 16.0f);
    const auto face = r.translated (0.0f, down ? 2.0f : 0.0f).withTrimmedBottom (3.0f);

    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f).withTrimmedBottom (3.0f), cr);
    g.setColour (on ? accent.brighter (highlighted ? 0.12f : 0.0f) : pfui::ink().brighter (highlighted ? 0.12f : 0.0f));
    g.fillRoundedRectangle (face, cr);
    g.setColour (on ? pfui::ink() : accent.withAlpha (0.9f));
    g.drawRoundedRectangle (face, cr, 2.5f);

    g.setFont (FontOptions (jmax (10.0f, face.getHeight() * 0.40f), Font::bold));
    g.setColour (on ? pfui::ink() : accent);
    g.drawText (b.getButtonText(), face.toNearestInt(), Justification::centred, false);
}

void SunLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    const float cr = jmin (r.getHeight() * 0.5f, 16.0f);
    const auto face = r.translated (0.0f, down ? 2.0f : 0.0f).withTrimmedBottom (3.0f);

    const auto base   = b.findColour (TextButton::buttonColourId);
    const auto onCol  = b.findColour (TextButton::buttonOnColourId);

    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f).withTrimmedBottom (3.0f), cr);
    g.setColour ((on ? onCol : base).brighter (highlighted ? 0.12f : 0.0f));
    g.fillRoundedRectangle (face, cr);
    g.setColour (pfui::ink());
    g.drawRoundedRectangle (face, cr, 2.5f);
}

void SunLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (2.0f).translated (0.0f, down ? 2.0f : 0.0f).withTrimmedBottom (3.0f);
    g.setFont (FontOptions (jmax (10.0f, r.getHeight() * 0.40f), Font::bold));
    g.setColour (b.findColour (b.getToggleState() ? TextButton::textColourOnId : TextButton::textColourOffId));
    g.drawText (b.getButtonText(), r.toNearestInt(), Justification::centred, false);
}

//==============================================================================
//  Help overlay
//==============================================================================
void HelpOverlay::paint (Graphics& g)
{
    const float s = (float) getWidth() / 1040.0f;
    g.fillAll (pfui::ink().withAlpha (0.88f));

    const auto card = Rectangle<float> (700.0f * s, 470.0f * s).withCentre (getLocalBounds().toFloat().getCentre());
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (card.translated (0.0f, 8.0f * s), 22.0f * s);
    g.setColour (pfui::ink2());
    g.fillRoundedRectangle (card, 22.0f * s);
    g.setColour (pfui::sun());
    g.drawRoundedRectangle (card.reduced (1.5f), 22.0f * s, 3.5f);

    auto line = [&] (const String& str, float y, float size, Colour c, bool bold)
    {
        g.setFont (FontOptions (size * s, bold ? Font::bold : Font::plain));
        g.setColour (c);
        g.drawText (str, Rectangle<float> (card.getX() + 34.0f * s, card.getY() + y * s, card.getWidth() - 68.0f * s, (size + 8.0f) * s),
                    Justification::centredLeft, false);
    };
    auto step = [&] (int n, const String& str, float y)
    {
        const Rectangle<float> dot (card.getX() + 34.0f * s, card.getY() + y * s, 30.0f * s, 30.0f * s);
        g.setColour (pfui::sun());
        g.fillEllipse (dot);
        g.setFont (FontOptions (17.0f * s, Font::bold));
        g.setColour (pfui::ink());
        g.drawText (String (n), dot, Justification::centred, false);
        g.setFont (FontOptions (15.0f * s, Font::plain));
        g.setColour (pfui::cream());
        g.drawFittedText (str, Rectangle<float> (card.getX() + 76.0f * s, card.getY() + (y - 6.0f) * s, card.getWidth() - 112.0f * s, 44.0f * s).toNearestInt(),
                          Justification::centredLeft, 2);
    };

    line ("CONNECT YOUR KICK", 22.0f, 28.0f, pfui::sun(), true);
    line ("PhaseForge needs to hear your kick so it can line the bass up with it.", 60.0f, 14.0f, pfui::cream().withAlpha (0.8f), false);

    step (1, "Put PhaseForge on your BASS track (an audio track).", 100.0f);
    step (2, "Look at PhaseForge's device strip in Ableton (not this window). Switch its SIDECHAIN on. You need Live 10.1 or newer.", 146.0f);
    step (3, "In the \"Audio From\" menu, pick your KICK track.", 202.0f);
    step (4, "Press play. The badge at the top turns green: KICK CONNECTED.", 248.0f);

    line ("THEN MAKE THEM MATCH", 304.0f, 20.0f, pfui::sun(), true);
    line ("Press MAKE IT MATCH, or drag the yellow wave until it lines up with the pink one.", 336.0f, 14.0f, pfui::cream(), false);
    line ("Pink = kick.   Yellow = your bass after PhaseForge.   Mint = adding up.   Red = fighting.", 362.0f, 14.0f, pfui::cream(), false);
    line ("Tiny moves: use the +/- 0.01 buttons, scroll the mouse wheel, or hold SHIFT while dragging.", 388.0f, 14.0f, pfui::cream(), false);
    line ("click anywhere to close", 424.0f, 12.0f, pfui::pink(), true);
}

//==============================================================================
//  Editor
//==============================================================================
void PhaseForgeAudioProcessorEditor::styleButton (TextButton& b, Colour base, Colour onColour, Colour textOff, Colour textOn)
{
    b.setColour (TextButton::buttonColourId, base);
    b.setColour (TextButton::buttonOnColourId, onColour);
    b.setColour (TextButton::textColourOffId, textOff);
    b.setColour (TextButton::textColourOnId, textOn);
}

void PhaseForgeAudioProcessorEditor::setupKnob (Slider& s, Colour accent, double defaultValue)
{
    s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (Slider::TextBoxBelow, false, 84, 16);
    s.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    s.setColour (Slider::rotarySliderFillColourId, accent);
    s.setDoubleClickReturnValue (true, defaultValue);
    addChildComponent (s);                       // only shown when PRO TOOLS is open
}

PhaseForgeAudioProcessorEditor::PhaseForgeAudioProcessorEditor (PhaseForgeAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    // ---- the big shift slider -----------------------------------------------------------------
    shiftK.setSliderStyle (Slider::LinearHorizontal);
    shiftK.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    shiftK.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (shiftK);

    // ---- pro knobs ----------------------------------------------------------------------------
    setupKnob (rotStagesK, pfui::sky(),    0.0);
    setupKnob (rotFreqK,   pfui::sky(),    60.0);
    setupKnob (rotQK,      pfui::sky(),    0.7);
    setupKnob (xoverK,     pfui::orange(), 120.0);
    setupKnob (speedK,     pfui::mint(),   0.4);
    setupKnob (mixK,       pfui::pink(),   1.0);
    setupKnob (outK,       pfui::pink(),   0.0);
    proKnobs = { { { &rotStagesK, "PHASE TWIST" }, { &rotFreqK, "TWIST PITCH" }, { &rotQK, "TWIST SHAPE" },
                   { &xoverK, "SPLIT AT" }, { &speedK, "REACTION" }, { &mixK, "MIX" }, { &outK, "VOLUME" } } };

    // ---- toggles --------------------------------------------------------------------------------
    flipB.setColour (ToggleButton::tickColourId, pfui::pink());
    autoB.setColour (ToggleButton::tickColourId, pfui::mint());
    holdB.setColour (ToggleButton::tickColourId, pfui::sky());
    followB.setColour (ToggleButton::tickColourId, pfui::sky());
    addAndMakeVisible (flipB);
    addAndMakeVisible (autoB);
    addAndMakeVisible (holdB);
    addChildComponent (followB);

    // ---- buttons ---------------------------------------------------------------------------------
    styleButton (matchB, pfui::sun(), pfui::sun(), pfui::ink(), pfui::ink());
    matchB.onClick = [this] { proc.startLearn(); };
    addAndMakeVisible (matchB);

    styleButton (resetB, pfui::ink2().brighter (0.15f), pfui::ink2(), pfui::cream(), pfui::cream());
    resetB.onClick = [this] { resetAll(); };
    addAndMakeVisible (resetB);

    styleButton (helpB, pfui::pink(), pfui::pink(), pfui::ink(), pfui::ink());
    helpB.onClick = [this] { help.setVisible (true); help.toFront (false); };
    addAndMakeVisible (helpB);

    styleButton (proB, pfui::ink2().brighter (0.15f), pfui::orange(), pfui::cream(), pfui::ink());
    proB.setClickingTogglesState (true);
    proB.onClick = [this] { setProOpen (proB.getToggleState()); };
    addAndMakeVisible (proB);

    styleButton (resetKeyB, pfui::ink2().brighter (0.15f), pfui::ink2(), pfui::cream(), pfui::cream());
    resetKeyB.onClick = [this] { proc.resetKey(); };
    addAndMakeVisible (resetKeyB);

    for (size_t i = 0; i < nudgeB.size(); ++i)
    {
        auto& b = nudgeB[i];
        b.setButtonText (kNudgeLabels[i]);
        styleButton (b, i < 3 ? pfui::pink().withAlpha (0.85f) : pfui::sky().withAlpha (0.85f),
                     pfui::cream(), pfui::ink(), pfui::ink());
        const double d = kNudgeDeltas[i];
        b.onClick = [this, d] { nudgeShift (d); };
        addAndMakeVisible (b);
    }

    for (size_t i = 0; i < zoomB.size(); ++i)
    {
        auto& b = zoomB[i];
        b.setButtonText (kZoomLabels[i]);
        styleButton (b, pfui::ink(), pfui::sun(), pfui::cream(), pfui::ink());
        b.setClickingTogglesState (true);
        b.setRadioGroupId (2);
        const int idx = (int) i;
        b.onClick = [this, idx] { zoomIdx = idx; };
        addAndMakeVisible (b);
    }
    zoomB[(size_t) zoomIdx].setToggleState (true, dontSendNotification);

    styleButton (tabWave, pfui::ink(), pfui::sun(), pfui::cream(), pfui::ink());
    styleButton (tabFreq, pfui::ink(), pfui::sun(), pfui::cream(), pfui::ink());
    for (auto* t : { &tabWave, &tabFreq })
    {
        t->setClickingTogglesState (true);
        t->setRadioGroupId (1);
        addAndMakeVisible (*t);
    }
    tabWave.setToggleState (true, dontSendNotification);
    tabWave.onClick = [this] { tab = 0; resized(); };
    tabFreq.onClick = [this] { tab = 1; resized(); };

    // ---- combos -----------------------------------------------------------------------------------
    bandBox.addItemList ({ "Whole bass", "Only below split" }, 1);
    keyModeBox.addItemList ({ "Auto", "Manual" }, 1);
    keyRootBox.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    keyScaleBox.addItemList ({ "Minor", "Major" }, 1);
    for (auto* c : { &keyModeBox, &keyRootBox, &keyScaleBox, &presetBox })
        addAndMakeVisible (*c);
    addChildComponent (bandBox);

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
    presetBox.setTextWhenNothingSelected ("PRESETS");
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };

    // ---- parameter attachments ---------------------------------------------------------------------
    auto& v = proc.apvts;
    shiftAtt     = std::make_unique<SliderAtt> (v, "shift", shiftK);
    rotStagesAtt = std::make_unique<SliderAtt> (v, "rotStages", rotStagesK);
    rotFreqAtt   = std::make_unique<SliderAtt> (v, "rotFreq", rotFreqK);
    rotQAtt      = std::make_unique<SliderAtt> (v, "rotQ", rotQK);
    xoverAtt     = std::make_unique<SliderAtt> (v, "xover", xoverK);
    speedAtt     = std::make_unique<SliderAtt> (v, "speed", speedK);
    mixAtt       = std::make_unique<SliderAtt> (v, "mix", mixK);
    outAtt       = std::make_unique<SliderAtt> (v, "out", outK);
    flipAtt      = std::make_unique<ButtonAtt> (v, "polarity", flipB);
    autoAtt      = std::make_unique<ButtonAtt> (v, "auto", autoB);
    followAtt    = std::make_unique<ButtonAtt> (v, "follow", followB);
    bandAtt      = std::make_unique<ComboAtt>  (v, "band", bandBox);
    keyModeAtt   = std::make_unique<ComboAtt>  (v, "keyMode", keyModeBox);
    keyRootAtt   = std::make_unique<ComboAtt>  (v, "keyRoot", keyRootBox);
    keyScaleAtt  = std::make_unique<ComboAtt>  (v, "keyScale", keyScaleBox);

    // ---- confetti ----------------------------------------------------------------------------------
    for (auto& pt : particles)
    {
        pt.x = rng.nextFloat();  pt.y = rng.nextFloat();
        pt.vx = (rng.nextFloat() - 0.5f) * 0.0006f;
        pt.vy = -0.0003f - rng.nextFloat() * 0.0010f;
        pt.size = 2.0f + rng.nextFloat() * 4.0f;
        pt.phase = rng.nextFloat() * 6.28f;
    }

    help.onClose = [this] { help.setVisible (false); };
    addChildComponent (help);

    setResizable (true, true);
    setResizeLimits (780, 525, 1560, 1050);
    getConstrainer()->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);
    setSize ((int) kDesignW, (int) kDesignH);

    startTimerHz (30);
}

PhaseForgeAudioProcessorEditor::~PhaseForgeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void PhaseForgeAudioProcessorEditor::setProOpen (bool open)
{
    proOpen = open;
    for (auto& k : proKnobs) k.s->setVisible (open);
    bandBox.setVisible (open);
    followB.setVisible (open);
    resized();
    repaint();
}

double PhaseForgeAudioProcessorEditor::currentShiftParam() const
{
    return (double) proc.apvts.getRawParameterValue ("shift")->load();
}

void PhaseForgeAudioProcessorEditor::setShiftMs (double ms)
{
    if (auto* p = proc.apvts.getParameter ("shift"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) jlimit (-10.0, 10.0, ms)));
}

void PhaseForgeAudioProcessorEditor::nudgeShift (double deltaMs)
{
    if (auto* p = proc.apvts.getParameter ("shift"))
    {
        p->beginChangeGesture();
        setShiftMs (currentShiftParam() + deltaMs);
        p->endChangeGesture();
    }
}

void PhaseForgeAudioProcessorEditor::resetAll()
{
    auto set = [this] (const char* id, float plain)
    {
        if (auto* p = proc.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
            p->endChangeGesture();
        }
    };
    set ("shift", 0.0f);
    set ("polarity", 0.0f);
    set ("auto", 0.0f);
    set ("rotStages", 0.0f);
}

//==============================================================================
void PhaseForgeAudioProcessorEditor::resized()
{
    scale = (float) getWidth() / kDesignW;
    auto S = [this] (float v) { return v * scale; };
    auto box = [&] (float x, float y, float w, float h) { return Rectangle<float> (S (x), S (y), S (w), S (h)).toNearestInt(); };

    const float top = 76.0f, proH = 124.0f;
    const float bottom = proOpen ? (kDesignH - 16.0f - proH - 8.0f) : (kDesignH - 16.0f);

    headerR = { S (0), S (0), S (kDesignW), S (64) };
    heroR   = { S (16), S (top), S (680), S (bottom - top) };
    faceR   = { S (712), S (top), S (312), S (250) };
    keyR    = { S (712), S (top + 258.0f), S (312), S (bottom - (top + 258.0f)) };
    proR    = { S (16), S (kDesignH - 16.0f - proH), S (1008), S (proH) };

    help.setBounds (getLocalBounds());

    // header
    presetBox.setBounds (box (330, 17, 200, 30));
    helpB.setBounds (box (846, 15, 74, 34));
    proB.setBounds (box (926, 15, 98, 34));

    // hero: tabs + zoom
    const float hx = 16.0f, hy = top, hw = 680.0f, hh = bottom - top;
    tabWave.setBounds (box (hx + 12, hy + 10, 104, 30));
    tabFreq.setBounds (box (hx + 122, hy + 10, 140, 30));
    for (size_t i = 0; i < zoomB.size(); ++i)
    {
        zoomB[i].setBounds (box (hx + hw - 12.0f - 220.0f + (float) i * 56.0f, hy + 12, 52, 26));
        zoomB[i].setVisible (tab == 0);
    }

    waveR = { S (hx + 14), S (hy + 70), S (hw - 28), S (hh - 70.0f - 136.0f) };

    // hero: controls
    const float x0 = hx + 14.0f;
    shiftK.setBounds (box (x0, hy + hh - 128, hw - 28, 26));
    const float r1 = hy + hh - 96.0f;
    for (size_t i = 0; i < 3; ++i) nudgeB[i].setBounds (box (x0 + (float) i * 55.0f, r1, 50, 38));
    for (size_t i = 3; i < 6; ++i) nudgeB[i].setBounds (box (x0 + 362.0f + (float) (i - 3) * 55.0f, r1, 50, 38));
    flipB.setBounds (box (x0 + 530, r1, 60, 38));
    resetB.setBounds (box (x0 + 596, r1, 52, 38));

    const float r2 = hy + hh - 52.0f;
    matchB.setBounds (box (x0, r2, 260, 42));
    autoB.setBounds (box (x0 + 266, r2, 170, 42));
    holdB.setBounds (box (x0 + 442, r2, 150, 42));

    // key panel controls
    const float kx = 712.0f + 14.0f, kyb = top + 258.0f + (bottom - (top + 258.0f)) - 44.0f;
    keyModeBox.setBounds (box (kx, kyb, 72, 28));
    keyRootBox.setBounds (box (kx + 78, kyb, 50, 28));
    keyScaleBox.setBounds (box (kx + 134, kyb, 66, 28));
    resetKeyB.setBounds (box (kx + 206, kyb, 56, 28));

    // pro panel controls
    const float py = kDesignH - 16.0f - proH;
    bandBox.setBounds (box (32, py + 40, 150, 28));
    followB.setBounds (box (32, py + 76, 150, 34));
    const float kw = (1008.0f - 190.0f - 12.0f) / 7.0f;
    for (size_t i = 0; i < proKnobs.size(); ++i)
        proKnobs[i].s->setBounds (box (16.0f + 190.0f + kw * (float) i, py + 26, kw, proH - 30.0f));
}

//==============================================================================
//  Mouse: drag the waves, wheel for tiny steps
//==============================================================================
void PhaseForgeAudioProcessorEditor::mouseMove (const MouseEvent& e)
{
    mousePos = e.position;
    setMouseCursor (tab == 0 && waveR.contains (e.position) ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
}

void PhaseForgeAudioProcessorEditor::mouseExit (const MouseEvent&)
{
    mousePos = { -1.0f, -1.0f };
}

void PhaseForgeAudioProcessorEditor::mouseDown (const MouseEvent& e)
{
    if (tab == 0 && waveR.contains (e.position))
    {
        dragging = true;
        lastDragX = e.position.x;
        if (auto* p = proc.apvts.getParameter ("shift"))
            p->beginChangeGesture();
    }
}

void PhaseForgeAudioProcessorEditor::mouseDrag (const MouseEvent& e)
{
    if (! dragging) return;

    const float pxPerMs = waveR.getWidth() / kZoomMs[(size_t) zoomIdx];
    const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
    setShiftMs (currentShiftParam() + (double) ((e.position.x - lastDragX) / pxPerMs * fine));
    lastDragX = e.position.x;
}

void PhaseForgeAudioProcessorEditor::mouseUp (const MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    if (auto* p = proc.apvts.getParameter ("shift"))
        p->endChangeGesture();
}

void PhaseForgeAudioProcessorEditor::mouseDoubleClick (const MouseEvent& e)
{
    if (tab == 0 && waveR.contains (e.position))
    {
        if (auto* p = proc.apvts.getParameter ("shift"))
        {
            p->beginChangeGesture();
            setShiftMs (0.0);
            p->endChangeGesture();
        }
    }
}

void PhaseForgeAudioProcessorEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (tab != 0 || ! waveR.contains (e.position) || w.deltaY == 0.0f) return;
    nudgeShift ((w.deltaY > 0.0f ? 1.0 : -1.0) * (e.mods.isShiftDown() ? 0.01 : 0.1));
}

//==============================================================================
//  Timer + view maths
//==============================================================================
void PhaseForgeAudioProcessorEditor::timerCallback()
{
    tAnim += 1.0f / 30.0f;

    kickPulse = jmax (jlimit (0.0f, 1.0f, proc.kickPeak.load()), kickPulse * 0.84f);
    bassGlow  = jmax (jlimit (0.0f, 1.0f, proc.bassPeak.load()), bassGlow * 0.90f);

    if (! holdB.getToggleState())
    {
        haveScope = proc.getScopeSnapshot (sBass.data(), sKick.data(), kScope);
        proc.getSpectrum (specB.data(), specK.data(), specPh.data(), specCo.data());
    }
    if (haveScope)
        updateView();

    // match meter (relative to the best the analyser could find)
    float target = -1.0f;
    if (proc.autoValid.load() && proc.autoScore.load() > 0.05f && proc.scConnected.load())
        target = jlimit (0.0f, 1.0f, proc.corrAfter.load() / proc.autoScore.load());
    if (target < 0.0f)            matchS = -1.0f;
    else if (matchS < 0.0f)       matchS = target;
    else                          matchS += 0.15f * (target - matchS);

    for (auto& pt : particles)
    {
        const float boost = 1.0f + kickPulse * 6.0f;
        pt.x += pt.vx * boost;
        pt.y += pt.vy * boost;
        if (pt.y < -0.03f) { pt.y = 1.03f; pt.x = rng.nextFloat(); }
        if (pt.x < -0.03f) pt.x = 1.03f;
        if (pt.x >  1.03f) pt.x = -0.03f;
    }

    const int ls = proc.learnState.load();
    if (ls != lastLearnState) { lastLearnState = ls; learnStamp = Time::getMillisecondCounter(); }
    String label = "MAKE IT MATCH";
    if (ls == 1)      label = "LISTENING... " + String (roundToInt (proc.learnProgress.load() * 100.0f)) + "%";
    else if (ls == 2 && Time::getMillisecondCounter() - learnStamp < 3000) label = "MATCHED!";
    else if (ls == 3 && Time::getMillisecondCounter() - learnStamp < 3000) label = "NO SIGNAL";
    if (matchB.getButtonText() != label) matchB.setButtonText (label);

    repaint();
}

void PhaseForgeAudioProcessorEditor::updateView()
{
    const double dsr = proc.decimatedRate.load();

    float aK = 0.0f, aB = 0.0f;
    for (int i = 0; i < kScope; ++i)
    {
        aK = jmax (aK, std::abs (sKick[(size_t) i]));
        aB = jmax (aB, std::abs (sBass[(size_t) i]));
    }
    kickHeard = aK > 1e-4f;
    bassHeard = aB > 1e-4f;

    // ---- "bass after PhaseForge", rendered with the exact same signal flow -----------------------
    const auto& vts = proc.apvts;
    pf::PreviewParams pp;
    pp.lowBand   = vts.getRawParameterValue ("band")->load() > 0.5f;
    pp.xoverHz   = (double) proc.effXover.load();
    pp.stages    = (int) std::lround (vts.getRawParameterValue ("rotStages")->load());
    pp.rotFreqHz = (double) proc.effRotFreq.load();
    pp.rotQ      = (double) vts.getRawParameterValue ("rotQ")->load();
    pp.shiftSamples = (double) proc.engineShiftMs.load() * 0.001 * dsr;
    pp.polarity  = proc.appliedPolarity.load() >= 0.0f ? 1.0 : -1.0;
    pp.mix       = (double) vts.getRawParameterValue ("mix")->load();
    pf::renderPreview (sBass.data(), sAligned.data(), kScope, dsr, pp, previewScratch);

    // ---- window: lock onto the most recent complete kick hit so the picture holds still ----------
    const float ms = kZoomMs[(size_t) zoomIdx];
    viewSamples = (double) ms * 0.001 * dsr;
    const int pre = (int) std::lround (0.12 * viewSamples);
    const int needBefore = pre + (int) (0.15 * dsr);                      // let the filters settle first
    const int needAfter  = (int) std::ceil (viewSamples) + (int) (0.012 * dsr);

    int onset = -1;
    if (kickHeard)
    {
        const float aF = 1.0f - std::exp (-1.0f / (0.002f * (float) dsr));
        const float aS = 1.0f - std::exp (-1.0f / (0.060f * (float) dsr));
        const int refractory = (int) (0.12 * dsr);
        float eF = 0.0f, eS = 0.0f;
        int lastOnset = -1000000;
        for (int i = 0; i < kScope; ++i)
        {
            const float a = std::abs (sKick[(size_t) i]);
            eF += aF * (a - eF);
            eS += aS * (a - eS);
            if (i - lastOnset > refractory && eF > 0.2f * aK && eF > 3.0f * eS + 0.02f * aK)
            {
                lastOnset = i;
                int j = i;                                                  // back up to where the hit really starts
                while (j > 0 && i - j < (int) (0.02 * dsr) && std::abs (sKick[(size_t) (j - 1)]) > 0.05f * aK) --j;
                if (j >= needBefore && j + needAfter < kScope)
                    onset = j;
            }
        }
    }

    haveTrigger = onset >= 0;
    if (haveTrigger)
    {
        triggerIdx = (double) onset;
        viewStart  = triggerIdx - (double) pre;
    }
    else
    {
        viewStart  = (double) (kScope - 1) - viewSamples - (double) (0.012 * dsr);
        triggerIdx = viewStart + (double) pre;
    }

    const int a0 = jmax (0, (int) std::floor (viewStart));
    const int a1 = jmin (kScope - 1, (int) std::ceil (viewStart + viewSamples) + 1);
    pkK = 1e-6f; pkB = 1e-6f;
    for (int i = a0; i <= a1; ++i)
    {
        pkK = jmax (pkK, std::abs (sKick[(size_t) i]));
        pkB = jmax (pkB, jmax (std::abs (sBass[(size_t) i]), std::abs (sAligned[(size_t) i])));
    }
}

//==============================================================================
//  Plain-language advice
//==============================================================================
PhaseForgeAudioProcessorEditor::Advice PhaseForgeAudioProcessorEditor::buildAdvice() const
{
    if (! proc.scConnected.load())
        return { "Connect your kick", "Click ? HELP to see how to route your kick into PhaseForge.", pfui::sun() };
    if (haveScope && ! kickHeard)
        return { "Waiting for your kick", "Press play on a loop that has your kick in it.", pfui::cream() };
    if (haveScope && ! bassHeard)
        return { "Waiting for your bass", "Press play on a loop that has this bass in it.", pfui::cream() };
    if (! proc.autoValid.load())
        return { "Listening...", "Keep it playing. I need a couple of seconds.", pfui::cream() };

    const float applied = proc.appliedShiftMs.load();
    const float best    = proc.autoShiftMs.load();
    const int bestPol   = proc.autoPolarity.load();
    const int curPol    = proc.appliedPolarity.load() >= 0.0f ? 1 : -1;

    if (bestPol != curPol)
        return { "Bass is upside-down", "Press FLIP, or MAKE IT MATCH and I'll do it for you.", pfui::orange() };

    const float err = best - applied;
    if (std::abs (err) < 0.15f)
        return { "Locked in!", "Kick and bass are moving together.", pfui::mint() };

    const String dir = err < 0.0f ? "earlier" : "later";
    return { "Move the bass " + dir,
             "About " + String (std::abs (err), 2) + " ms " + dir + ". Drag the yellow wave, or press MAKE IT MATCH.",
             pfui::orange() };
}

//==============================================================================
//  Painting
//==============================================================================
void PhaseForgeAudioProcessorEditor::text (Graphics& g, const String& s, Rectangle<float> r, float size,
                                           Colour c, Justification j, bool bold) const
{
    g.setFont (FontOptions (size, bold ? Font::bold : Font::plain));
    g.setColour (c);
    g.drawText (s, r, j, false);
}

void PhaseForgeAudioProcessorEditor::drawPanel (Graphics& g, Rectangle<float> r, const String& title, Colour accent) const
{
    const float cr = 18.0f * scale;
    g.setColour (Colours::black.withAlpha (0.40f));
    g.fillRoundedRectangle (r.translated (0.0f, 5.0f * scale), cr);
    g.setColour (pfui::ink2());
    g.fillRoundedRectangle (r, cr);
    g.setColour (accent.withAlpha (0.9f));
    g.drawRoundedRectangle (r.reduced (1.2f), cr, 2.5f);

    if (title.isNotEmpty())
    {
        const Rectangle<float> chip (r.getX() + 16.0f * scale, r.getY() - 10.0f * scale,
                                     (float) title.length() * 8.0f * scale + 28.0f * scale, 22.0f * scale);
        g.setColour (accent);
        g.fillRoundedRectangle (chip, 11.0f * scale);
        text (g, title, chip, 11.5f * scale, pfui::ink(), Justification::centred);
    }
}

void PhaseForgeAudioProcessorEditor::drawSparkle (Graphics& g, float x, float y, float size, Colour c) const
{
    Path p;
    for (int i = 0; i < 8; ++i)
    {
        const float ang = (float) i * MathConstants<float>::pi / 4.0f - MathConstants<float>::halfPi;
        const float rad = (i % 2 == 0) ? size : size * 0.28f;
        const float px = x + std::cos (ang) * rad, py = y + std::sin (ang) * rad;
        if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
    }
    p.closeSubPath();
    g.setColour (c);
    g.fillPath (p);
}

void PhaseForgeAudioProcessorEditor::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    g.setGradientFill (ColourGradient (pfui::ink(), 0.0f, 0.0f, Colour (0xff2a1250), 0.0f, b.getHeight(), false));
    g.fillAll();

    {   // sunshine glow that pulses with the kick
        const float rad = b.getWidth() * (0.38f + 0.16f * kickPulse);
        g.setGradientFill (ColourGradient (pfui::sun().withAlpha (0.10f + 0.16f * kickPulse), b.getWidth() * 0.18f, b.getHeight() * 0.12f,
                                           Colours::transparentBlack, b.getWidth() * 0.18f + rad, b.getHeight() * 0.12f, true));
        g.fillEllipse (b.getWidth() * 0.18f - rad, b.getHeight() * 0.12f - rad, rad * 2.0f, rad * 2.0f);

        const float rad2 = b.getWidth() * (0.30f + 0.10f * bassGlow);
        g.setGradientFill (ColourGradient (pfui::pink().withAlpha (0.08f + 0.12f * bassGlow), b.getWidth() * 0.88f, b.getHeight() * 0.92f,
                                           Colours::transparentBlack, b.getWidth() * 0.88f + rad2, b.getHeight() * 0.92f, true));
        g.fillEllipse (b.getWidth() * 0.88f - rad2, b.getHeight() * 0.92f - rad2, rad2 * 2.0f, rad2 * 2.0f);
    }

    const Colour confetti[4] = { pfui::sun(), pfui::pink(), pfui::mint(), pfui::sky() };
    int ci = 0;
    for (const auto& pt : particles)
    {
        const float a = (0.12f + 0.18f * std::sin (tAnim * 1.4f + pt.phase)) * (0.6f + kickPulse);
        const float s = pt.size * scale * (1.0f + kickPulse * 0.7f);
        g.setColour (confetti[ci++ % 4].withAlpha (jlimit (0.0f, 0.7f, a)));
        g.fillEllipse (pt.x * b.getWidth(), pt.y * b.getHeight(), s, s);
    }

    drawHeader (g);
    drawHero (g);
    drawFacePanel (g);
    drawKeyPanel (g);
    if (proOpen) drawProPanel (g);
}

void PhaseForgeAudioProcessorEditor::drawHeader (Graphics& g) const
{
    auto S = [this] (float v) { return v * scale; };

    // chunky logo with a hard shadow
    const Rectangle<float> logo (S (22), S (6), S (300), S (40));
    g.setFont (FontOptions (S (36), Font::bold));
    g.setColour (Colours::black.withAlpha (0.55f));
    g.drawText ("PhaseForge", logo.translated (S (3), S (3)), Justification::centredLeft, false);
    g.setColour (pfui::sun());
    g.drawText ("PhaseForge", logo, Justification::centredLeft, false);
    text (g, "make your bass and kick best friends", Rectangle<float> (S (24), S (42), S (300), S (16)), S (11.5f),
          pfui::pink(), Justification::centredLeft);

    // status badge
    const bool sc = proc.scConnected.load();
    const float pulse = 0.6f + 0.4f * std::sin (tAnim * 5.0f);
    const auto col = sc ? pfui::mint() : pfui::orange();
    const Rectangle<float> badge (S (550), S (16), S (284), S (32));
    g.setColour (Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (badge.translated (0, S (3)), S (16));
    g.setColour (pfui::ink());
    g.fillRoundedRectangle (badge, S (16));
    g.setColour (col.withAlpha (0.9f));
    g.drawRoundedRectangle (badge.reduced (1.2f), S (16), 2.5f);
    g.setColour (sc ? col : col.withAlpha (pulse));
    g.fillEllipse (badge.getX() + S (12), badge.getCentreY() - S (6), S (12), S (12));
    text (g, sc ? "KICK CONNECTED" : "NO KICK YET  -  CLICK ? HELP", badge.withTrimmedLeft (S (30)), S (12.5f), col, Justification::centredLeft);
}

//------------------------------------------------------------------------------
void PhaseForgeAudioProcessorEditor::drawHero (Graphics& g) const
{
    auto S = [this] (float v) { return v * scale; };
    drawPanel (g, heroR, {}, pfui::sun());

    // legend
    const float ly = heroR.getY() + S (46.0f);
    float lx = heroR.getX() + S (18.0f);
    auto chip = [&] (const String& label, Colour c, bool square, float w)
    {
        g.setColour (c);
        if (square) g.fillRoundedRectangle (lx, ly + S (3), S (12), S (12), S (3));
        else        g.fillEllipse (lx, ly + S (3), S (12), S (12));
        text (g, label, Rectangle<float> (lx + S (17), ly, S (w), S (18)), S (11.5f), pfui::cream(), Justification::centredLeft);
        lx += S (w) + S (24);
    };
    if (tab == 0)
    {
        chip ("KICK", pfui::pink(), false, 34);
        chip ("BASS BEFORE", pfui::cream().withAlpha (0.55f), false, 84);
        chip ("BASS AFTER", pfui::sun(), false, 76);
        chip ("ADDING UP", pfui::mint(), true, 70);
        chip ("FIGHTING", pfui::coral(), true, 62);
    }
    else
    {
        chip ("BEFORE", pfui::cream().withAlpha (0.65f), false, 52);
        chip ("AFTER (with your settings)", pfui::sun(), false, 160);
    }

    if (tab == 0) drawOverlap (g, waveR);
    else          drawFrequencyView (g, waveR);

    // ---- under the picture: how far the bass has moved ------------------------------------------------
    const float x0 = heroR.getX() + S (14.0f);
    const float r1 = heroR.getBottom() - S (96.0f);
    text (g, "MOVE BASS EARLIER", Rectangle<float> (x0, r1 - S (15), S (160), S (13)), S (10), pfui::pink(), Justification::centred);
    text (g, "MOVE BASS LATER", Rectangle<float> (x0 + S (362), r1 - S (15), S (160), S (13)), S (10), pfui::sky(), Justification::centred);

    const Rectangle<float> vb (x0 + S (166), r1, S (190), S (38));
    g.setColour (pfui::ink());
    g.fillRoundedRectangle (vb, S (12));
    g.setColour (pfui::cream().withAlpha (0.25f));
    g.drawRoundedRectangle (vb.reduced (1.0f), S (12), 1.5f);

    const double ms = (double) proc.appliedShiftMs.load();
    const double sr = jmax (1.0, proc.getSampleRate());
    const bool zero = std::abs (ms) < 0.005;
    text (g, zero ? String ("0.00 ms") : signedMs (ms), vb.withHeight (S (22)).translated (0, S (2)), S (17), pfui::sun(), Justification::centred);
    text (g, zero ? "bass is not moved" : String (ms < 0.0 ? "earlier" : "later") + "  (" + String (std::abs (roundToInt ((float) (ms * 0.001 * sr)))) + " samples)",
          vb.withTrimmedTop (S (21)), S (10.5f), pfui::cream().withAlpha (0.75f), Justification::centred);
}

//------------------------------------------------------------------------------
void PhaseForgeAudioProcessorEditor::drawOverlap (Graphics& g, Rectangle<float> r) const
{
    auto S = [this] (float v) { return v * scale; };
    const float cr = S (14.0f);
    g.setColour (pfui::ink().darker (0.35f));
    g.fillRoundedRectangle (r, cr);
    g.setColour (pfui::cream().withAlpha (0.12f));
    g.drawRoundedRectangle (r.reduced (0.8f), cr, 1.5f);

    if (! haveScope || ! kickHeard || ! bassHeard)
    {
        String msg = "Press play - waiting for audio...";
        if (haveScope)
        {
            if (! proc.scConnected.load()) msg = "No kick connected yet - click ? HELP";
            else if (! kickHeard)          msg = "I can't hear your kick yet";
            else                           msg = "I can't hear your bass yet";
        }
        text (g, msg, r, S (17), pfui::cream().withAlpha (0.7f), Justification::centred);
        return;
    }

    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r.toNearestInt());

    const float stripH = S (10);
    const float usable = r.getHeight() - stripH - S (22);
    const float cy  = r.getY() + usable * 0.5f + S (6);
    const float amp = usable * 0.5f * 0.92f;
    const float stripY = r.getBottom() - stripH - S (7);

    const double dsr = proc.decimatedRate.load();
    const float Wms = kZoomMs[(size_t) zoomIdx];
    const float pxPerMs = r.getWidth() / Wms;
    const double msPerSample = 1000.0 / dsr;
    const double tLeft = (viewStart - triggerIdx) * msPerSample;                 // ms before the hit at the left edge

    // ---- time grid ---------------------------------------------------------------------------------
    float step = 20.0f;
    for (float st : { 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f })
        if (st * pxPerMs >= S (62.0f)) { step = st; break; }

    for (double t = std::ceil (tLeft / (double) step) * (double) step; t <= tLeft + (double) Wms; t += (double) step)
    {
        const float x = r.getX() + (float) (t - tLeft) * pxPerMs;
        g.setColour (pfui::cream().withAlpha (0.07f));
        g.drawLine (x, r.getY() + S (4), x, stripY - S (3), 1.0f);
        if (haveTrigger)
            text (g, String (t, step < 1.0f ? 1 : 0) + " ms", Rectangle<float> (x - S (30), r.getY() + S (3), S (60), S (12)),
                  S (9.5f), pfui::cream().withAlpha (0.4f), Justification::centred);
    }
    g.setColour (pfui::cream().withAlpha (0.18f));
    g.drawLine (r.getX(), cy, r.getRight(), cy, 1.2f);

    if (haveTrigger && tLeft < 0.0 && tLeft + (double) Wms > 0.0)                 // the kick hit
    {
        const float x = r.getX() + (float) (0.0 - tLeft) * pxPerMs;
        const float dashes[2] = { S (5), S (4) };
        g.setColour (pfui::pink().withAlpha (0.7f));
        g.drawDashedLine (Line<float> (x, r.getY() + S (16), x, stripY - S (3)), dashes, 2, 1.6f);
        text (g, "KICK HIT", Rectangle<float> (x + S (4), r.getY() + S (15), S (70), S (12)), S (9.5f), pfui::pink(), Justification::centredLeft);
    }

    // ---- sample the three waves at one point per ~2 px ------------------------------------------------
    constexpr int kMaxCols = 1400;
    const int cols = jlimit (2, kMaxCols - 1, (int) std::floor (r.getWidth() / S (2.0f)));
    const float colW = r.getWidth() / (float) cols;

    static thread_local std::array<float, kMaxCols> kn, bn, an;
    for (int i = 0; i <= cols; ++i)
    {
        const double pos = viewStart + (double) i / (double) cols * viewSamples;
        kn[(size_t) i] = pf::readFrac (sKick.data(),    kScope, pos) / pkK;
        bn[(size_t) i] = pf::readFrac (sBass.data(),    kScope, pos) / pkB;
        an[(size_t) i] = pf::readFrac (sAligned.data(), kScope, pos) / pkB;
    }
    auto yOf = [&] (float v) { return cy - jlimit (-1.2f, 1.2f, v) * amp; };

    // ---- green where kick and bass agree, red where they fight ------------------------------------------
    for (int i = 0; i < cols; ++i)
    {
        const float x = r.getX() + (float) i * colW;
        const float k = kn[(size_t) i], a = an[(size_t) i];
        const bool agree = (k * a) >= 0.0f;
        const float y1 = yOf (k), y2 = yOf (a);
        g.setColour ((agree ? pfui::mint() : pfui::coral()).withAlpha (agree ? 0.24f : 0.40f));
        g.fillRect (x, jmin (y1, y2), colW + 0.7f, std::abs (y1 - y2) + 0.6f);

        const float strength = jlimit (0.0f, 1.0f, std::abs (k * a) * 2.0f);
        g.setColour ((agree ? pfui::mint() : pfui::coral()).withAlpha (0.20f + 0.70f * strength));
        g.fillRect (x, stripY, colW + 0.7f, stripH);
    }
    text (g, "ADDING UP  <--  strip  -->  FIGHTING", Rectangle<float> (r.getX(), stripY - S (14), r.getWidth(), S (12)),
          S (9), pfui::cream().withAlpha (0.35f), Justification::centred);

    // ---- the three waves ------------------------------------------------------------------------------------
    Path pk, pb, pa;
    for (int i = 0; i <= cols; ++i)
    {
        const float x = r.getX() + (float) i * colW;
        if (i == 0) { pk.startNewSubPath (x, yOf (kn[0])); pb.startNewSubPath (x, yOf (bn[0])); pa.startNewSubPath (x, yOf (an[0])); }
        else        { pk.lineTo (x, yOf (kn[(size_t) i])); pb.lineTo (x, yOf (bn[(size_t) i])); pa.lineTo (x, yOf (an[(size_t) i])); }
    }

    Path dashed;
    const float dl[2] = { S (7), S (5) };
    PathStrokeType (S (1.8f)).createDashedStroke (dashed, pb, dl, 2);
    g.setColour (pfui::cream().withAlpha (0.50f));
    g.fillPath (dashed);

    g.setColour (pfui::pink().withAlpha (0.25f));
    g.strokePath (pk, PathStrokeType (S (9.0f), PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (pfui::pink());
    g.strokePath (pk, PathStrokeType (S (3.4f), PathStrokeType::curved, PathStrokeType::rounded));

    g.setColour (pfui::sun().withAlpha (0.25f));
    g.strokePath (pa, PathStrokeType (S (9.0f), PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (pfui::sun());
    g.strokePath (pa, PathStrokeType (S (3.4f), PathStrokeType::curved, PathStrokeType::rounded));

    // ---- hints -----------------------------------------------------------------------------------------------
    text (g, "drag to slide the bass   |   hold SHIFT = tiny moves   |   scroll = 0.1 ms   |   double-click = reset",
          Rectangle<float> (r.getX() + S (12), stripY - S (30), r.getWidth() - S (24), S (14)), S (10), pfui::cream().withAlpha (0.45f), Justification::centredLeft, false);

    if (holdB.getToggleState())
    {
        const Rectangle<float> badge (r.getRight() - S (96), r.getY() + S (18), S (84), S (22));
        g.setColour (pfui::sky());
        g.fillRoundedRectangle (badge, S (11));
        text (g, "FROZEN", badge, S (11), pfui::ink(), Justification::centred);
    }
}

//------------------------------------------------------------------------------
//  By-frequency view: phase of the bass relative to the kick at every pitch
//------------------------------------------------------------------------------
void PhaseForgeAudioProcessorEditor::drawFrequencyView (Graphics& g, Rectangle<float> r) const
{
    auto S = [this] (float v) { return v * scale; };
    const float cr = S (14.0f);
    g.setColour (pfui::ink().darker (0.35f));
    g.fillRoundedRectangle (r, cr);
    g.setColour (pfui::cream().withAlpha (0.12f));
    g.drawRoundedRectangle (r.reduced (0.8f), cr, 1.5f);

    const double dsr = proc.decimatedRate.load();
    const auto& vts = proc.apvts;

    const double shiftMs = (double) proc.engineShiftMs.load();
    const double pol     = proc.appliedPolarity.load() >= 0.0f ? 1.0 : -1.0;
    const bool   low     = vts.getRawParameterValue ("band")->load() > 0.5f;
    const int    stages  = (int) std::lround (vts.getRawParameterValue ("rotStages")->load());
    const double rotQ    = (double) vts.getRawParameterValue ("rotQ")->load();
    const double mixV    = (double) vts.getRawParameterValue ("mix")->load();
    const double xo      = (double) proc.effXover.load();
    const double rotF    = (double) proc.effRotFreq.load();

    std::array<float, kBins> before {}, after {}, weight {};
    float wMax = 0.0f;
    for (int i = 0; i < kBins; ++i)
    {
        const double f = pf::PhaseSpectrum::binFreq (i, dsr);
        const auto resp  = pf::AlignEngine::responseAt (f, shiftMs, pol, low, xo, stages, rotF, rotQ);
        const auto blend = (1.0 - mixV) + mixV * resp;
        before[(size_t) i] = wrapDeg (specPh[(size_t) i] * 57.2957795f);
        after[(size_t) i]  = wrapDeg (before[(size_t) i] + (float) (std::arg (blend) * 57.29577951308232));
        weight[(size_t) i] = specCo[(size_t) i] * specB[(size_t) i] * specK[(size_t) i];
        wMax = jmax (wMax, weight[(size_t) i]);
    }

    if (! proc.scConnected.load() || wMax <= 1e-9f)
    {
        text (g, proc.scConnected.load() ? "Press play - this lights up when it hears kick + bass"
                                         : "No kick connected yet - click ? HELP",
              r, S (16), pfui::cream().withAlpha (0.7f), Justification::centred);
        return;
    }

    float sumW = 0.0f, errB = 0.0f, errA = 0.0f;
    for (int i = 0; i < kBins; ++i)
    {
        const float w = weight[(size_t) i] / wMax;
        sumW += w; errB += w * std::abs (before[(size_t) i]); errA += w * std::abs (after[(size_t) i]);
    }
    if (sumW > 0.0f) { errB /= sumW; errA /= sumW; }

    auto plot = r.reduced (S (46), S (22)).withTrimmedBottom (S (4));
    constexpr float fLo = 20.0f, fHi = 400.0f;
    const float logSpan = std::log (fHi / fLo);
    auto xOf = [&] (float f) { return plot.getX() + std::log (jlimit (fLo, fHi, f) / fLo) / logSpan * plot.getWidth(); };
    auto yOf = [&] (float deg) { return plot.getCentreY() - (deg / 180.0f) * plot.getHeight() * 0.5f; };

    g.setColour (pfui::mint().withAlpha (0.10f));                                  // "in phase" zone
    g.fillRect (Rectangle<float> (plot.getX(), yOf (30.0f), plot.getWidth(), yOf (-30.0f) - yOf (30.0f)));
    g.setColour (pfui::cream().withAlpha (0.07f));
    for (float f : { 30.0f, 40.0f, 50.0f, 60.0f, 80.0f, 100.0f, 150.0f, 200.0f, 300.0f })
        g.drawLine (xOf (f), plot.getY(), xOf (f), plot.getBottom(), 1.0f);
    for (float d : { -180.0f, -90.0f, 90.0f, 180.0f })
        g.drawLine (plot.getX(), yOf (d), plot.getRight(), yOf (d), 1.0f);
    g.setColour (pfui::mint().withAlpha (0.6f));
    g.drawLine (plot.getX(), yOf (0.0f), plot.getRight(), yOf (0.0f), 2.0f);

    for (float f : { 20.0f, 30.0f, 50.0f, 100.0f, 200.0f, 400.0f })
        text (g, String ((int) f) + " Hz", Rectangle<float> (xOf (f) - S (24), plot.getBottom() + S (1), S (48), S (12)), S (9.5f), pfui::cream().withAlpha (0.45f), Justification::centred);
    text (g, "IN PHASE", Rectangle<float> (r.getX() + S (4), yOf (0.0f) - S (7), S (44), S (14)), S (8.5f), pfui::mint(), Justification::centredRight);
    text (g, "bass LATE", Rectangle<float> (r.getX() + S (4), yOf (-90.0f) - S (7), S (44), S (14)), S (8.5f), pfui::cream().withAlpha (0.5f), Justification::centredRight);
    text (g, "bass EARLY", Rectangle<float> (r.getX() + S (2), yOf (90.0f) - S (7), S (46), S (14)), S (8.5f), pfui::cream().withAlpha (0.5f), Justification::centredRight);

    text (g, "Each dot is one pitch in your bass. Dots on the green line are in phase with the kick.",
          Rectangle<float> (r.getX() + S (50), r.getY() + S (3), r.getWidth() - S (60), S (16)), S (10.5f), pfui::cream().withAlpha (0.6f), Justification::centredLeft, false);
    text (g, "average error:  " + String (roundToInt (errB)) + String::fromUTF8 ("\xC2\xB0  ->  ") + String (roundToInt (errA)) + String::fromUTF8 ("\xC2\xB0"),
          Rectangle<float> (r.getRight() - S (240), r.getBottom() - S (36), S (226), S (16)), S (12), errA <= errB ? pfui::mint() : pfui::orange(), Justification::centredRight);

    // spectra backdrop
    float pB = 1e-12f, pK = 1e-12f;
    for (int i = 0; i < kBins; ++i) { pB = jmax (pB, specB[(size_t) i]); pK = jmax (pK, specK[(size_t) i]); }
    auto backdrop = [&] (const std::array<float, kBins>& m, float peak, Colour c)
    {
        Path p;
        p.startNewSubPath (xOf ((float) pf::PhaseSpectrum::binFreq (0, dsr)), plot.getBottom());
        for (int i = 0; i < kBins; ++i)
            p.lineTo (xOf ((float) pf::PhaseSpectrum::binFreq (i, dsr)), plot.getBottom() - std::sqrt (m[(size_t) i] / peak) * plot.getHeight() * 0.45f);
        p.lineTo (xOf ((float) pf::PhaseSpectrum::binFreq (kBins - 1, dsr)), plot.getBottom());
        p.closeSubPath();
        g.setColour (c);
        g.fillPath (p);
    };
    backdrop (specK, pK, pfui::pink().withAlpha (0.13f));
    backdrop (specB, pB, pfui::cream().withAlpha (0.09f));

    auto drawPhase = [&] (const std::array<float, kBins>& ph, Colour c, float dotScale)
    {
        for (int i = 0; i < kBins; ++i)
        {
            const float f = (float) pf::PhaseSpectrum::binFreq (i, dsr);
            const float w = jlimit (0.0f, 1.0f, weight[(size_t) i] / wMax);
            const float co = jlimit (0.0f, 1.0f, specCo[(size_t) i]);
            const float x = xOf (f), y = yOf (ph[(size_t) i]);
            if (i > 0)
            {
                const float w0 = jlimit (0.0f, 1.0f, weight[(size_t) i - 1] / wMax);
                if (std::abs (ph[(size_t) i] - ph[(size_t) i - 1]) < 150.0f && jmin (w, w0) > 0.01f)
                {
                    g.setColour (c.withAlpha (0.15f + 0.55f * jmin (w, w0)));
                    g.drawLine (xOf ((float) pf::PhaseSpectrum::binFreq (i - 1, dsr)), yOf (ph[(size_t) i - 1]), x, y, S (1.8f) * dotScale);
                }
            }
            const float d = S (2.0f + 5.5f * std::sqrt (w)) * dotScale;
            g.setColour (c.withAlpha (0.12f + 0.25f * co));
            g.fillEllipse (x - d, y - d, d * 2.0f, d * 2.0f);
            g.setColour (c.withAlpha (0.30f + 0.65f * jmax (w, co * 0.4f)));
            g.fillEllipse (x - d * 0.5f, y - d * 0.5f, d, d);
        }
    };
    drawPhase (before, pfui::cream(), 1.0f);
    drawPhase (after,  pfui::sun(),   1.2f);

    if (plot.expanded (S (4)).contains (mousePos))                                   // hover read-out
    {
        const float f = fLo * std::exp ((mousePos.x - plot.getX()) / plot.getWidth() * logSpan);
        int best = 0; float bd = 1e9f;
        for (int i = 0; i < kBins; ++i)
        {
            const float d = std::abs ((float) pf::PhaseSpectrum::binFreq (i, dsr) - f);
            if (d < bd) { bd = d; best = i; }
        }
        g.setColour (pfui::cream().withAlpha (0.35f));
        g.drawLine (mousePos.x, plot.getY(), mousePos.x, plot.getBottom(), 1.0f);
        const float bf = (float) pf::PhaseSpectrum::binFreq (best, dsr);
        const String s = String (bf, 1) + " Hz (" + noteLabel (bf) + ")   before " + degText (before[(size_t) best])
                       + "   after " + degText (after[(size_t) best]);
        const auto bx = Rectangle<float> (S (300), S (20)).withCentre ({ jlimit (plot.getX() + S (150), plot.getRight() - S (150), mousePos.x), plot.getY() + S (26) });
        g.setColour (pfui::ink());
        g.fillRoundedRectangle (bx, S (8));
        g.setColour (pfui::sun());
        g.drawRoundedRectangle (bx, S (8), 1.8f);
        text (g, s, bx, S (11), pfui::cream(), Justification::centred);
    }
}

//------------------------------------------------------------------------------
//  The face: its mouth is a wave that smooths into a smile as the match improves
//------------------------------------------------------------------------------
void PhaseForgeAudioProcessorEditor::drawFace (Graphics& g, Rectangle<float> box, float match) const
{
    const auto c = box.getCentre();
    const float R = jmin (box.getWidth(), box.getHeight()) * 0.5f * (1.0f + 0.07f * kickPulse);
    const bool known = match >= 0.0f;
    const float m = known ? match : 0.5f;

    Colour mood = pfui::coral().interpolatedWith (pfui::sun(), jlimit (0.0f, 1.0f, m * 2.0f))
                               .interpolatedWith (pfui::mint(), jlimit (0.0f, 1.0f, (m - 0.5f) * 2.0f));
    if (! known) mood = pfui::cream().withAlpha (0.5f);

    g.setColour (mood.withAlpha (0.28f));
    g.fillEllipse (c.x - R - 9.0f * scale, c.y - R - 9.0f * scale, (R + 9.0f * scale) * 2.0f, (R + 9.0f * scale) * 2.0f);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - R, c.y - R + 5.0f * scale, R * 2.0f, R * 2.0f);
    g.setGradientFill (ColourGradient (pfui::sun().brighter (0.25f), c.x - R * 0.3f, c.y - R * 0.5f, pfui::orange(), c.x + R * 0.5f, c.y + R, false));
    g.fillEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f);
    g.setColour (pfui::ink());
    g.drawEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f, 4.5f * scale);

    if (known && m > 0.6f)                                                          // rosy cheeks
    {
        g.setColour (pfui::coral().withAlpha (0.40f));
        g.fillEllipse (c.x - R * 0.62f, c.y + R * 0.02f, R * 0.26f, R * 0.18f);
        g.fillEllipse (c.x + R * 0.36f, c.y + R * 0.02f, R * 0.26f, R * 0.18f);
    }

    // ---- eyes -------------------------------------------------------------------------------------
    const float ey = c.y - R * 0.20f;
    const float look = std::sin (tAnim * 0.7f) * R * 0.02f;
    const float blink = std::fmod (tAnim, 3.7f) < 0.12f ? 0.15f : 1.0f;
    for (int side = -1; side <= 1; side += 2)
    {
        const float ex = c.x + (float) side * R * 0.34f + look;
        if (! known)
        {
            g.setColour (pfui::ink());
            g.drawLine (ex - R * 0.13f, ey, ex + R * 0.13f, ey, 4.5f * scale);
        }
        else if (m >= 0.88f)
        {
            Path arc;
            arc.addCentredArc (ex, ey + R * 0.06f, R * 0.13f, R * 0.13f, 0.0f, -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
            g.setColour (pfui::ink());
            g.strokePath (arc, PathStrokeType (4.5f * scale, PathStrokeType::curved, PathStrokeType::rounded));
        }
        else
        {
            const float ew = R * 0.17f, eh = R * 0.24f * blink;
            g.setColour (pfui::ink());
            g.fillEllipse (ex - ew * 0.5f, ey - eh * 0.5f, ew, eh);
            if (blink > 0.5f)
            {
                g.setColour (Colours::white);
                g.fillEllipse (ex - ew * 0.05f, ey - eh * 0.32f, ew * 0.34f, ew * 0.34f);
            }
            if (m < 0.45f)                                                          // worried brows
            {
                g.setColour (pfui::ink());
                g.drawLine (ex - (float) side * R * 0.14f, ey - R * 0.22f, ex + (float) side * R * 0.14f, ey - R * 0.31f, 4.0f * scale);
            }
        }
    }

    // ---- mouth: a wave that settles into a smile ---------------------------------------------------
    const float mx = c.x, my = c.y + R * 0.30f, hw = R * 0.38f;
    const float cv = known ? (m * 2.0f - 1.0f) : 0.15f;
    const float wobble = known ? (1.0f - m) * R * 0.075f : 0.0f;

    Path mouth;
    const int N = 32;
    for (int i = 0; i <= N; ++i)
    {
        const float t = (float) i / (float) N;
        const float x = mx + (t - 0.5f) * 2.0f * hw;
        float y = my + cv * R * 0.26f * (1.0f - (2.0f * t - 1.0f) * (2.0f * t - 1.0f));
        y += wobble * std::sin (t * 18.8f + tAnim * 7.0f) * std::sin (t * MathConstants<float>::pi);
        if (i == 0) mouth.startNewSubPath (x, y); else mouth.lineTo (x, y);
    }

    if (known && cv > 0.62f)                                                        // big open smile
    {
        Path open (mouth);
        open.lineTo (mx + hw, my - R * 0.02f);
        open.lineTo (mx - hw, my - R * 0.02f);
        open.closeSubPath();
        g.setColour (pfui::ink());
        g.fillPath (open);
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (open);
            g.setColour (pfui::pink());
            g.fillEllipse (mx - hw * 0.45f, my + cv * R * 0.15f, hw * 0.9f, R * 0.30f);
        }
        g.setColour (pfui::ink());
        g.strokePath (open, PathStrokeType (4.0f * scale, PathStrokeType::curved, PathStrokeType::rounded));
    }
    else
    {
        g.setColour (pfui::ink());
        g.strokePath (mouth, PathStrokeType (5.0f * scale, PathStrokeType::curved, PathStrokeType::rounded));
    }

    if (known && m >= 0.93f)                                                        // sparkles when locked
    {
        const float k = 0.6f + 0.4f * std::sin (tAnim * 6.0f);
        drawSparkle (g, c.x - R * 1.15f, c.y - R * 0.65f, R * 0.16f * k, pfui::mint());
        drawSparkle (g, c.x + R * 1.18f, c.y - R * 0.30f, R * 0.20f * (1.1f - k * 0.4f), pfui::sun());
        drawSparkle (g, c.x + R * 0.95f, c.y + R * 0.80f, R * 0.14f * k, pfui::pink());
    }
}

void PhaseForgeAudioProcessorEditor::drawFacePanel (Graphics& g) const
{
    auto S = [this] (float v) { return v * scale; };
    drawPanel (g, faceR, "HOW IN-SYNC?", pfui::pink());

    const auto adv = buildAdvice();

    const float fs = S (124);
    drawFace (g, Rectangle<float> (faceR.getCentreX() - fs * 0.5f, faceR.getY() + S (20), fs, fs), matchS);

    Colour numCol = pfui::cream().withAlpha (0.6f);
    if (matchS >= 0.0f)
        numCol = pfui::coral().interpolatedWith (pfui::sun(), jlimit (0.0f, 1.0f, matchS * 2.0f))
                              .interpolatedWith (pfui::mint(), jlimit (0.0f, 1.0f, (matchS - 0.5f) * 2.0f));
    text (g, matchS >= 0.0f ? String (roundToInt (matchS * 100.0f)) + "% MATCH" : String ("-- % MATCH"),
          Rectangle<float> (faceR.getX(), faceR.getY() + S (152), faceR.getWidth(), S (30)), S (24), numCol, Justification::centred);

    text (g, adv.headline, Rectangle<float> (faceR.getX() + S (12), faceR.getY() + S (186), faceR.getWidth() - S (24), S (22)), S (17), adv.colour, Justification::centred);

    g.setFont (FontOptions (S (12), Font::plain));
    g.setColour (pfui::cream().withAlpha (0.82f));
    g.drawFittedText (adv.detail, Rectangle<float> (faceR.getX() + S (18), faceR.getY() + S (208), faceR.getWidth() - S (36), S (36)).toNearestInt(),
                      Justification::centredTop, 3);
}

//------------------------------------------------------------------------------
void PhaseForgeAudioProcessorEditor::drawKeyPanel (Graphics& g) const
{
    auto S = [this] (float v) { return v * scale; };
    const auto r = keyR;
    drawPanel (g, r, "SONG KEY", pfui::sky());

    const bool manual = proc.apvts.getRawParameterValue ("keyMode")->load() > 0.5f;
    int root; bool minor; bool valid;
    if (manual)
    {
        root  = (int) std::lround (proc.apvts.getRawParameterValue ("keyRoot")->load());
        minor = proc.apvts.getRawParameterValue ("keyScale")->load() < 0.5f;
        valid = true;
    }
    else
    {
        root = proc.keyRoot.load(); minor = proc.keyMinor.load(); valid = proc.keyValid.load();
    }

    const auto keyCol = valid ? pfui::sun() : pfui::cream().withAlpha (0.4f);
    const String keyText = valid ? String (pf::noteName (root)) + (minor ? " minor" : " major") : String ("listening...");
    text (g, keyText, Rectangle<float> (r.getX() + S (12), r.getY() + S (16), r.getWidth() - S (24), S (44)), S (34), keyCol, Justification::centred);
    text (g, manual ? "you picked this key" : (valid ? "found by listening to your bass" : "play some bass and I'll find the key"),
          Rectangle<float> (r.getX() + S (12), r.getY() + S (58), r.getWidth() - S (24), S (14)), S (11), pfui::cream().withAlpha (0.6f), Justification::centred, false);

    const float bHz = proc.bassHz.load(), kHz = proc.kickHz.load();
    auto noteBox = [&] (float x, const char* name, float hz, Colour c)
    {
        const Rectangle<float> bx (r.getX() + S (x), r.getY() + S (80), S (138), S (54));
        g.setColour (pfui::ink());
        g.fillRoundedRectangle (bx, S (12));
        g.setColour (c.withAlpha (0.9f));
        g.drawRoundedRectangle (bx.reduced (1.0f), S (12), 2.0f);
        text (g, name, bx.withHeight (S (16)).translated (0, S (3)), S (10), c, Justification::centred);
        text (g, noteLabel (hz) + (hz > 0 ? "   " + String (hz, 1) + " Hz" : String()), bx.withTrimmedTop (S (18)), S (17),
              hz > 0 ? pfui::cream() : pfui::cream().withAlpha (0.3f), Justification::centred);
    };
    noteBox (12.0f, "BASS NOTE", bHz, pfui::cream());
    noteBox (162.0f, "KICK NOTE", kHz, pfui::pink());

    // is the kick in tune with the key?
    String tune = "Play your kick to check its tuning.";
    Colour tuneCol = pfui::cream().withAlpha (0.6f);
    if (kHz > 0.0f && valid)
    {
        double delta = 0.0;
        pf::nearestInKey (pf::freqToMidi (kHz), root, minor, delta);
        const double cents = -delta * 100.0;                                        // + = kick is sharp
        if (std::abs (cents) < 12.0) { tune = "Your kick is in tune with the key."; tuneCol = pfui::mint(); }
        else
        {
            tune = String ("Your kick is a bit ") + (cents > 0 ? "HIGH" : "LOW") + " for the key. Tune it "
                 + (cents > 0 ? "down" : "up") + " about " + String (roundToInt ((float) std::abs (cents))) + " cents.";
            tuneCol = pfui::orange();
        }
    }
    g.setFont (FontOptions (S (12.5f), Font::bold));
    g.setColour (tuneCol);
    g.drawFittedText (tune, Rectangle<float> (r.getX() + S (14), r.getY() + S (140), r.getWidth() - S (28), S (36)).toNearestInt(), Justification::centredLeft, 2);

    text (g, "key finder:", Rectangle<float> (r.getX() + S (14), r.getBottom() - S (60), S (100), S (14)), S (10), pfui::cream().withAlpha (0.45f), Justification::centredLeft, false);
}

//------------------------------------------------------------------------------
void PhaseForgeAudioProcessorEditor::drawProPanel (Graphics& g) const
{
    auto S = [this] (float v) { return v * scale; };
    drawPanel (g, proR, "PRO TOOLS", pfui::orange());

    text (g, "WHAT TO MOVE", Rectangle<float> (proR.getX() + S (16), proR.getY() + S (22), S (170), S (14)), S (10), pfui::cream().withAlpha (0.7f), Justification::centredLeft);
    for (const auto& k : proKnobs)
    {
        const auto kb = k.s->getBounds().toFloat();
        text (g, k.name, Rectangle<float> (kb.getX(), kb.getY() - S (3), kb.getWidth(), S (13)), S (10.5f), pfui::cream().withAlpha (0.85f), Justification::centred);
    }
}
