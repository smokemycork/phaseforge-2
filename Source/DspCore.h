// =============================================================================
//  PhaseForge - DspCore.h
//  JUCE-free DSP building blocks (so they can be unit-tested without a DAW).
//  The audio-thread parts (AlignEngine, FracDelay, Biquad) never allocate after
//  prepare(). The Analyzer allocates a little, but only runs on a background thread.
// =============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace pf
{
constexpr double kPi = 3.14159265358979323846;

//==============================================================================
//  Biquad (RBJ cookbook, transposed direct form II)
//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0.0; }

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setLowpass  (double fs, double f, double q) noexcept
    {
        double c, al; prep (fs, f, q, c, al);
        set ((1 - c) * 0.5, 1 - c, (1 - c) * 0.5, 1 + al, -2 * c, 1 - al);
    }

    void setHighpass (double fs, double f, double q) noexcept
    {
        double c, al; prep (fs, f, q, c, al);
        set ((1 + c) * 0.5, -(1 + c), (1 + c) * 0.5, 1 + al, -2 * c, 1 - al);
    }

    void setAllpass  (double fs, double f, double q) noexcept
    {
        double c, al; prep (fs, f, q, c, al);
        set (1 - al, -2 * c, 1 + al, 1 + al, -2 * c, 1 - al);
    }

private:
    static void prep (double fs, double f, double q, double& c, double& al) noexcept
    {
        f = std::min (std::max (f, 1.0), fs * 0.45);
        q = std::max (q, 0.05);
        const double w0 = 2.0 * kPi * f / fs;
        c  = std::cos (w0);
        al = std::sin (w0) / (2.0 * q);
    }

    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
    {
        const double inv = 1.0 / a0;
        b0 = nb0 * inv; b1 = nb1 * inv; b2 = nb2 * inv;
        a1 = na1 * inv; a2 = na2 * inv;
    }
};

//==============================================================================
//  Fractional delay line (4-point Hermite). Delay is measured in samples
//  relative to the most recently pushed sample: push(x) then read(d) with d >= 1.
//==============================================================================
class FracDelay
{
public:
    void prepare (int maxDelaySamples)
    {
        const int need = maxDelaySamples + 8;
        size = 1;
        while (size < need) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }

    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }

    inline void push (float x) noexcept
    {
        buf[(size_t) w] = x;
        w = (w + 1) & mask;
    }

    inline float read (float d) const noexcept
    {
        d = std::min (std::max (d, 1.0f), (float) (size - 4));
        const int   i = (int) d;
        const float f = d - (float) i;

        const float ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2);
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

private:
    inline float at (int k) const noexcept   // k samples ago (0 = newest)
    {
        return buf[(size_t) (((unsigned) (w - 1 - k)) & (unsigned) mask)];
    }

    std::vector<float> buf;
    int size = 0, mask = 0, w = 0;
};

//==============================================================================
//  AlignEngine - the actual audio path.
//
//  The engine always adds a fixed latency L (= 10 ms). A "shift" of s samples
//  means the signal is delayed by (L + s) samples, so relative to the rest of
//  the project (which the host delays by L) the signal moves by s samples:
//  s < 0 = earlier, s > 0 = later. Range: roughly -10 ms .. +10 ms.
//
//  Signal flow per channel:
//     in --+--> [dry delay L] ----------------------------------------> dry
//          |
//          +--> (Full)      lo = in                      hi = 0
//               (Low Band)  lo = LR4 lowpass(in)         hi = LR4 highpass(in)
//          lo --> [delay L+s] --> [N x 2nd-order allpass] --> x polarity --+
//          hi --> [delay L] ---------------------------------------------- + --> wet
//==============================================================================
class AlignEngine
{
public:
    static constexpr int kMaxCh = 2;
    static constexpr int kMaxStages = 4;
    static constexpr double kMaxShiftMs = 10.0;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        latency = (int) std::lround (kMaxShiftMs * 0.001 * sr);
        for (int c = 0; c < kMaxCh; ++c)
        {
            delLo[c].prepare (2 * latency + 4);
            delHi[c].prepare (latency + 4);
            delDry[c].prepare (latency + 4);
        }
        reset();
        setBlockParams (false, 120.0, 0, 60.0, 0.7);
    }

    int    getLatency() const noexcept { return latency; }
    double getSampleRate() const noexcept { return sr; }

    // Phase delay (ms) that the LR4 crossover's low band adds to a tone at `freqHz`.
    // In Low Band mode the bass is already delayed by this much before any shift is applied,
    // so Auto-Align subtracts it to hit the true target.
    static double lowBandPhaseDelayMs (double xoverHz, double freqHz) noexcept
    {
        const double om = std::max (freqHz, 1.0) / std::max (xoverHz, 1.0);
        const double phase = 2.0 * std::atan2 (1.41421356237 * om, 1.0 - om * om);   // LR4 = 2 x Butterworth-2
        return phase / (2.0 * kPi * std::max (freqHz, 1.0)) * 1000.0;
    }

    // Exact complex gain of the wet path relative to the (latency-delayed) dry signal at `freqHz`.
    // shiftMs is the ENGINE shift (the value handed to process(), converted to ms).
    // The visualizer uses this to PREDICT what the current settings do to the phase.
    static std::complex<double> responseAt (double freqHz, double shiftMs, double polarity, bool lowBand,
                                            double xoverHz, int numStages, double rotFreqHz, double rotQ) noexcept
    {
        using C = std::complex<double>;
        const double w = 2.0 * kPi * freqHz;
        C d = std::polar (1.0, -w * shiftMs * 0.001) * polarity;

        const double wr = freqHz / std::max (rotFreqHz, 1.0);
        const double apPhase = -2.0 * std::atan2 (wr / std::max (rotQ, 0.05), 1.0 - wr * wr);
        for (int i = 0; i < std::min (std::max (numStages, 0), kMaxStages); ++i)
            d *= std::polar (1.0, apPhase);

        if (! lowBand)
            return d;

        const double om = freqHz / std::max (xoverHz, 1.0);
        const C jw (0.0, om);
        const C den = jw * jw + 1.41421356237 * jw + 1.0;
        const C lp = 1.0 / den, hp = (jw * jw) / den;
        return d * lp * lp + hp * hp;
    }

    void reset()
    {
        for (int c = 0; c < kMaxCh; ++c)
        {
            delLo[c].reset(); delHi[c].reset(); delDry[c].reset();
            lp1[c].reset(); lp2[c].reset(); hp1[c].reset(); hp2[c].reset();
            for (auto& a : ap[c]) a.reset();
        }
    }

    void setBlockParams (bool lowBand, double xoverHz, int numStages, double rotFreqHz, double rotQ)
    {
        lowBandMode = lowBand;
        stages = std::min (std::max (numStages, 0), kMaxStages);
        for (int c = 0; c < kMaxCh; ++c)
        {
            lp1[c].setLowpass  (sr, xoverHz, 0.70710678);
            lp2[c].setLowpass  (sr, xoverHz, 0.70710678);
            hp1[c].setHighpass (sr, xoverHz, 0.70710678);
            hp2[c].setHighpass (sr, xoverHz, 0.70710678);
            for (auto& a : ap[c]) a.setAllpass (sr, rotFreqHz, rotQ);
        }
    }

    inline void process (int ch, float in, float shiftSamples, float polarity,
                         float& wetOut, float& dryOut) noexcept
    {
        delDry[ch].push (in);
        dryOut = delDry[ch].read ((float) latency);

        const double l = lp2[ch].process (lp1[ch].process ((double) in));
        const double h = hp2[ch].process (hp1[ch].process ((double) in));

        delLo[ch].push (lowBandMode ? (float) l : in);
        delHi[ch].push ((float) h);

        double d = (double) delLo[ch].read ((float) latency + shiftSamples);
        for (int s = 0; s < stages; ++s)
            d = ap[ch][(size_t) s].process (d);

        float w = (float) d * polarity;
        if (lowBandMode)
            w += delHi[ch].read ((float) latency);

        wetOut = w;
    }

private:
    double sr = 48000.0;
    int latency = 480;
    bool lowBandMode = false;
    int stages = 0;

    FracDelay delLo[kMaxCh], delHi[kMaxCh], delDry[kMaxCh];
    Biquad lp1[kMaxCh], lp2[kMaxCh], hp1[kMaxCh], hp2[kMaxCh];
    std::array<Biquad, kMaxStages> ap[kMaxCh];
};

//==============================================================================
//  Decimation helper for the analysis path (4th-order lowpass @ 400 Hz, then
//  keep every D-th sample so analysis runs at ~4 kHz).
//==============================================================================
inline int decimationFactor (double sr) noexcept
{
    return std::max (1, (int) std::lround (sr / 4000.0));
}

struct DecimatorLP
{
    Biquad a, b;
    void prepare (double sr)
    {
        a.setLowpass (sr, 400.0, 0.70710678);
        b.setLowpass (sr, 400.0, 0.70710678);
        a.reset(); b.reset();
    }
    inline float process (float x) noexcept { return (float) b.process (a.process ((double) x)); }
};

//==============================================================================
//  Music helpers
//==============================================================================
inline double freqToMidi (double f) noexcept { return 69.0 + 12.0 * std::log2 (f / 440.0); }
inline double midiToFreq (double m) noexcept { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

inline const char* noteName (int pc) noexcept
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pc % 12) + 12) % 12];
}

inline int midiOctave (int midiNote) noexcept
{
    return (int) std::floor ((double) midiNote / 12.0) - 1;
}

// Signed semitone distance from `midi` to the nearest note whose pitch class is `pc` (range -6..+6).
inline double semitonesToPitchClass (double midi, int pc) noexcept
{
    double d = std::fmod ((double) pc - midi, 12.0);
    if (d >  6.0) d -= 12.0;
    if (d < -6.0) d += 12.0;
    return d;
}

inline bool isInScale (int pc, int root, bool minor) noexcept
{
    static const bool maj[12] = { 1,0,1,0,1,1,0,1,0,1,0,1 };
    static const bool min[12] = { 1,0,1,1,0,1,0,1,1,0,1,0 };
    const int rel = (((pc - root) % 12) + 12) % 12;
    return minor ? min[rel] : maj[rel];
}

// Nearest in-key pitch class to `midi`. Returns pitch class, writes semitone delta.
inline int nearestInKey (double midi, int root, bool minor, double& deltaSemis) noexcept
{
    int best = root; double bestAbs = 1e9; deltaSemis = 0.0;
    for (int pc = 0; pc < 12; ++pc)
    {
        if (! isInScale (pc, root, minor)) continue;
        const double d = semitonesToPitchClass (midi, pc);
        if (std::abs (d) < bestAbs) { bestAbs = std::abs (d); best = pc; deltaSemis = d; }
    }
    return best;
}

//==============================================================================
//  YIN pitch detector
//==============================================================================
struct PitchResult { double freq = 0.0; double conf = 0.0; };

inline PitchResult detectPitchYin (const float* x, int n, double sr, double fmin, double fmax,
                                   std::vector<double>& tmp)
{
    PitchResult r;
    const int tauMax = std::min ((int) (sr / fmin) + 1, n / 2 - 1);
    const int tauMin = std::max (2, (int) (sr / fmax));
    const int W = n - tauMax;
    if (tauMax <= tauMin + 2 || W < 32) return r;

    double energy = 0.0;
    for (int j = 0; j < n; ++j) energy += (double) x[j] * (double) x[j];
    if (energy / (double) n < 1e-9) return r;

    tmp.assign ((size_t) tauMax + 2, 0.0);
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double d = 0.0;
        for (int j = 0; j < W; ++j)
        {
            const double diff = (double) x[j] - (double) x[j + tau];
            d += diff * diff;
        }
        tmp[(size_t) tau] = d;
    }

    double running = 0.0;
    tmp[0] = 1.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        const double raw = tmp[(size_t) tau];
        running += raw;
        tmp[(size_t) tau] = running > 0.0 ? raw * (double) tau / running : 1.0;
    }

    int est = -1;
    const double thr = 0.15;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (tmp[(size_t) tau] < thr)
        {
            while (tau + 1 < tauMax && tmp[(size_t) tau + 1] < tmp[(size_t) tau]) ++tau;
            est = tau;
            break;
        }
    }
    if (est < 0)
    {
        double mn = 1e9;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (tmp[(size_t) tau] < mn) { mn = tmp[(size_t) tau]; est = tau; }
        if (est < 0 || mn > 0.30) return r;
    }

    double tauRef = (double) est;
    if (est > 1 && est < tauMax)
    {
        const double a = tmp[(size_t) est - 1], b = tmp[(size_t) est], c = tmp[(size_t) est + 1];
        const double den = a - 2.0 * b + c;
        if (std::abs (den) > 1e-12)
            tauRef += std::min (std::max (0.5 * (a - c) / den, -1.0), 1.0);
    }

    r.freq = sr / tauRef;
    r.conf = std::min (std::max (1.0 - tmp[(size_t) est], 0.0), 1.0);
    return r;
}

//==============================================================================
//  Key estimator (Krumhansl-Schmuckler on a decaying pitch-class histogram)
//==============================================================================
class KeyEstimator
{
public:
    struct Result { bool valid = false; int root = 0; bool minor = false; double conf = 0.0; };

    void reset() { chroma.fill (0.0); }

    void addNote (double midi, double weight) noexcept
    {
        const int pc = (((int) std::lround (midi)) % 12 + 12) % 12;
        chroma[(size_t) pc] += weight;
    }

    void decay (double f) noexcept { for (auto& c : chroma) c *= f; }

    Result estimate() const
    {
        static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double min[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

        Result res;
        double total = 0.0;
        for (double c : chroma) total += c;
        if (total < 15.0) return res;   // roughly 0.6 s of confident notes

        double best = -2.0, second = -2.0;
        int bestRoot = 0; bool bestMinor = false;
        for (int mode = 0; mode < 2; ++mode)
            for (int root = 0; root < 12; ++root)
            {
                double prof[12];
                for (int pc = 0; pc < 12; ++pc)
                    prof[pc] = (mode == 0 ? maj : min)[((pc - root) % 12 + 12) % 12];
                const double r = pearson (chroma.data(), prof);
                if (r > best)        { second = best; best = r; bestRoot = root; bestMinor = (mode == 1); }
                else if (r > second) { second = r; }
            }

        res.valid = true;
        res.root = bestRoot;
        res.minor = bestMinor;
        res.conf = std::min (std::max (best, 0.0), 1.0)
                 * std::min (std::max ((best - second) / 0.12, 0.25), 1.0);
        return res;
    }

private:
    static double pearson (const double* a, const double* b) noexcept
    {
        double ma = 0, mb = 0;
        for (int i = 0; i < 12; ++i) { ma += a[i]; mb += b[i]; }
        ma /= 12.0; mb /= 12.0;
        double num = 0, da = 0, db = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = a[i] - ma, y = b[i] - mb;
            num += x * y; da += x * x; db += y * y;
        }
        const double den = std::sqrt (da * db);
        return den > 1e-12 ? num / den : 0.0;
    }

    std::array<double, 12> chroma {};
};

//==============================================================================
//  CorrAligner - decaying cross-correlation between bass (x) and kick (y).
//
//  C(s) = sum_n x[n - s] * y[n]
//  A positive peak at lag s means: delaying the bass by s samples (and keeping
//  polarity) lines it up with the kick. A negative peak means: also flip polarity.
//==============================================================================
class CorrAligner
{
public:
    static constexpr int kMaxLag  = 42;                 // decimated samples (~10.5 ms @ 4 kHz)
    static constexpr int kNumLags = 2 * kMaxLag + 1;

    struct Result
    {
        bool   valid = false;
        double lag = 0.0;      // decimated samples, refined (sub-sample)
        int    sign = 1;       // +1 keep polarity, -1 flip
        double score = 0.0;    // |normalised correlation| at the peak, 0..1
    };

    CorrAligner() { reset(); }

    void reset()
    {
        acc.fill (0.0);
        accEx = accEy = 0.0;
        curIdx = -1;
        haveData = false;
    }

    bool hasData() const noexcept { return haveData; }

    // Returns true if the window carried enough energy in BOTH signals to be used.
    bool update (const float* x, const float* y, int n, double decay)
    {
        double ex = 0.0, ey = 0.0;
        for (int i = 0; i < n; ++i) { ex += (double) x[i] * x[i]; ey += (double) y[i] * y[i]; }
        const double gate = 1e-8 * (double) n;
        if (ex < gate || ey < gate) return false;

        for (int k = 0; k < kNumLags; ++k)
        {
            const int s  = k - kMaxLag;
            const int lo = std::max (0, s);
            const int hi = std::min (n, n + s);
            double sum = 0.0;
            for (int i = lo; i < hi; ++i) sum += (double) x[i - s] * (double) y[i];
            acc[(size_t) k] = acc[(size_t) k] * decay + sum;
        }
        accEx = accEx * decay + ex;
        accEy = accEy * decay + ey;
        haveData = true;
        return true;
    }

    Result evaluate()
    {
        Result r;
        if (! haveData) return r;

        const double norm = std::sqrt (accEx * accEy) + 1e-20;
        auto score = [&] (int k)
        {
            return std::abs (acc[(size_t) k]) / norm
                 * (1.0 - 0.08 * std::abs ((double) (k - kMaxLag)) / (double) kMaxLag);
        };

        int gBest = 0; double gScore = -1.0;
        for (int k = 0; k < kNumLags; ++k)
            if (score (k) > gScore) { gScore = score (k); gBest = k; }

        if (curIdx < 0)
        {
            curIdx = gBest;
        }
        else
        {
            int lBest = curIdx; double lScore = score (curIdx);       // follow the current peak locally
            for (int k = std::max (0, curIdx - 3); k <= std::min (kNumLags - 1, curIdx + 3); ++k)
                if (score (k) > lScore) { lScore = score (k); lBest = k; }
            curIdx = lBest;

            if (std::abs (gBest - curIdx) > 3 && gScore > 1.06 * score (curIdx))   // hysteresis for jumps
                curIdx = gBest;
        }

        double lag = (double) (curIdx - kMaxLag);
        if (curIdx > 0 && curIdx < kNumLags - 1)
        {
            const double y1 = std::abs (acc[(size_t) curIdx - 1]);
            const double y2 = std::abs (acc[(size_t) curIdx]);
            const double y3 = std::abs (acc[(size_t) curIdx + 1]);
            const double den = y1 - 2.0 * y2 + y3;
            if (std::abs (den) > 1e-18)
                lag += std::min (std::max (0.5 * (y1 - y3) / den, -1.0), 1.0);
        }

        r.valid = true;
        r.lag   = lag;
        r.sign  = acc[(size_t) curIdx] >= 0.0 ? 1 : -1;
        r.score = std::abs (acc[(size_t) curIdx]) / norm;
        return r;
    }

    // Signed normalised correlation at a fractional lag (decimated samples).
    double normalisedAt (double lag) const
    {
        if (! haveData) return 0.0;
        const double norm = std::sqrt (accEx * accEy) + 1e-20;
        double p = std::min (std::max (lag + (double) kMaxLag, 0.0), (double) (kNumLags - 1));
        const int i0 = (int) p;
        const int i1 = std::min (i0 + 1, kNumLags - 1);
        const double f = p - (double) i0;
        return ((1.0 - f) * acc[(size_t) i0] + f * acc[(size_t) i1]) / norm;
    }

    void copyCurve (float* out) const
    {
        const double norm = std::sqrt (accEx * accEy) + 1e-20;
        for (int k = 0; k < kNumLags; ++k)
            out[k] = haveData ? (float) (acc[(size_t) k] / norm) : 0.0f;
    }

private:
    std::array<double, kNumLags> acc {};
    double accEx = 0.0, accEy = 0.0;
    int curIdx = -1;
    bool haveData = false;
};

//==============================================================================
//  PhaseSpectrum - cross-spectrum of bass vs kick (~20..400 Hz) for the visualizer.
//  Phase = arg(B * conj(K)):  negative = bass arrives LATE vs the kick at that
//  frequency, positive = bass is EARLY.  Coherence says how much to trust each bin.
//==============================================================================
class PhaseSpectrum
{
public:
    static constexpr int kN    = 1024;                 // analysis window (decimated samples)
    static constexpr int kLo   = 5;                    // bin 5   ~ 19.5 Hz @ 4 kHz
    static constexpr int kHi   = 103;                  // bin 103 ~ 402 Hz
    static constexpr int kBins = kHi - kLo + 1;

    PhaseSpectrum()
    {
        win.resize ((size_t) kN);
        for (int n = 0; n < kN; ++n)
            win[(size_t) n] = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) n / (double) (kN - 1));
        reset();
    }

    void reset()
    {
        accBB.fill (0.0); accKK.fill (0.0); accRe.fill (0.0); accIm.fill (0.0);
        haveData = false;
    }

    bool hasData() const noexcept { return haveData; }

    static double binFreq (int i, double dsr) noexcept { return (double) (kLo + i) * dsr / (double) kN; }

    // bass / kick must each hold kN decimated samples.
    bool update (const float* bass, const float* kick, double decay)
    {
        double eb = 0.0, ek = 0.0;
        for (int i = 0; i < kN; ++i) { eb += (double) bass[i] * bass[i]; ek += (double) kick[i] * kick[i]; }
        const double gate = 1e-8 * (double) kN;
        if (eb < gate || ek < gate) return false;

        for (int b = 0; b < kBins; ++b)
        {
            const double w  = 2.0 * kPi * (double) (kLo + b) / (double) kN;
            const double cw = std::cos (w), sw = std::sin (w);
            double c = 1.0, s = 0.0;
            double bRe = 0, bIm = 0, kRe = 0, kIm = 0;
            for (int n = 0; n < kN; ++n)
            {
                const double xb = (double) bass[n] * win[(size_t) n];
                const double xk = (double) kick[n] * win[(size_t) n];
                bRe += xb * c;  bIm -= xb * s;
                kRe += xk * c;  kIm -= xk * s;
                const double nc = c * cw - s * sw;     // rotate the oscillator
                s = s * cw + c * sw;
                c = nc;
            }
            const double re = bRe * kRe + bIm * kIm;   // B * conj(K)
            const double im = bIm * kRe - bRe * kIm;
            const size_t i = (size_t) b;
            accBB[i] = accBB[i] * decay + (bRe * bRe + bIm * bIm);
            accKK[i] = accKK[i] * decay + (kRe * kRe + kIm * kIm);
            accRe[i] = accRe[i] * decay + re;
            accIm[i] = accIm[i] * decay + im;
        }
        haveData = true;
        return true;
    }

    // magB / magK: amplitude, phase: radians (bass - kick), coh: 0..1
    void get (float* magB, float* magK, float* phase, float* coh) const
    {
        for (int b = 0; b < kBins; ++b)
        {
            const size_t i = (size_t) b;
            magB[b]  = haveData ? (float) std::sqrt (accBB[i]) : 0.0f;
            magK[b]  = haveData ? (float) std::sqrt (accKK[i]) : 0.0f;
            phase[b] = haveData ? (float) std::atan2 (accIm[i], accRe[i]) : 0.0f;
            const double den = accBB[i] * accKK[i];
            coh[b]   = (haveData && den > 1e-30) ? (float) ((accRe[i] * accRe[i] + accIm[i] * accIm[i]) / den) : 0.0f;
        }
    }

private:
    std::vector<double> win;
    std::array<double, kBins> accBB {}, accKK {}, accRe {}, accIm {};
    bool haveData = false;
};

//==============================================================================
//  Preview renderer - what the visualizer draws as "bass AFTER PhaseForge".
//  Runs the same signal flow as AlignEngine, but in one batch on the decimated
//  (~4 kHz) analysis signal, so the picture shows exactly what your settings do
//  (shift, polarity, phase rotation, low-band mode and mix).
//==============================================================================
struct PreviewParams
{
    bool   lowBand = false;
    double xoverHz = 120.0;
    int    stages = 0;
    double rotFreqHz = 60.0, rotQ = 0.7;
    double shiftSamples = 0.0;     // in decimated samples, + = later
    double polarity = 1.0;         // +1 / -1
    double mix = 1.0;
};

struct PreviewScratch { std::vector<float> lo, hi, dl; };

// Catmull-Rom / Hermite read at a fractional position (outside the array = silence).
inline float readFrac (const float* x, int n, double pos) noexcept
{
    const int i0 = (int) std::floor (pos);
    const double g = pos - (double) i0;
    auto at = [&] (int k) -> double { return (k < 0 || k >= n) ? 0.0 : (double) x[k]; };
    const double p0 = at (i0 - 1), p1 = at (i0), p2 = at (i0 + 1), p3 = at (i0 + 2);
    return (float) (p1 + 0.5 * g * (p2 - p0 + g * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3
                                                  + g * (3.0 * (p1 - p2) + p3 - p0))));
}

inline void renderPreview (const float* in, float* out, int n, double dsr,
                           const PreviewParams& p, PreviewScratch& sc)
{
    sc.lo.resize ((size_t) n);
    sc.hi.resize ((size_t) n);
    sc.dl.resize ((size_t) n);

    if (p.lowBand)
    {
        Biquad lp1, lp2, hp1, hp2;
        lp1.setLowpass  (dsr, p.xoverHz, 0.70710678);
        lp2.setLowpass  (dsr, p.xoverHz, 0.70710678);
        hp1.setHighpass (dsr, p.xoverHz, 0.70710678);
        hp2.setHighpass (dsr, p.xoverHz, 0.70710678);
        for (int i = 0; i < n; ++i)
        {
            const double x = (double) in[i];
            sc.lo[(size_t) i] = (float) lp2.process (lp1.process (x));
            sc.hi[(size_t) i] = (float) hp2.process (hp1.process (x));
        }
    }
    else
    {
        for (int i = 0; i < n; ++i) { sc.lo[(size_t) i] = in[i]; sc.hi[(size_t) i] = 0.0f; }
    }

    for (int i = 0; i < n; ++i)
        sc.dl[(size_t) i] = readFrac (sc.lo.data(), n, (double) i - p.shiftSamples);

    const int stages = std::min (std::max (p.stages, 0), AlignEngine::kMaxStages);
    std::array<Biquad, AlignEngine::kMaxStages> ap;
    for (auto& a : ap) a.setAllpass (dsr, p.rotFreqHz, p.rotQ);

    for (int i = 0; i < n; ++i)
    {
        double d = (double) sc.dl[(size_t) i];
        for (int s = 0; s < stages; ++s) d = ap[(size_t) s].process (d);

        const double wet = p.polarity * d + (p.lowBand ? (double) sc.hi[(size_t) i] : 0.0);
        out[i] = (float) ((double) in[i] + p.mix * (wet - (double) in[i]));
    }
}

//==============================================================================
//  Analyzer - everything the background thread does, in one testable object.
//==============================================================================
class Analyzer
{
public:
    static constexpr int kLearnTarget  = 50;    // valid updates (~2 s of material)
    static constexpr int kLearnTimeout = 700;   // updates (~28 s) before giving up

    struct Output
    {
        bool   alignUpdated = false;
        bool   alignValid = false;
        double shiftMs = 0.0;
        int    polarity = 1;
        double score = 0.0;

        double beforeCorr = 0.0;       // correlation with NO alignment
        double bassHz = 0.0, bassConf = 0.0;
        double kickHz = 0.0, kickConf = 0.0;
        bool   keyValid = false; int keyRoot = 0; bool keyMinor = false; double keyConf = 0.0;
    };

    struct LearnResult { bool done = false; bool ok = false; double shiftMs = 0.0; int polarity = 1; };

    void reset()
    {
        aligner.reset(); learner.reset(); key.reset(); spectrum.reset();
        learning = false; learnValid = learnTicks = 0;
        bassHist = {}; kickHist = {};
        bassMiss = kickMiss = 1000;
        lastBass = lastKick = PitchResult {};
        alignValid = false; lastShiftMs = 0.0; lastSign = 1; lastScore = 0.0;
        pendingLearn = LearnResult {};
    }

    void resetKey() { key.reset(); }

    void startLearn()
    {
        learner.reset();
        learning = true; learnValid = 0; learnTicks = 0;
    }

    bool   isLearning() const noexcept { return learning; }
    double learnProgress() const noexcept
    {
        return learning ? std::min (1.0, (double) learnValid / (double) kLearnTarget) : 0.0;
    }

    // Call after analyse(); if done==true the result is consumed.
    LearnResult takeLearnResult()
    {
        LearnResult r = pendingLearn;
        pendingLearn = LearnResult {};
        return r;
    }

    CorrAligner&   getAligner()  noexcept { return aligner; }
    PhaseSpectrum& getSpectrum() noexcept { return spectrum; }

    // n must equal PhaseSpectrum::kN for the spectrum to update.
    Output analyse (const float* bass, const float* kick, int n, double dsr, double decay)
    {
        Output o;

        // ---- alignment ------------------------------------------------------
        if (aligner.update (bass, kick, n, decay))
        {
            const auto r = aligner.evaluate();
            if (r.valid && r.score > 0.12)
            {
                alignValid = true;
                lastShiftMs = std::min (std::max (r.lag / dsr * 1000.0, -AlignEngine::kMaxShiftMs), AlignEngine::kMaxShiftMs);
                lastSign = r.sign;
                lastScore = r.score;
            }
            o.alignUpdated = true;
        }
        o.alignValid = alignValid;
        o.shiftMs = lastShiftMs;
        o.polarity = lastSign;
        o.score = lastScore;
        o.beforeCorr = aligner.normalisedAt (0.0);

        // ---- per-frequency phase (visualizer) ---------------------------------
        if (n == PhaseSpectrum::kN)
            spectrum.update (bass, kick, decay);

        // ---- learn mode -----------------------------------------------------
        if (learning)
        {
            ++learnTicks;
            if (learner.update (bass, kick, n, 0.9995)) ++learnValid;

            if (learnValid >= kLearnTarget || learnTicks >= kLearnTimeout)
            {
                const auto r = learner.evaluate();
                pendingLearn.done = true;
                pendingLearn.ok = r.valid && r.score > 0.10 && learnValid >= 10;
                pendingLearn.shiftMs = std::min (std::max (r.lag / dsr * 1000.0, -AlignEngine::kMaxShiftMs), AlignEngine::kMaxShiftMs);
                pendingLearn.polarity = r.sign;
                learning = false;
            }
        }

        // ---- pitch / key ----------------------------------------------------
        trackPitch (bass, n, dsr, 27.5, 330.0, bassHist, bassMiss, lastBass, 1.0);
        trackPitch (kick, n, dsr, 27.5, 200.0, kickHist, kickMiss, lastKick, 0.5);
        key.decay (0.9995);

        o.bassHz = bassMiss < kHold ? lastBass.freq : 0.0;
        o.bassConf = bassMiss < kHold ? lastBass.conf : 0.0;
        o.kickHz = kickMiss < kHold ? lastKick.freq : 0.0;
        o.kickConf = kickMiss < kHold ? lastKick.conf : 0.0;

        const auto k = key.estimate();
        o.keyValid = k.valid; o.keyRoot = k.root; o.keyMinor = k.minor; o.keyConf = k.conf;
        return o;
    }

private:
    static constexpr int kHold = 40;    // updates to keep showing a pitch after the note stops

    struct Hist { std::array<double, 5> m {}; int count = 0; int head = 0; };

    void trackPitch (const float* x, int n, double dsr, double fmin, double fmax,
                     Hist& h, int& miss, PitchResult& last, double keyWeight)
    {
        const auto p = detectPitchYin (x, n, dsr, fmin, fmax, scratch);
        if (p.conf >= 0.80 && p.freq > 0.0)
        {
            h.m[(size_t) h.head] = freqToMidi (p.freq);
            h.head = (h.head + 1) % 5;
            h.count = std::min (h.count + 1, 5);

            std::array<double, 5> tmp = h.m;
            for (int i = 1; i < h.count; ++i)       // insertion sort of the valid entries
            {
                const double v = tmp[(size_t) i];
                int j = i - 1;
                while (j >= 0 && tmp[(size_t) j] > v) { tmp[(size_t) j + 1] = tmp[(size_t) j]; --j; }
                tmp[(size_t) j + 1] = v;
            }
            const double med = tmp[(size_t) (h.count / 2)];

            last.freq = midiToFreq (med);
            last.conf = p.conf;
            miss = 0;
            key.addNote (med, keyWeight * p.conf * p.conf);
        }
        else
        {
            miss = std::min (miss + 1, 100000);
        }
    }

    CorrAligner aligner, learner;
    PhaseSpectrum spectrum;
    KeyEstimator key;
    std::vector<double> scratch;

    Hist bassHist, kickHist;
    int bassMiss = 1000, kickMiss = 1000;
    PitchResult lastBass, lastKick;

    bool alignValid = false;
    double lastShiftMs = 0.0, lastScore = 0.0;
    int lastSign = 1;

    bool learning = false;
    int learnValid = 0, learnTicks = 0;
    LearnResult pendingLearn;
};

} // namespace pf
