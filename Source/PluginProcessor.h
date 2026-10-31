#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "DspCore.h"

//==============================================================================
//  PhaseForge
//  Put it on your BASS (or any element), route the KICK into its sidechain input.
//  It measures the phase relationship in the low end and lines the two up.
//==============================================================================
class PhaseForgeAudioProcessor : public juce::AudioProcessor,
                                 private juce::AsyncUpdater
{
public:
    static constexpr int kRing = 4096;            // decimated analysis ring buffer (~1 s)
    static constexpr int kScratch = 4096;         // max decimated samples we accept per block
    static constexpr int kAnalysisWindow = pf::PhaseSpectrum::kN;   // 1024 decimated samples (~256 ms)
    static constexpr int kSpecBins = pf::PhaseSpectrum::kBins;

    PhaseForgeAudioProcessor();
    ~PhaseForgeAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PhaseForge"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    // ---- live data for the UI (all thread-safe) -------------------------------
    std::atomic<bool>  scConnected { false };
    std::atomic<float> kickPeak { 0.f }, bassPeak { 0.f };
    std::atomic<float> appliedShiftMs { 0.f };      // how far the bass FUNDAMENTAL really moves (incl. crossover delay)
    std::atomic<float> engineShiftMs { 0.f };       // the raw delay-line shift (what AlignEngine::responseAt wants)
    std::atomic<float> appliedPolarity { 1.f };
    std::atomic<float> effRotFreq { 60.f }, effXover { 120.f };
    std::atomic<float> lowBandCompMs { 0.f };

    std::atomic<bool>  autoValid { false };
    std::atomic<float> autoShiftMs { 0.f }, autoScore { 0.f };
    std::atomic<int>   autoPolarity { 1 };
    std::atomic<float> corrBefore { 0.f }, corrAfter { 0.f };

    std::atomic<float> bassHz { 0.f }, bassConf { 0.f }, kickHz { 0.f }, kickConf { 0.f };
    std::atomic<bool>  keyValid { false };
    std::atomic<int>   keyRoot { 0 };
    std::atomic<bool>  keyMinor { true };
    std::atomic<float> keyConf { 0.f };

    std::atomic<int>   learnState { 0 };   // 0 idle, 1 learning, 2 done ok, 3 failed
    std::atomic<float> learnProgress { 0.f };
    std::atomic<double> decimatedRate { 4000.0 };

    void startLearn() { learnRequest.store (true); }
    void resetKey()   { keyResetRequest.store (true); }

    // Copies the newest `n` decimated samples (oldest first). Returns false if not enough data yet.
    bool getScopeSnapshot (float* bass, float* kick, int n) const;
    void getCurve (float* out /* CorrAligner::kNumLags */) const;
    // Per-frequency bass/kick spectrum + phase (each array holds kSpecBins values).
    void getSpectrum (float* magBass, float* magKick, float* phaseRad, float* coherence) const;

private:
    class AnalysisThread;
    friend class AnalysisThread;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void runAnalysisStep();
    void handleAsyncUpdate() override;
    void setParam (const char* id, float plainValue);

    // ---- DSP ------------------------------------------------------------------
    pf::AlignEngine engine;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smShift, smPol, smMix, smGain;
    bool firstBlock = true;
    double sampleRateHz = 44100.0;
    float followFreq = 0.f;

    // analysis feed (audio thread -> background thread)
    pf::DecimatorLP decBass, decKick;
    int decFactor = 12, decCounter = 0;
    std::array<float, kScratch> scratchB {}, scratchK {};
    std::array<float, kRing> ringBass {}, ringKick {};
    std::uint64_t ringWrite = 0;
    mutable juce::SpinLock ringLock;

    // analysis results shared with the UI
    pf::Analyzer analyzer;
    std::uint64_t lastAnalysedWrite = 0;
    mutable juce::SpinLock curveLock;
    std::array<float, pf::CorrAligner::kNumLags> curve {};
    mutable juce::SpinLock specLock;
    std::array<float, kSpecBins> specMagB {}, specMagK {}, specPhase {}, specCoh {};
    std::atomic<bool> resetAnalyzer { true }, learnRequest { false }, keyResetRequest { false };
    std::atomic<float> learnShiftMs { 0.f };
    std::atomic<int>   learnPol { 1 };
    std::atomic<bool>  learnOk { false };

    std::unique_ptr<AnalysisThread> analysisThread;

    // cached parameter pointers
    std::atomic<float> *pShift, *pPolarity, *pAuto, *pSpeed, *pRotFreq, *pRotStages, *pRotQ,
                       *pBand, *pXover, *pFollow, *pMix, *pOut;

    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaseForgeAudioProcessor)
};
