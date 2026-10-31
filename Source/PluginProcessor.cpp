#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
//  Background analysis thread
//==============================================================================
class PhaseForgeAudioProcessor::AnalysisThread : public juce::Thread
{
public:
    explicit AnalysisThread (PhaseForgeAudioProcessor& o) : juce::Thread ("PhaseForge Analysis"), owner (o) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            owner.runAnalysisStep();
            wait (40);
        }
    }

private:
    PhaseForgeAudioProcessor& owner;
};

//==============================================================================
//  Presets
//==============================================================================
namespace
{
struct Preset
{
    const char* name;
    float shift; bool pol; bool autoOn; float speed;
    float rotFreq; int stages; float rotQ;
    int band; float xover; bool follow; float mix; float out;
};

const Preset kPresets[] =
{
    { "Init",                  0.f, false, false, 0.4f, 60.f, 0, 0.7f, 0, 120.f, false, 1.f, 0.f },
    { "Auto Lock",             0.f, false, true,  0.4f, 60.f, 0, 0.7f, 0, 120.f, false, 1.f, 0.f },
    { "Tight Sub (Low Band)",  0.f, false, true,  0.5f, 60.f, 0, 0.7f, 1, 120.f, true,  1.f, 0.f },
    { "Punch Rotate",          0.f, false, false, 0.4f, 70.f, 2, 0.9f, 1, 140.f, false, 1.f, 0.f },
    { "Key Follow Rotate",     0.f, false, true,  0.4f, 60.f, 1, 0.8f, 1, 120.f, true,  1.f, 0.f },
    { "Polarity Flip Test",    0.f, true,  false, 0.4f, 60.f, 0, 0.7f, 0, 120.f, false, 1.f, 0.f },
};
constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PhaseForgeAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto pid = [] (const char* id) { return ParameterID { id, 1 }; };

    auto msText   = [] (float v, int) { return String (v, 2) + " ms"; };
    auto hzText   = [] (float v, int) { return String (v, 1) + " Hz"; };
    auto pctText  = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };
    auto dbText   = [] (float v, int) { return String (v, 1) + " dB"; };
    auto plainVal = [] (const String& t) { return t.getFloatValue(); };
    auto pctVal   = [] (const String& t) { return t.getFloatValue() / 100.0f; };

    layout.add (std::make_unique<AudioParameterFloat> (pid ("shift"), "Shift (ms)",
        NormalisableRange<float> (-10.0f, 10.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText).withValueFromStringFunction (plainVal)));

    layout.add (std::make_unique<AudioParameterBool> (pid ("polarity"), "Polarity Flip", false));
    layout.add (std::make_unique<AudioParameterBool> (pid ("auto"),     "Auto Align",    false));

    layout.add (std::make_unique<AudioParameterFloat> (pid ("speed"), "Track Speed",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.4f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pctText).withValueFromStringFunction (pctVal)));

    layout.add (std::make_unique<AudioParameterInt> (pid ("rotStages"), "Phase Rotate (stages)", 0, 4, 0));

    layout.add (std::make_unique<AudioParameterFloat> (pid ("rotFreq"), "Rotate Freq",
        NormalisableRange<float> (20.0f, 400.0f, 0.1f, 0.5f), 60.0f,
        AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hzText).withValueFromStringFunction (plainVal)));

    layout.add (std::make_unique<AudioParameterFloat> (pid ("rotQ"), "Rotate Q",
        NormalisableRange<float> (0.3f, 4.0f, 0.01f, 0.5f), 0.7f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); })
                                       .withValueFromStringFunction (plainVal)));

    layout.add (std::make_unique<AudioParameterChoice> (pid ("band"), "Band Mode", StringArray { "Full", "Low Band" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (pid ("xover"), "Crossover",
        NormalisableRange<float> (70.0f, 400.0f, 0.1f, 0.5f), 120.0f,
        AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hzText).withValueFromStringFunction (plainVal)));

    layout.add (std::make_unique<AudioParameterBool> (pid ("follow"), "Key Follow", false));

    layout.add (std::make_unique<AudioParameterFloat> (pid ("mix"), "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pctText).withValueFromStringFunction (pctVal)));

    layout.add (std::make_unique<AudioParameterFloat> (pid ("out"), "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText).withValueFromStringFunction (plainVal)));

    layout.add (std::make_unique<AudioParameterChoice> (pid ("keyMode"),  "Key Mode",  StringArray { "Auto", "Manual" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (pid ("keyRoot"),  "Key Root",
        StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (pid ("keyScale"), "Key Scale", StringArray { "Minor", "Major" }, 0));

    return layout;
}

//==============================================================================
PhaseForgeAudioProcessor::PhaseForgeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",   juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Kick SC", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Output",  juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PhaseForgeState", createParameterLayout())
{
    pShift     = apvts.getRawParameterValue ("shift");
    pPolarity  = apvts.getRawParameterValue ("polarity");
    pAuto      = apvts.getRawParameterValue ("auto");
    pSpeed     = apvts.getRawParameterValue ("speed");
    pRotFreq   = apvts.getRawParameterValue ("rotFreq");
    pRotStages = apvts.getRawParameterValue ("rotStages");
    pRotQ      = apvts.getRawParameterValue ("rotQ");
    pBand      = apvts.getRawParameterValue ("band");
    pXover     = apvts.getRawParameterValue ("xover");
    pFollow    = apvts.getRawParameterValue ("follow");
    pMix       = apvts.getRawParameterValue ("mix");
    pOut       = apvts.getRawParameterValue ("out");

    analysisThread = std::make_unique<AnalysisThread> (*this);
    analysisThread->startThread (juce::Thread::Priority::low);
}

PhaseForgeAudioProcessor::~PhaseForgeAudioProcessor()
{
    if (analysisThread != nullptr)
        analysisThread->stopThread (2000);

    cancelPendingUpdate();
}

//==============================================================================
bool PhaseForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != out)
        return false;

    const auto& sc = layouts.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void PhaseForgeAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz = sampleRate;

    engine.prepare (sampleRate);
    setLatencySamples (engine.getLatency());

    decFactor = pf::decimationFactor (sampleRate);
    decBass.prepare (sampleRate);
    decKick.prepare (sampleRate);
    decCounter = 0;
    decimatedRate.store (sampleRate / (double) decFactor);

    {
        const juce::SpinLock::ScopedLockType sl (ringLock);
        ringWrite = 0;
        ringBass.fill (0.0f);
        ringKick.fill (0.0f);
    }

    smShift.reset (sampleRate, 0.05);
    smPol.reset   (sampleRate, 0.006);
    smMix.reset   (sampleRate, 0.02);
    smGain.reset  (sampleRate, 0.02);
    firstBlock = true;
    followFreq = 0.0f;

    resetAnalyzer.store (true);
}

//==============================================================================
void PhaseForgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (getMainBusNumInputChannels(), 2);
    if (numCh <= 0 || n <= 0)
        return;

    // ---- sidechain (kick) input ------------------------------------------------
    const float* sc[2] = { nullptr, nullptr };
    int scCh = 0;
    if (getBusCount (true) > 1)
        if (auto* scBus = getBus (true, 1))
            if (scBus->isEnabled() && scBus->getNumberOfChannels() > 0)
            {
                auto scBuf = getBusBuffer (buffer, true, 1);
                scCh = juce::jmin (scBuf.getNumChannels(), 2);
                for (int c = 0; c < scCh; ++c)
                    sc[c] = scBuf.getReadPointer (c);
            }
    scConnected.store (scCh > 0);

    // ---- parameters -------------------------------------------------------------
    const bool autoOn  = pAuto->load()   > 0.5f;
    const bool lowBand = pBand->load()   > 0.5f;
    const bool follow  = pFollow->load() > 0.5f;
    const int  stages  = (int) std::lround (pRotStages->load());
    const float rotQ   = pRotQ->load();

    float shiftMs = pShift->load();
    float pol     = pPolarity->load() > 0.5f ? -1.0f : 1.0f;
    float rotF    = pRotFreq->load();
    float xo      = pXover->load();

    // Key Follow: the rotation frequency (and crossover) track the detected bass fundamental.
    const float bHz = bassHz.load();
    if (follow && bassConf.load() > 0.6f && bHz > 20.0f)
    {
        const float target = juce::jlimit (20.0f, 400.0f, bHz);
        const float a = 1.0f - std::exp (-(float) n / (float) sampleRateHz / 0.08f);
        followFreq = followFreq <= 0.0f ? target : followFreq + a * (target - followFreq);
    }
    if (follow && followFreq > 0.0f)
    {
        rotF = followFreq;
        xo   = juce::jlimit (70.0f, 400.0f, followFreq * 3.0f);
    }

    // The LR4 crossover delays the low band by a few ms before any shift is applied.
    const float compMs = lowBand ? (float) pf::AlignEngine::lowBandPhaseDelayMs (xo, bHz > 20.0f ? bHz : 55.0f) : 0.0f;
    lowBandCompMs.store (compMs);

    if (autoOn && autoValid.load())
    {
        shiftMs += autoShiftMs.load();
        pol     *= (float) autoPolarity.load();
        shiftMs -= compMs;                        // hit the true target even in Low Band mode
    }
    shiftMs = juce::jlimit (-10.0f, 10.0f, shiftMs);

    engine.setBlockParams (lowBand, xo, stages, rotF, rotQ);
    effRotFreq.store (rotF);
    effXover.store (xo);

    const float shiftSamples = shiftMs * 0.001f * (float) sampleRateHz;
    const float mixV  = pMix->load();
    const float gainV = juce::Decibels::decibelsToGain (pOut->load());

    if (firstBlock)
    {
        smShift.setCurrentAndTargetValue (shiftSamples);
        smPol.setCurrentAndTargetValue (pol);
        smMix.setCurrentAndTargetValue (mixV);
        smGain.setCurrentAndTargetValue (gainV);
        firstBlock = false;
    }
    else
    {
        smShift.setTargetValue (shiftSamples);
        smPol.setTargetValue (pol);
        smMix.setTargetValue (mixV);
        smGain.setTargetValue (gainV);
    }

    // ---- main loop ---------------------------------------------------------------
    float* data[2] = { nullptr, nullptr };
    for (int c = 0; c < numCh; ++c)
        data[c] = buffer.getWritePointer (c);

    int pushed = 0;
    float kPeak = 0.0f, bPeak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float shiftS = smShift.getNextValue();
        const float polV   = smPol.getNextValue();
        const float mixS   = smMix.getNextValue();
        const float gainS  = smGain.getNextValue();

        float in[2] = { 0.0f, 0.0f };
        float bm = 0.0f;
        for (int c = 0; c < numCh; ++c) { in[c] = data[c][i]; bm += in[c]; }
        bm /= (float) numCh;

        float km = 0.0f;
        if (scCh > 0)
        {
            for (int c = 0; c < scCh; ++c) km += sc[c][i];
            km /= (float) scCh;
        }

        bPeak = juce::jmax (bPeak, std::abs (bm));
        kPeak = juce::jmax (kPeak, std::abs (km));

        // analysis feed (decimated to ~4 kHz)
        const float fb = decBass.process (bm);
        const float fk = decKick.process (km);
        if (++decCounter >= decFactor)
        {
            decCounter = 0;
            if (pushed < kScratch)
            {
                scratchB[(size_t) pushed] = fb;
                scratchK[(size_t) pushed] = fk;
                ++pushed;
            }
        }

        for (int c = 0; c < numCh; ++c)
        {
            float wet = 0.0f, dry = 0.0f;
            engine.process (c, in[c], shiftS, polV, wet, dry);
            data[c][i] = (dry + (wet - dry) * mixS) * gainS;
        }
    }

    if (pushed > 0)
    {
        const juce::SpinLock::ScopedTryLockType tl (ringLock);
        if (tl.isLocked())
        {
            for (int p = 0; p < pushed; ++p)
            {
                const auto idx = (size_t) ((ringWrite + (std::uint64_t) p) % (std::uint64_t) kRing);
                ringBass[idx] = scratchB[(size_t) p];
                ringKick[idx] = scratchK[(size_t) p];
            }
            ringWrite += (std::uint64_t) pushed;
        }
    }

    // ---- UI metering ---------------------------------------------------------------
    const float dk = std::exp (-(float) n / (float) sampleRateHz / 0.12f);
    kickPeak.store (juce::jmax (kPeak, kickPeak.load() * dk));
    bassPeak.store (juce::jmax (bPeak, bassPeak.load() * dk));

    // Raw engine shift, and the displayed shift = what the bass fundamental really moves.
    const float engMs = smShift.getCurrentValue() * 1000.0f / (float) sampleRateHz;
    engineShiftMs.store (engMs);
    appliedShiftMs.store (engMs + compMs);
    appliedPolarity.store (smPol.getCurrentValue());
}

//==============================================================================
//  Background analysis (runs ~25x per second whenever new audio has arrived)
//==============================================================================
void PhaseForgeAudioProcessor::runAnalysisStep()
{
    if (resetAnalyzer.exchange (false))
    {
        analyzer.reset();
        lastAnalysedWrite = 0;
        learnState.store (0);
        learnProgress.store (0.0f);
    }
    if (keyResetRequest.exchange (false))
        analyzer.resetKey();

    if (learnRequest.exchange (false))
    {
        analyzer.startLearn();
        learnState.store (1);
        learnProgress.store (0.0f);
    }

    constexpr int W = kAnalysisWindow;
    std::array<float, W> b {}, k {};
    std::uint64_t wc = 0;
    {
        const juce::SpinLock::ScopedLockType sl (ringLock);
        wc = ringWrite;
        if (wc < (std::uint64_t) W)
            return;
        if (wc < lastAnalysedWrite)
            lastAnalysedWrite = 0;                      // ring was reset (prepareToPlay)
        if (wc < lastAnalysedWrite + 64)
            return;                                     // no meaningful new audio

        for (int i = 0; i < W; ++i)
        {
            const auto idx = (size_t) ((wc - (std::uint64_t) W + (std::uint64_t) i) % (std::uint64_t) kRing);
            b[(size_t) i] = ringBass[idx];
            k[(size_t) i] = ringKick[idx];
        }
    }
    lastAnalysedWrite = wc;

    const double speed = (double) pSpeed->load();
    const double decay = 0.99 - 0.14 * speed;
    const double dsr = decimatedRate.load();

    const auto out = analyzer.analyse (b.data(), k.data(), W, dsr, decay);

    autoValid.store (out.alignValid);
    autoShiftMs.store ((float) out.shiftMs);
    autoPolarity.store (out.polarity);
    autoScore.store ((float) out.score);

    corrBefore.store ((float) out.beforeCorr);
    {
        const double lagDec = (double) appliedShiftMs.load() * 0.001 * dsr;
        const double sign = appliedPolarity.load() >= 0.0f ? 1.0 : -1.0;
        corrAfter.store ((float) (sign * analyzer.getAligner().normalisedAt (lagDec)));
    }

    bassHz.store ((float) out.bassHz);   bassConf.store ((float) out.bassConf);
    kickHz.store ((float) out.kickHz);   kickConf.store ((float) out.kickConf);
    keyValid.store (out.keyValid);
    keyRoot.store (out.keyRoot);
    keyMinor.store (out.keyMinor);
    keyConf.store ((float) out.keyConf);

    {
        const juce::SpinLock::ScopedLockType sl (curveLock);
        analyzer.getAligner().copyCurve (curve.data());
    }
    {
        const juce::SpinLock::ScopedLockType sl (specLock);
        analyzer.getSpectrum().get (specMagB.data(), specMagK.data(), specPhase.data(), specCoh.data());
    }

    learnProgress.store ((float) analyzer.learnProgress());
    const auto lr = analyzer.takeLearnResult();
    if (lr.done)
    {
        learnOk.store (lr.ok);
        learnShiftMs.store ((float) lr.shiftMs);
        learnPol.store (lr.polarity);
        learnState.store (lr.ok ? 2 : 3);
        learnProgress.store (lr.ok ? 1.0f : 0.0f);
        triggerAsyncUpdate();                          // apply on the message thread
    }
}

void PhaseForgeAudioProcessor::handleAsyncUpdate()
{
    if (! learnOk.load())
        return;

    float shift = learnShiftMs.load();
    if (pBand->load() > 0.5f)                          // keep Low Band consistent with Auto
    {
        const float hz = bassHz.load();
        shift -= (float) pf::AlignEngine::lowBandPhaseDelayMs (effXover.load(), hz > 20.0f ? hz : 55.0f);
    }

    setParam ("shift", juce::jlimit (-10.0f, 10.0f, shift));
    setParam ("polarity", learnPol.load() < 0 ? 1.0f : 0.0f);
    setParam ("auto", 0.0f);
}

void PhaseForgeAudioProcessor::setParam (const char* id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

bool PhaseForgeAudioProcessor::getScopeSnapshot (float* bass, float* kick, int n) const
{
    const juce::SpinLock::ScopedLockType sl (ringLock);
    if (n > kRing || ringWrite < (std::uint64_t) n)
        return false;

    for (int i = 0; i < n; ++i)
    {
        const auto idx = (size_t) ((ringWrite - (std::uint64_t) n + (std::uint64_t) i) % (std::uint64_t) kRing);
        bass[i] = ringBass[idx];
        kick[i] = ringKick[idx];
    }
    return true;
}

void PhaseForgeAudioProcessor::getCurve (float* out) const
{
    const juce::SpinLock::ScopedLockType sl (curveLock);
    for (int i = 0; i < pf::CorrAligner::kNumLags; ++i)
        out[i] = curve[(size_t) i];
}

void PhaseForgeAudioProcessor::getSpectrum (float* magBass, float* magKick, float* phaseRad, float* coherence) const
{
    const juce::SpinLock::ScopedLockType sl (specLock);
    for (int i = 0; i < kSpecBins; ++i)
    {
        magBass[i]   = specMagB[(size_t) i];
        magKick[i]   = specMagK[(size_t) i];
        phaseRad[i]  = specPhase[(size_t) i];
        coherence[i] = specCoh[(size_t) i];
    }
}

//==============================================================================
int PhaseForgeAudioProcessor::getNumPrograms() { return kNumPresets; }

const juce::String PhaseForgeAudioProcessor::getProgramName (int index)
{
    return (index >= 0 && index < kNumPresets) ? juce::String (kPresets[index].name) : juce::String();
}

void PhaseForgeAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= kNumPresets)
        return;

    currentProgram = index;
    const auto& p = kPresets[index];
    setParam ("shift",     p.shift);
    setParam ("polarity",  p.pol ? 1.0f : 0.0f);
    setParam ("auto",      p.autoOn ? 1.0f : 0.0f);
    setParam ("speed",     p.speed);
    setParam ("rotFreq",   p.rotFreq);
    setParam ("rotStages", (float) p.stages);
    setParam ("rotQ",      p.rotQ);
    setParam ("band",      (float) p.band);
    setParam ("xover",     p.xover);
    setParam ("follow",    p.follow ? 1.0f : 0.0f);
    setParam ("mix",       p.mix);
    setParam ("out",       p.out);
}

//==============================================================================
void PhaseForgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PhaseForgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* PhaseForgeAudioProcessor::createEditor()
{
    return new PhaseForgeAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhaseForgeAudioProcessor();
}
