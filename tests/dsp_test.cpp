// Standalone tests for PhaseForge's DSP core. No JUCE required:
//   g++ -std=c++17 -O2 -Wall -Wextra tests/dsp_test.cpp -o dsp_test && ./dsp_test
#include "../Source/DspCore.h"

#include <complex>
#include <cstdio>
#include <cstdlib>

using namespace pf;

static int failures = 0;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (cond) { std::printf ("  PASS  "); }                                \
        else      { std::printf ("  FAIL  "); ++failures; }                    \
        std::printf (__VA_ARGS__);                                             \
        std::printf ("\n");                                                    \
    } while (0)

static double ncorr (const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to)
{
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = from; i < to; ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
    return ab / (std::sqrt (aa * bb) + 1e-20);
}

// ---------------------------------------------------------------------------
static void testDelay()
{
    std::printf ("[FracDelay]\n");
    FracDelay d; d.prepare (2000);
    const double sr = 48000.0, f = 100.0;
    const float delay = 123.37f;
    double maxErr = 0.0;
    for (int n = 0; n < 6000; ++n)
    {
        d.push ((float) std::sin (2 * kPi * f * n / sr));
        if (n > 500)
        {
            const double expect = std::sin (2 * kPi * f * ((double) n - delay) / sr);
            maxErr = std::max (maxErr, std::abs (expect - (double) d.read (delay)));
        }
    }
    CHECK (maxErr < 1e-4, "fractional delay of a 100 Hz sine, max error = %.2e", maxErr);
}

// ---------------------------------------------------------------------------
static std::vector<float> synthTone (double f, int n, double sr, double harm2 = 0.3)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = (float) (0.5 * std::sin (2 * kPi * f * i / sr) + 0.5 * harm2 * std::sin (4 * kPi * f * i / sr));
    return v;
}

static void testPitch()
{
    std::printf ("[YIN pitch]\n");
    const double dsr = 4000.0;
    std::vector<double> tmp;
    for (double f : { 36.7, 41.2, 55.0, 73.4, 98.0, 130.8 })
    {
        auto x = synthTone (f, 1024, dsr);
        auto r = detectPitchYin (x.data(), 1024, dsr, 27.5, 330.0, tmp);
        const double cents = 1200.0 * std::log2 (r.freq / f);
        CHECK (std::abs (cents) < 15.0 && r.conf > 0.8, "%.1f Hz -> %.2f Hz (%.1f cents, conf %.2f)", f, r.freq, cents, r.conf);
    }
    std::vector<float> silent (1024, 0.0f);
    auto r = detectPitchYin (silent.data(), 1024, dsr, 27.5, 330.0, tmp);
    CHECK (r.conf == 0.0, "silence gives no pitch");
}

// ---------------------------------------------------------------------------
static void testKey()
{
    std::printf ("[Key estimator]\n");
    {
        KeyEstimator k;   // G minor bass line: G G Bb C D F Eb G, root heavy
        const int pcs[] = { 7, 7, 10, 0, 2, 5, 3, 7, 7, 7 };
        for (int rep = 0; rep < 12; ++rep)
            for (int pc : pcs) k.addNote (36 + pc, 1.0);
        auto r = k.estimate();
        CHECK (r.valid && r.root == 7 && r.minor, "G minor line -> %s %s (conf %.2f)", noteName (r.root), r.minor ? "minor" : "major", r.conf);
    }
    {
        KeyEstimator k;   // F minor: F F Ab Bb C Db Eb F
        const int pcs[] = { 5, 5, 8, 10, 0, 1, 3, 5, 5, 5 };
        for (int rep = 0; rep < 12; ++rep)
            for (int pc : pcs) k.addNote (36 + pc, 1.0);
        auto r = k.estimate();
        CHECK (r.valid && r.root == 5 && r.minor, "F minor line -> %s %s", noteName (r.root), r.minor ? "minor" : "major");
    }
    {
        KeyEstimator k;   // not enough data -> invalid
        k.addNote (43, 1.0);
        CHECK (! k.estimate().valid, "too little data gives no key");
    }
    {
        // 51 Hz is ~ +70 cents above G1 (49 Hz) -> need to tune DOWN by ~0.7 semitones
        const double d = semitonesToPitchClass (freqToMidi (51.0), 7);
        CHECK (d < 0.0 && d > -1.0, "kick at 51 Hz is above G: tune %.2f semitones", d);
        double delta = 0.0;
        const int pc = nearestInKey (freqToMidi (51.0), 7, true, delta);
        CHECK (pc == 7, "nearest in-key note to 51 Hz in G minor is %s", noteName (pc));
    }
}

// ---------------------------------------------------------------------------
//  The visualizer predicts the result of the current settings with an analytic
//  formula. Prove it matches what the audio engine really does.
// ---------------------------------------------------------------------------
static void testResponse()
{
    std::printf ("[Analytic response vs real engine]\n");
    struct Cfg { bool low; double xo; int st; double rf, rq, shiftMs, pol; };
    const Cfg cfgs[] = {
        { false, 120, 0, 60, 0.7,  2.5, +1 },
        { false, 120, 2, 70, 0.9, -1.5, -1 },
        { true,  150, 1, 60, 0.8,  1.0, +1 },
        { true,  200, 3, 80, 1.5, -2.0, -1 },
    };
    const double sr = 48000.0;
    for (const auto& c : cfgs)
        for (double f : { 45.0, 60.0, 95.0, 160.0 })
        {
            AlignEngine eng; eng.prepare (sr);
            eng.setBlockParams (c.low, c.xo, c.st, c.rf, c.rq);
            const int cycles = 24;
            const int warm = (int) (sr * 1.0);
            const int meas = (int) std::lround ((double) cycles * sr / f);
            std::complex<double> aw (0, 0), ad (0, 0);
            for (int i = 0; i < warm + meas; ++i)
            {
                const float x = (float) (0.5 * std::sin (2 * kPi * f * i / sr));
                float w, d;
                eng.process (0, x, (float) (c.shiftMs * 0.001 * sr), (float) c.pol, w, d);
                if (i >= warm)
                {
                    const auto e = std::polar (1.0, -2 * kPi * f * i / sr);
                    aw += (double) w * e;  ad += (double) d * e;
                }
            }
            const auto measured = aw / ad;
            const auto predicted = AlignEngine::responseAt (f, c.shiftMs, c.pol, c.low, c.xo, c.st, c.rf, c.rq);
            double dPhase = std::arg (measured) - std::arg (predicted);
            while (dPhase >  kPi) dPhase -= 2 * kPi;
            while (dPhase < -kPi) dPhase += 2 * kPi;
            const double magErr = std::abs (std::abs (measured) / std::abs (predicted) - 1.0);
            CHECK (std::abs (dPhase) * 180 / kPi < 1.5 && magErr < 0.02,
                   "%s xo=%.0f st=%d @%.0f Hz: phase err %.2f deg, mag err %.2f%%",
                   c.low ? "low " : "full", c.xo, c.st, f, dPhase * 180 / kPi, magErr * 100);
        }
}

// ---------------------------------------------------------------------------
//  The overlap view draws "bass after PhaseForge" with renderPreview(). Prove it
//  matches the real 48 kHz audio engine (after the same decimation).
// ---------------------------------------------------------------------------
static void testPreview()
{
    std::printf ("[Preview renderer vs real engine]\n");
    struct Cfg { const char* name; bool low; double xo; int st; double rf, rq, shiftMs, pol, mix; };
    const Cfg cfgs[] = {
        { "full, +2.5 ms, flipped",          false, 120, 0, 60, 0.7,  2.5, -1, 1.0 },
        { "full, -1.2 ms, 2 rotate stages",  false, 120, 2, 70, 0.9, -1.2, +1, 1.0 },
        { "low band 150 Hz, -3 ms, 1 stage", true,  150, 1, 60, 0.8, -3.0, +1, 1.0 },
        { "full, +1 ms, 50% mix",            false, 120, 0, 60, 0.7,  1.0, +1, 0.5 },
    };

    const double sr = 48000.0;
    const int D = decimationFactor (sr);
    const double dsr = sr / D;
    const int N = (int) (sr * 6.0);

    std::vector<float> x ((size_t) N);                         // a bass line: 49/55 Hz notes with decay
    for (int i = 0; i < N; ++i)
    {
        const double t = (double) i / sr;
        const int k = (int) std::floor (t / 0.25);
        const double tn = t - (double) k * 0.25;
        const double f = (k % 2 == 0) ? 49.0 : 55.0;
        const double env = std::exp (-tn / 0.12);
        x[(size_t) i] = (float) (0.6 * env * std::sin (2 * kPi * f * t) + 0.25 * env * std::sin (4 * kPi * f * t));
    }

    DecimatorLP dIn; dIn.prepare (sr);
    std::vector<float> xd;
    for (int i = 0; i < N; ++i)
    {
        const float f = dIn.process (x[(size_t) i]);
        if ((i + 1) % D == 0) xd.push_back (f);
    }

    for (const auto& c : cfgs)
    {
        AlignEngine eng; eng.prepare (sr);
        eng.setBlockParams (c.low, c.xo, c.st, c.rf, c.rq);
        const int Ld = eng.getLatency() / D;                   // 480 / 12 = 40 decimated samples

        DecimatorLP dOut; dOut.prepare (sr);
        std::vector<float> yd;
        for (int i = 0; i < N; ++i)
        {
            float w, d;
            eng.process (0, x[(size_t) i], (float) (c.shiftMs * 0.001 * sr), (float) c.pol, w, d);
            const float o = d + (w - d) * (float) c.mix;
            const float f = dOut.process (o);
            if ((i + 1) % D == 0) yd.push_back (f);
        }

        PreviewParams pp;
        pp.lowBand = c.low; pp.xoverHz = c.xo; pp.stages = c.st; pp.rotFreqHz = c.rf; pp.rotQ = c.rq;
        pp.shiftSamples = c.shiftMs * 0.001 * dsr; pp.polarity = c.pol; pp.mix = c.mix;

        const int n = (int) xd.size();
        std::vector<float> pv ((size_t) n);
        PreviewScratch sc;
        renderPreview (xd.data(), pv.data(), n, dsr, pp, sc);

        double se = 0, ss = 0;
        for (int j = 1000; j < n - Ld - 50; ++j)
        {
            const double a = pv[(size_t) j], b = yd[(size_t) (j + Ld)];
            se += (a - b) * (a - b);
            ss += b * b;
        }
        const double rel = std::sqrt (se / (ss + 1e-20));
        CHECK (rel < 0.05, "%-34s relative error %.2f%%", c.name, rel * 100.0);
    }
}

static void testPhaseSpectrum()
{
    std::printf ("[PhaseSpectrum]\n");
    const double dsr = 4000.0;
    const int N = PhaseSpectrum::kN;
    const double f0 = 50.0, lagMs = 4.0;
    std::vector<float> b ((size_t) N), k ((size_t) N);
    for (int i = 0; i < N; ++i)
    {
        const double t = (double) i / dsr;
        k[(size_t) i] = (float) (0.8 * std::exp (-t / 0.2) * std::sin (2 * kPi * f0 * t));
        b[(size_t) i] = (float) (0.5 * std::sin (2 * kPi * f0 * (t - lagMs * 0.001)));      // bass is 4 ms late
    }
    PhaseSpectrum ps;
    for (int i = 0; i < 5; ++i) ps.update (b.data(), k.data(), 0.9);
    std::vector<float> mb (PhaseSpectrum::kBins), mk (PhaseSpectrum::kBins), ph (PhaseSpectrum::kBins), co (PhaseSpectrum::kBins);
    ps.get (mb.data(), mk.data(), ph.data(), co.data());

    int best = 0;
    for (int i = 0; i < PhaseSpectrum::kBins; ++i)
        if (mb[(size_t) i] * mk[(size_t) i] > mb[(size_t) best] * mk[(size_t) best]) best = i;
    const double fb = PhaseSpectrum::binFreq (best, dsr);
    const double expected = -2 * kPi * fb * lagMs * 0.001 * 180 / kPi;     // bass lags -> negative phase
    const double got = ph[(size_t) best] * 180 / kPi;
    CHECK (std::abs (fb - f0) < 4.0, "strongest bin is %.1f Hz", fb);
    CHECK (std::abs (got - expected) < 12.0, "bass-vs-kick phase at %.1f Hz = %.1f deg (expected %.1f)", fb, got, expected);
    CHECK (co[(size_t) best] > 0.8, "coherence at the peak = %.2f", co[(size_t) best]);

    // the analytic 'after' phase with the shift the analyzer would pick (-4 ms) must be ~0
    const auto g = AlignEngine::responseAt (fb, -lagMs, +1, false, 120, 0, 60, 0.7);
    double after = got + std::arg (g) * 180 / kPi;
    while (after > 180) after -= 360;
    while (after < -180) after += 360;
    CHECK (std::abs (after) < 12.0, "predicted phase AFTER alignment = %.1f deg", after);
}

// ---------------------------------------------------------------------------
//  Full pipeline at 48 kHz: synthetic kick + bass with a known offset
//  -> decimate -> Analyzer -> AlignEngine -> verify alignment really improved.
// ---------------------------------------------------------------------------
struct Scenario
{
    const char* name;
    double bassLagMs;      // bass(t) = tone(t - lag): bass arrives this much LATER than the kick
    bool   bassInverted;
    double expectShiftMs;  // what the analyser should tell the plugin to apply
    int    expectSign;
    bool   lowBand;
};

static void runScenario (const Scenario& sc)
{
    std::printf ("[Pipeline] %s\n", sc.name);
    const double sr = 48000.0;
    const int N = (int) (sr * 12.0);
    const double f0 = 50.0;

    std::vector<float> kick ((size_t) N), bass ((size_t) N);
    const double period = 60.0 / 128.0;                       // 128 BPM
    for (int i = 0; i < N; ++i)
    {
        const double t = (double) i / sr;
        const double tk = std::fmod (t, period);
        const double env = std::exp (-tk / 0.15);
        kick[(size_t) i] = (float) (0.9 * env * std::sin (2 * kPi * f0 * t));

        const double tb = t - sc.bassLagMs * 0.001;
        double b = 0.6 * std::sin (2 * kPi * f0 * tb) + 0.2 * std::sin (2 * kPi * 2 * f0 * tb);
        bass[(size_t) i] = (float) (sc.bassInverted ? -b : b);
    }

    // ---- analysis path exactly as the plugin does it -------------------------
    const int D = decimationFactor (sr);
    const double dsr = sr / D;
    DecimatorLP dB, dK; dB.prepare (sr); dK.prepare (sr);
    std::vector<float> db, dk;
    for (int i = 0; i < N; ++i)
    {
        const float b = dB.process (bass[(size_t) i]);
        const float k = dK.process (kick[(size_t) i]);
        if ((i + 1) % D == 0) { db.push_back (b); dk.push_back (k); }
    }

    Analyzer an;
    Analyzer::Output out;
    const int W = PhaseSpectrum::kN, hop = 100;
    for (size_t end = (size_t) W; end <= db.size(); end += (size_t) hop)
        out = an.analyse (db.data() + end - W, dk.data() + end - W, W, dsr, 0.93);

    CHECK (out.alignValid, "alignment found (score %.2f)", out.score);
    CHECK (std::abs (out.shiftMs - sc.expectShiftMs) < 0.35, "shift = %+.2f ms (expected %+.2f)", out.shiftMs, sc.expectShiftMs);
    CHECK (out.polarity == sc.expectSign, "polarity sign = %+d (expected %+d)", out.polarity, sc.expectSign);
    CHECK (std::abs (out.bassHz - f0) < 1.5, "bass pitch = %.2f Hz (conf %.2f)", out.bassHz, out.bassConf);
    CHECK (std::abs (out.kickHz - f0) < 1.5, "kick pitch = %.2f Hz (conf %.2f)", out.kickHz, out.kickConf);

    // ---- apply through the audio engine and measure ---------------------------
    AlignEngine eng; eng.prepare (sr);
    eng.setBlockParams (sc.lowBand, 150.0, 0, 60.0, 0.7);
    const int L = eng.getLatency();
    // In Low Band mode the crossover itself delays the bass; Auto-Align removes that (see processor).
    double appliedMs = out.shiftMs;
    if (sc.lowBand) appliedMs -= AlignEngine::lowBandPhaseDelayMs (150.0, out.bassHz);
    std::printf ("         applied engine shift: %+.2f ms\n", appliedMs);
    std::vector<float> wet ((size_t) N), dry ((size_t) N), kickD ((size_t) N, 0.0f);
    for (int i = 0; i < N; ++i)
    {
        float w, d;
        eng.process (0, bass[(size_t) i], (float) (appliedMs * 0.001 * sr), (float) out.polarity, w, d);
        wet[(size_t) i] = w; dry[(size_t) i] = d;
        if (i >= L) kickD[(size_t) i] = kick[(size_t) (i - L)];   // the host delays the kick by L (PDC)
    }
    const size_t from = (size_t) (sr * 6.0), to = (size_t) N;
    const double before = ncorr (dry, kickD, from, to);
    const double after  = ncorr (wet, kickD, from, to);
    std::printf ("         correlation with kick: before %.3f  ->  after %.3f\n", before, after);

    // Ceiling: what a perfectly aligned bass would score (kick tail envelope limits it below 1.0).
    std::vector<float> ideal ((size_t) N);
    for (int i = 0; i < N; ++i)
    {
        const double t = (double) i / sr;
        ideal[(size_t) i] = (float) (0.6 * std::sin (2 * kPi * f0 * t) + 0.2 * std::sin (2 * kPi * 2 * f0 * t));
    }
    const double ceiling = ncorr (ideal, kick, from, to);
    CHECK (after > 0.97 * ceiling, "aligned bass reaches %.1f%% of the theoretical maximum (%.3f of %.3f)", 100.0 * after / ceiling, after, ceiling);
    CHECK (after > before + 0.05 || before > 0.97 * ceiling, "alignment improved the correlation");
    CHECK (std::abs (out.beforeCorr - ncorr (bass, kick, from, to)) < 0.25, "'before' meter (%.2f) agrees with direct measurement (%.2f)",
           out.beforeCorr, ncorr (bass, kick, from, to));

    // ---- the Phase Lab's prediction must agree with reality ----------------------
    std::vector<float> mb (PhaseSpectrum::kBins), mk (PhaseSpectrum::kBins), ph (PhaseSpectrum::kBins), co (PhaseSpectrum::kBins);
    an.getSpectrum().get (mb.data(), mk.data(), ph.data(), co.data());
    int best = 0;
    for (int i = 0; i < PhaseSpectrum::kBins; ++i)
        if (mb[(size_t) i] * mk[(size_t) i] > mb[(size_t) best] * mk[(size_t) best]) best = i;
    const double fb = PhaseSpectrum::binFreq (best, dsr);
    const auto g = AlignEngine::responseAt (fb, appliedMs, (double) out.polarity, sc.lowBand, 150.0, 0, 60.0, 0.7);
    double afterDeg = (double) ph[(size_t) best] * 180.0 / kPi + std::arg (g) * 180.0 / kPi;
    while (afterDeg >  180.0) afterDeg -= 360.0;
    while (afterDeg < -180.0) afterDeg += 360.0;
    CHECK (std::abs (afterDeg) < 25.0, "Phase Lab predicts residual phase error %.1f deg at %.1f Hz", afterDeg, fb);
}

int main()
{
    testDelay();
    testPitch();
    testKey();
    testResponse();
    testPreview();
    testPhaseSpectrum();

    //            name                              lagMs  inv    expectShift sign lowBand
    runScenario ({ "bass 4 ms late (move earlier)",   4.0, false,  -4.0,  +1, false });
    runScenario ({ "bass 2 ms early (move later)",   -2.0, false,  +2.0,  +1, false });
    runScenario ({ "bass inverted, in time",          0.0, true,    0.0,  -1, false });
    runScenario ({ "bass 4 ms late, low-band mode",   4.0, false,  -4.0,  +1, true  });

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
