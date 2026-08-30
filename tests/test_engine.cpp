// test_engine -- JUCE-free harness for SoundFontBank + ScoutEngine.
//
// Builds a tiny but structurally complete SF2 in memory (two samples: a
// looped sine "Loop_C4" on keys 0-71 and a one-shot ramp "Shot_C5" on 72-127),
// then checks the spec's acceptance items that a harness can check:
//   [load]      parse, preset list, zone join to sample names, loop points
//   [authored]  AS-AUTHORED starts at sample 0, loops indefinitely, no-loop
//               zone plays through once and frees its voice
//   [looponly]  LOOP-ONLY starts AT loopStart; unlooped zone loops whole sample
//   [pitch]     root key plays at native pitch; +12 st doubles the rate
//   [release]   note-off fades to silence within ~80 ms and frees the voice
//   [malformed] garbage / truncated input -> error string, no crash
//   [swap]      bank swap retires the old bank exactly once
// Compile (validator.json dsp stage):
//   cl /nologo /EHsc /O2 /std:c++17 tests\test_engine.cpp Source\engine\SoundFontBank.cpp Source\engine\ScoutEngine.cpp /Fo:scratch\ /Fe:scratch\test_engine.exe
#include "../Source/engine/SoundFontBank.h"
#include "../Source/engine/ScoutEngine.h"
#include "../Source/engine/NoteNames.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

using namespace sf2scout;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; std::printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
#define SECTION(name) std::printf("[%s]\n", name)

// ---------------------------------------------------------------- SF2 writer
namespace
{
struct Buf
{
    std::vector<uint8_t> b;
    void u8 (uint8_t v)   { b.push_back (v); }
    void u16 (uint16_t v) { u8 ((uint8_t) v); u8 ((uint8_t) (v >> 8)); }
    void i16 (int16_t v)  { u16 ((uint16_t) v); }
    void u32 (uint32_t v) { u16 ((uint16_t) v); u16 ((uint16_t) (v >> 16)); }
    void fcc (const char* s) { for (int i = 0; i < 4; ++i) u8 ((uint8_t) s[i]); }
    void name20 (const char* s) { size_t n = std::strlen (s); for (size_t i = 0; i < 20; ++i) u8 (i < n ? (uint8_t) s[i] : 0); }
    void bytes (const Buf& o) { b.insert (b.end(), o.b.begin(), o.b.end()); }
    void chunk (const char* id, const Buf& body)
    {
        fcc (id); u32 ((uint32_t) body.b.size()); bytes (body);
        if (body.b.size() & 1) u8 (0);
    }
    void list (const char* type, const Buf& body)
    {
        Buf inner; inner.fcc (type); inner.bytes (body);
        chunk ("LIST", inner);
    }
};

struct Sample { const char* name; std::vector<int16_t> pcm; uint32_t loopStart, loopEnd; uint32_t rate; uint8_t root; };

// generator opcodes we use
enum { GEN_KEYRANGE = 43, GEN_VELRANGE = 44, GEN_SAMPLEMODES = 54, GEN_ROOTKEY = 58, GEN_SAMPLEID = 53, GEN_INSTRUMENT = 41 };

std::vector<uint8_t> buildTestSf2 (std::vector<Sample>& samples)
{
    // --- sdta: concatenated PCM with 46 zero guard samples after each
    Buf smpl; std::vector<uint32_t> starts, ends;
    for (auto& s : samples)
    {
        starts.push_back ((uint32_t) (smpl.b.size() / 2));
        for (int16_t v : s.pcm) smpl.i16 (v);
        ends.push_back ((uint32_t) (smpl.b.size() / 2));
        for (int i = 0; i < 46; ++i) smpl.i16 (0);
    }
    Buf sdtaBody; sdtaBody.chunk ("smpl", smpl);

    // --- pdta
    Buf phdr, pbag, pmod, pgen, inst, ibag, imod, igen, shdr;
    // one preset "Scout Test", bank 0 program 0, one pbag pointing to instrument 0
    phdr.name20 ("Scout Test"); phdr.u16 (0); phdr.u16 (0); phdr.u16 (0); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
    phdr.name20 ("EOP");        phdr.u16 (0); phdr.u16 (0); phdr.u16 (1); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
    pbag.u16 (0); pbag.u16 (0);   // bag 0: gens start 0
    pbag.u16 (1); pbag.u16 (0);   // terminal
    pgen.u16 (GEN_INSTRUMENT); pgen.u16 (0);
    pgen.u16 (0); pgen.u16 (0);   // terminal
    pmod.u16 (0); pmod.u16 (0); pmod.i16 (0); pmod.u16 (0); pmod.u16 (0); // terminal only

    // one instrument, two zones
    inst.name20 ("ScoutInst"); inst.u16 (0);
    inst.name20 ("EOI");       inst.u16 (2);
    // zone 0: keys 0-71, looped sample 0, root from shdr
    ibag.u16 (0); ibag.u16 (0);
    // zone 1: keys 72-127, one-shot sample 1
    ibag.u16 (3); ibag.u16 (0);
    ibag.u16 (6); ibag.u16 (0);   // terminal
    igen.u16 (GEN_KEYRANGE); igen.u8 (0);  igen.u8 (71);
    igen.u16 (GEN_SAMPLEMODES); igen.u16 (1);
    igen.u16 (GEN_SAMPLEID); igen.u16 (0);
    igen.u16 (GEN_KEYRANGE); igen.u8 (72); igen.u8 (127);
    igen.u16 (GEN_SAMPLEMODES); igen.u16 (0);
    igen.u16 (GEN_SAMPLEID); igen.u16 (1);
    igen.u16 (0); igen.u16 (0);   // terminal
    imod.u16 (0); imod.u16 (0); imod.i16 (0); imod.u16 (0); imod.u16 (0);

    for (size_t i = 0; i < samples.size(); ++i)
    {
        auto& s = samples[i];
        shdr.name20 (s.name);
        shdr.u32 (starts[i]); shdr.u32 (ends[i]);
        shdr.u32 (starts[i] + s.loopStart); shdr.u32 (starts[i] + s.loopEnd);
        shdr.u32 (s.rate); shdr.u8 (s.root); shdr.u8 (0); shdr.u16 (0); shdr.u16 (1);
    }
    shdr.name20 ("EOS"); for (int i = 0; i < 5; ++i) shdr.u32 (0); shdr.u8 (0); shdr.u8 (0); shdr.u16 (0); shdr.u16 (0);

    Buf pdtaBody;
    pdtaBody.chunk ("phdr", phdr); pdtaBody.chunk ("pbag", pbag); pdtaBody.chunk ("pmod", pmod); pdtaBody.chunk ("pgen", pgen);
    pdtaBody.chunk ("inst", inst); pdtaBody.chunk ("ibag", ibag); pdtaBody.chunk ("imod", imod); pdtaBody.chunk ("igen", igen);
    pdtaBody.chunk ("shdr", shdr);

    // --- INFO
    Buf info, ifil; ifil.u16 (2); ifil.u16 (1); info.chunk ("ifil", ifil);
    Buf isng; for (char c : std::string ("EMU8000")) isng.u8 ((uint8_t) c); isng.u8 (0); info.chunk ("isng", isng);
    Buf inam; for (char c : std::string ("Scout Test")) inam.u8 ((uint8_t) c); inam.u8 (0); info.chunk ("INAM", inam);

    Buf riffBody; riffBody.fcc ("sfbk");
    riffBody.list ("INFO", info); riffBody.list ("sdta", sdtaBody); riffBody.list ("pdta", pdtaBody);
    Buf file; file.chunk ("RIFF", riffBody);
    return file.b;
}

std::vector<int16_t> sine (int len, double periodSamples, double amp = 0.5)
{
    std::vector<int16_t> v ((size_t) len);
    for (int i = 0; i < len; ++i) v[(size_t) i] = (int16_t) (amp * 32767.0 * std::sin (2.0 * 3.14159265358979 * i / periodSamples));
    return v;
}
std::vector<int16_t> ramp (int len)
{
    std::vector<int16_t> v ((size_t) len);
    for (int i = 0; i < len; ++i) v[(size_t) i] = (int16_t) (1000 + i);   // strictly increasing, never zero
    return v;
}

// Renders `n` samples in blocks, returns mono (left) output.
std::vector<float> render (ScoutEngine& e, int n, int block = 64)
{
    std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
    for (int i = 0; i < n; i += block)
    {
        const int m = std::min (block, n - i);
        e.process (L.data() + i, R.data() + i, m, 1.0f);
    }
    return L;
}

// Estimate period (in samples) of a sine by zero-crossing spacing.
double estimatePeriod (const std::vector<float>& x, size_t from, size_t to)
{
    std::vector<size_t> zc;
    for (size_t i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) zc.push_back (i);
    if (zc.size() < 3) return 0.0;
    return (double) (zc.back() - zc.front()) / (double) (zc.size() - 1);
}
} // namespace

// ----------------------------------------------------------------- tests
int main()
{
    std::vector<Sample> samples = {
        { "Loop_C4", sine (4000, 100.0), 1000, 3000, 44100, 60 },   // loop [1000,3000), 100-sample period
        { "Shot_C5", ramp (2000),        0,    0,    44100, 72 },   // one-shot
    };
    std::vector<uint8_t> file = buildTestSf2 (samples);
    std::printf ("test sf2: %zu bytes\n", file.size());

    SECTION ("load");
    std::string err;
    auto bank = SoundFontBank::load (file.data(), file.size(), err);
    CHECK (bank != nullptr);
    if (bank == nullptr) { std::printf ("  load error: %s\n", err.c_str()); return 1; }
    CHECK (bank->presetCount() == 1);
    const Preset& p = bank->presets()[0];
    CHECK (p.name == "Scout Test");
    CHECK (p.bank == 0 && p.program == 0);
    CHECK (p.zones.size() == 2);
    const Zone* z0 = bank->zoneForKey (0, 60);
    const Zone* z1 = bank->zoneForKey (0, 84);
    CHECK (z0 != nullptr && z1 != nullptr);
    CHECK (z0->sampleName == "Loop_C4");
    CHECK (z1->sampleName == "Shot_C5");
    CHECK (z0->lokey == 0 && z0->hikey == 71);
    CHECK (z1->lokey == 72 && z1->hikey == 127);
    CHECK (z0->rootKey == 60 && z1->rootKey == 72);
    CHECK (z0->hasLoop());
    CHECK (! z1->hasLoop());
    CHECK (z0->loopStartRel() == 1000 && z0->loopEndRel() == 3000);
    CHECK (z0->loopLength() == 2000);
    CHECK (z0->sampleLength() == 4000);
    CHECK (z1->sampleLength() == 2000);
    CHECK (z0->sampleRate == 44100);
    CHECK (bank->zoneForKey (0, 71)->sampleName == "Loop_C4");   // boundary key
    CHECK (bank->zoneForKey (0, 72)->sampleName == "Shot_C5");   // boundary key
    CHECK (noteName (60) == "C4" && noteName (72) == "C5" && noteName (0) == "C-1" && noteName (127) == "G9");
    CHECK (formatMs (45.35) == "45.4 ms" && formatMs (2000.0) == "2000 ms");

    const uint32_t shotStart = z1->sampleStart;
    const uint32_t loopStartAbs = z0->loopStart;

    SECTION ("authored");
    {
        ScoutEngine e;
        e.prepare (44100.0);
        e.setBank (bank.release());
        e.setMode (PlayMode::AsAuthored);
        e.setPreset (0);
        // one-shot zone at its root: output must equal the ramp from sample 0, then stop
        e.noteOn (72, 127);
        auto out = render (e, 2100);
        CHECK (std::fabs (out[0] - (1000.0f / 32767.0f)) < 1e-4f);           // starts at sample start
        CHECK (std::fabs (out[500] - (1500.0f / 32767.0f)) < 1e-4f);         // native pitch, 1:1
        CHECK (e.activeVoiceCount() == 0);                                    // played through once, freed
        CHECK (out[2050] == 0.0f);
        auto ln = e.lastNote();
        CHECK (ln.note == 72 && ln.zoneIndex == 1 && ln.velocity == 127 && ln.sequence == 1);

        // looped zone at root: sustains indefinitely, playhead stays inside the loop
        e.noteOn (60, 100);
        out = render (e, 44100);
        CHECK (e.activeVoiceCount() == 1);
        const double ph = e.lastPlayhead();
        CHECK (ph >= 1000.0 && ph < 3000.0);
        // loop seam is clean: the 100-sample sine loops over 2000 samples = 20 periods exactly
        double period = estimatePeriod (out, 5000, 44000);
        CHECK (std::fabs (period - 100.0) < 0.5);
        float peak = 0.0f; for (size_t i = 5000; i < 44100; ++i) peak = std::max (peak, std::fabs (out[i]));
        CHECK (peak > 0.3f && peak < 0.6f);                                   // sqrt(100/127)*0.5 = 0.44
        (void) shotStart;
    }

    SECTION ("looponly");
    {
        std::string e2; auto bank2 = SoundFontBank::load (file.data(), file.size(), e2);
        ScoutEngine e;
        e.prepare (44100.0);
        e.setBank (bank2.release());
        e.setMode (PlayMode::LoopOnly);
        e.setPreset (0);
        e.noteOn (60, 127);
        auto out = render (e, 64);
        // first output sample == pool[loopStart] (sin(2*pi*1000/100) = 0 -> use sample 25 of the loop instead)
        e.reset();
        e.noteOn (60, 127);
        out = render (e, 64);
        const float expected25 = 0.5f * std::sin (2.0f * 3.14159265f * (float) (loopStartAbs + 25) / 100.0f);
        CHECK (std::fabs (out[25] - expected25) < 2e-3f);
        CHECK (e.lastPlayhead() >= 1000.0 && e.lastPlayhead() < 3000.0);

        // unlooped zone in LOOP-ONLY: loops the whole sample -> still active after 3x its length
        e.reset();
        e.noteOn (84, 127);
        out = render (e, 6000);
        CHECK (e.activeVoiceCount() == 1);
        CHECK (out[5999] != 0.0f);
    }

    SECTION ("pitch");
    {
        std::string e3; auto bank3 = SoundFontBank::load (file.data(), file.size(), e3);
        ScoutEngine e;
        e.prepare (44100.0);
        e.setBank (bank3.release());
        e.setPreset (0);
        e.noteOn (60, 127);
        auto out = render (e, 20000);
        CHECK (std::fabs (estimatePeriod (out, 4000, 20000) - 100.0) < 0.5);   // root -> native
        e.reset(); e.noteOn (48, 127);
        out = render (e, 20000);
        CHECK (std::fabs (estimatePeriod (out, 4000, 20000) - 200.0) < 1.0);   // -12 st -> half rate
        e.reset(); e.noteOn (67, 127);                                          // +7 st
        out = render (e, 20000);
        CHECK (std::fabs (estimatePeriod (out, 4000, 20000) - 100.0 / std::pow (2.0, 7.0 / 12.0)) < 0.5);
        // host at 96 kHz: period scales with the rate ratio
        e.prepare (96000.0); e.noteOn (60, 127);
        out = render (e, 20000);
        CHECK (std::fabs (estimatePeriod (out, 4000, 20000) - 100.0 * 96000.0 / 44100.0) < 1.0);
        // pitch bend +2 st
        e.prepare (44100.0); e.setPitchBend (2.0); e.noteOn (60, 127);
        out = render (e, 20000);
        CHECK (std::fabs (estimatePeriod (out, 4000, 20000) - 100.0 / std::pow (2.0, 2.0 / 12.0)) < 0.5);
    }

    SECTION ("release");
    {
        std::string e4; auto bank4 = SoundFontBank::load (file.data(), file.size(), e4);
        ScoutEngine e;
        e.prepare (44100.0);
        e.setBank (bank4.release());
        e.setPreset (0);
        e.noteOn (60, 127);
        render (e, 1000);
        e.noteOff (60);
        auto out = render (e, 4410);     // 100 ms
        CHECK (e.activeVoiceCount() == 0);
        float tail = 0.0f; for (size_t i = 3600; i < out.size(); ++i) tail = std::max (tail, std::fabs (out[i]));
        CHECK (tail == 0.0f);            // silent after 80 ms
        float head = 0.0f; for (size_t i = 0; i < 100; ++i) head = std::max (head, std::fabs (out[i]));
        CHECK (head > 0.1f);             // but not cut instantly
        // polyphony + stealing: 40 notes -> 32 voices, none lost to a crash
        e.reset();
        for (int n = 0; n < 40; ++n) e.noteOn (30 + n, 100);
        render (e, 64);
        CHECK (e.activeVoiceCount() == ScoutEngine::kMaxVoices);
        e.allNotesOff();
        render (e, 8820);
        CHECK (e.activeVoiceCount() == 0);
    }

    SECTION ("malformed");
    {
        std::string m;
        CHECK (SoundFontBank::load (nullptr, 0, m) == nullptr && ! m.empty());
        const char junk[] = "this is not a soundfont at all, just some bytes......";
        CHECK (SoundFontBank::load (junk, sizeof (junk), m) == nullptr && ! m.empty());
        std::vector<uint8_t> riffOnly = { 'R','I','F','F', 4,0,0,0, 's','f','b','k' };
        CHECK (SoundFontBank::load (riffOnly.data(), riffOnly.size(), m) == nullptr);
        // truncated at every 1/8th of the file -> never crash
        int refused = 0;
        for (int k = 1; k < 8; ++k)
        {
            const size_t cut = file.size() * (size_t) k / 8;
            if (SoundFontBank::load (file.data(), cut, m) == nullptr) ++refused;
        }
        CHECK (refused >= 6);
        // header claims a huge chunk size
        std::vector<uint8_t> lie = file; lie[4] = 0xff; lie[5] = 0xff; lie[6] = 0xff; lie[7] = 0x7f;
        (void) SoundFontBank::load (lie.data(), lie.size(), m);   // must not crash; result either way
        CHECK (true);
    }

    SECTION ("swap");
    {
        std::string s1, s2;
        auto a = SoundFontBank::load (file.data(), file.size(), s1);
        auto b = SoundFontBank::load (file.data(), file.size(), s2);
        SoundFontBank* araw = a.get();
        ScoutEngine e;
        e.prepare (44100.0);
        e.setBank (a.release());
        render (e, 64);
        CHECK (e.takeRetiredBank() == nullptr);
        e.noteOn (60, 100);
        render (e, 64);
        CHECK (e.activeVoiceCount() == 1);
        e.setBank (b.release());
        CHECK (e.takeRetiredBank() == nullptr);      // not swapped until the audio thread runs
        render (e, 64);
        CHECK (e.activeVoiceCount() == 0);           // voices killed on swap
        SoundFontBank* r = e.takeRetiredBank();
        CHECK (r == araw);
        delete r;
        CHECK (e.takeRetiredBank() == nullptr);
        // replacing a pending bank before the audio thread runs must not leak or double-free
        std::string s3, s4;
        auto c = SoundFontBank::load (file.data(), file.size(), s3);
        auto d = SoundFontBank::load (file.data(), file.size(), s4);
        e.setBank (c.release());
        e.setBank (d.release());                     // c deleted inside
        render (e, 64);
        SoundFontBank* r2 = e.takeRetiredBank();
        CHECK (r2 != nullptr);
        delete r2;
        e.noteOn (60, 100); render (e, 64);
        CHECK (e.activeVoiceCount() == 1);
    }

    std::printf ("checks: %d, failed: %d\n", g_checks, g_failed);
    if (g_failed == 0) std::printf ("ALL CHECKS PASSED\n");
    return g_failed == 0 ? 0 : 1;
}
