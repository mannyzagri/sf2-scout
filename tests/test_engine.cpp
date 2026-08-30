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

struct Sample
{
    const char* name; std::vector<int16_t> pcm; uint32_t loopStart, loopEnd; uint32_t rate; uint8_t root;
    uint16_t sampleType = 1;     // shdr.sampleType: 1 mono, 2 right, 4 left (F11 stereo-pair test)
    uint16_t sampleLink = 0;     // shdr.sampleLink: paired sample index for linked stereo halves
    int64_t  endOverride = -1;  // F7 pool-bound test: if >= 0, write this as shdr.end instead of the
                                  // real PCM length -- a header that lies about how much data follows it
};

// generator opcodes we use (SF2 spec section 8.1.2)
enum {
    GEN_STARTADDRSOFFSET = 0,
    GEN_INSTRUMENT = 41, GEN_KEYRANGE = 43, GEN_VELRANGE = 44,
    GEN_COARSETUNE = 51, GEN_FINETUNE = 52, GEN_SAMPLEID = 53, GEN_SAMPLEMODES = 54,
    GEN_SCALETUNING = 56, GEN_ROOTKEY = 58
};

// One instrument-zone generator: {opcode, raw 2-byte amount}. A GEN_KEYRANGE/
// GEN_VELRANGE amount packs lo in the low byte and hi in the high byte (SF2's
// own range encoding); every other opcode's amount is just its int16/uint16
// bit pattern, so a single uint16_t representation covers all of them and a
// zone becomes DATA -- a vector of these -- instead of hand-counted byte
// offsets into igen/ibag (the old writer's index arithmetic F6/F7/F11's tests
// would otherwise have made unreadable).
struct GenOp { uint16_t oper; uint16_t amount; };
using ZoneDesc = std::vector<GenOp>;
GenOp keyRangeOp (int lo, int hi) { return { (uint16_t) GEN_KEYRANGE, (uint16_t) ((uint8_t) lo | ((unsigned) (uint8_t) hi << 8)) }; }
GenOp velRangeOp (int lo, int hi) { return { (uint16_t) GEN_VELRANGE, (uint16_t) ((uint8_t) lo | ((unsigned) (uint8_t) hi << 8)) }; }
GenOp genOp (uint16_t oper, int32_t value) { return { oper, (uint16_t) (int16_t) value }; }

// General SF2 writer: any sample set, any zone list (all zones in ONE
// instrument, ONE preset "bank 0 program 0"). Adding a zone or a generator to
// a test is now a data literal, not new index arithmetic.
std::vector<uint8_t> buildSf2 (std::vector<Sample>& samples, const std::vector<ZoneDesc>& zones, const char* presetName = "Scout Test")
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
    // one preset, bank 0 program 0, one pbag pointing to instrument 0
    phdr.name20 (presetName); phdr.u16 (0); phdr.u16 (0); phdr.u16 (0); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
    phdr.name20 ("EOP");      phdr.u16 (0); phdr.u16 (0); phdr.u16 (1); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
    pbag.u16 (0); pbag.u16 (0);   // bag 0: gens start 0
    pbag.u16 (1); pbag.u16 (0);   // terminal
    pgen.u16 (GEN_INSTRUMENT); pgen.u16 (0);
    pgen.u16 (0); pgen.u16 (0);   // terminal
    pmod.u16 (0); pmod.u16 (0); pmod.i16 (0); pmod.u16 (0); pmod.u16 (0); // terminal only

    // one instrument, N zones (data-driven -- see ZoneDesc above)
    inst.name20 ("ScoutInst"); inst.u16 (0);
    inst.name20 ("EOI");       inst.u16 ((uint16_t) zones.size());
    uint16_t genIdx = 0;
    for (auto& z : zones)
    {
        ibag.u16 (genIdx); ibag.u16 (0);
        for (auto& g : z) { igen.u16 (g.oper); igen.u16 (g.amount); }
        genIdx = (uint16_t) (genIdx + z.size());
    }
    ibag.u16 (genIdx); ibag.u16 (0);   // terminal
    igen.u16 (0); igen.u16 (0);        // terminal
    imod.u16 (0); imod.u16 (0); imod.i16 (0); imod.u16 (0); imod.u16 (0);

    for (size_t i = 0; i < samples.size(); ++i)
    {
        auto& s = samples[i];
        // F7: endOverride lets a test write a shdr.end that lies past the real
        // PCM this sample owns, to prove TSF's clamp (and ours) hold anyway.
        const uint32_t rawEnd = s.endOverride >= 0 ? (starts[i] + (uint32_t) s.endOverride) : ends[i];
        shdr.name20 (s.name);
        shdr.u32 (starts[i]); shdr.u32 (rawEnd);
        shdr.u32 (starts[i] + s.loopStart); shdr.u32 (starts[i] + s.loopEnd);
        shdr.u32 (s.rate); shdr.u8 (s.root); shdr.u8 (0); shdr.u16 (s.sampleLink); shdr.u16 (s.sampleType);
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

// The original two-zone layout ([load]..[swap] were all written against it),
// now expressed as data rather than hand-counted igen/ibag indices.
std::vector<uint8_t> buildTestSf2 (std::vector<Sample>& samples)
{
    std::vector<ZoneDesc> zones = {
        { keyRangeOp (0, 71),   genOp (GEN_SAMPLEMODES, 1), genOp (GEN_SAMPLEID, 0) },   // keys 0-71, looped sample 0
        { keyRangeOp (72, 127), genOp (GEN_SAMPLEMODES, 0), genOp (GEN_SAMPLEID, 1) },   // keys 72-127, one-shot sample 1
    };
    return buildSf2 (samples, zones);
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
        // F5: lastSeq_ is a seqlock now -- it advances by 2 per note-on
        // (odd mid-write, even once published), not 1. See ScoutEngine.h.
        CHECK (ln.note == 72 && ln.zoneIndex == 1 && ln.velocity == 127 && ln.sequence == 2);

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

    SECTION ("hostile-pitch");
    {
        // A zone whose generator VALUES are individually legal (SF2 allows
        // CoarseTune up to +-120 and ScaleTuning up to 1200) but whose COMBINED
        // effect at an extreme note is not: TSF does not clamp either of these
        // two fields on merge (genMetas rows 51 and 56 in tsf.h carry no
        // _GEN_LIMIT_MASK), so nothing upstream of startVoice's pitch formula
        // stops it. Before F1, note+transpose=247, keytrack=1200% would compute
        // an exponent of 2^187 -- baseStep would be +inf/NaN, and renderVoice's
        // old `while (pos >= loopEnd) pos -= length` would then spin the audio
        // thread forever trying to walk an infinite pos back into the loop.
        std::vector<Sample> hsamp = { { "HSample", sine (2000, 37.0), 100, 1900, 44100, 60 } };
        std::vector<ZoneDesc> hzones = {
            { keyRangeOp (0, 63),   genOp (GEN_SAMPLEMODES, 1), genOp (GEN_SAMPLEID, 0) },
            { keyRangeOp (64, 126), genOp (GEN_SAMPLEMODES, 0), genOp (GEN_SAMPLEID, 0) },
            // the third, hostile zone: legal-per-spec boundary values, note 127.
            // GEN_SAMPLEID must come LAST: tsf_load_presets resolves and pushes
            // the region the instant it sees SAMPLEID, so any generator listed
            // after it in the same zone would be silently dropped.
            { keyRangeOp (127, 127), genOp (GEN_SAMPLEMODES, 1),
              genOp (GEN_SCALETUNING, 1200), genOp (GEN_COARSETUNE, 120), genOp (GEN_SAMPLEID, 0) },
        };
        std::vector<uint8_t> hfile = buildSf2 (hsamp, hzones);
        std::string herr; auto hbank = SoundFontBank::load (hfile.data(), hfile.size(), herr);
        CHECK (hbank != nullptr);

        ScoutEngine e;
        e.prepare (44100.0);
        e.setBank (hbank.release());
        e.setPreset (0);
        e.noteOn (127, 127);
        auto out = render (e, 4096);          // must return -- proves no spin/hang
        CHECK (e.activeVoiceCount() <= ScoutEngine::kMaxVoices);
        // Not possible to inject a NaN from the file itself (every generator is
        // a plain int16), so this is the closest a black-box harness can get to
        // "the clamp held": every rendered sample stayed finite and inside a
        // sane amplitude envelope instead of the file's step overflowing to
        // +-inf/NaN and poisoning the whole buffer.
        float maxAbs = 0.0f;
        bool allFinite = true;
        for (float v : out) { allFinite = allFinite && std::isfinite (v); maxAbs = std::max (maxAbs, std::fabs (v)); }
        CHECK (allFinite);
        CHECK (maxAbs < 2.0f);

        // Direct unit test of the OTHER half of F1: SoundFontBank::load's own
        // clamp on transpose/keytrack/tuneCents, using generator values that
        // are themselves out of the SF2 spec's legal range (still representable
        // as a single int16 generator amount, so TSF happily stores them).
        std::vector<Sample> csamp = { { "CSample", sine (2000, 41.0), 100, 1900, 44100, 60 } };
        std::vector<ZoneDesc> czones = {
            { keyRangeOp (0, 127), genOp (GEN_SAMPLEMODES, 1),
              genOp (GEN_COARSETUNE, 32000), genOp (GEN_SCALETUNING, 32000), genOp (GEN_FINETUNE, 32000),
              genOp (GEN_SAMPLEID, 0) },   // SAMPLEID last -- see comment above
        };
        std::vector<uint8_t> cfile = buildSf2 (csamp, czones);
        std::string cerr; auto cbank = SoundFontBank::load (cfile.data(), cfile.size(), cerr);
        CHECK (cbank != nullptr);
        if (cbank != nullptr)
        {
            const Zone* cz = cbank->zoneForKey (0, 60);
            CHECK (cz != nullptr);
            if (cz != nullptr)
            {
                CHECK (cz->transpose <= 120 && cz->transpose >= -120);
                CHECK (cz->keytrack <= 1200 && cz->keytrack >= 0);
                CHECK (cz->tuneCents <= 12000.0 && cz->tuneCents >= -12000.0);
            }

            ScoutEngine e2;
            e2.prepare (44100.0);
            e2.setBank (cbank.release());
            e2.setPreset (0);
            e2.noteOn (60, 127);
            auto out2 = render (e2, 2048);
            bool finite2 = true; for (float v : out2) finite2 = finite2 && std::isfinite (v);
            CHECK (finite2);
            CHECK (e2.activeVoiceCount() <= ScoutEngine::kMaxVoices);
        }
    }

    SECTION ("seqlock");
    {
        std::string es; auto bankS = SoundFontBank::load (file.data(), file.size(), es);
        ScoutEngine e;
        e.prepare (44100.0);
        e.setBank (bankS.release());
        e.setPreset (0);
        e.noteOn (60, 100);
        e.noteOn (72, 90);                    // second note-on before any process() call
        auto ln = e.lastNote();
        CHECK ((ln.sequence % 2u) == 0u);     // never caught mid-write (an odd sequence)
        CHECK (ln.note == 72 && ln.velocity == 90 && ln.zoneIndex == 1);   // reflects the LAST note-on
        // repeated reads settle on the same consistent snapshot
        for (int k = 0; k < 20; ++k)
        {
            auto ln2 = e.lastNote();
            CHECK (ln2.sequence == ln.sequence && ln2.note == 72 && ln2.velocity == 90 && ln2.zoneIndex == 1);
        }
        // a third note-on bumps the sequence again (by 2: odd-then-even) and
        // publishes a new, fully consistent tuple
        e.noteOn (60, 50);
        auto ln3 = e.lastNote();
        CHECK (ln3.sequence == ln.sequence + 2);
        CHECK (ln3.note == 60 && ln3.velocity == 50 && ln3.zoneIndex == 0);
    }

    SECTION ("sample-id");
    {
        // Two real samples; a zone whose SAMPLEID names sample 1 but whose
        // startAddrsOffset generator makes the ABSOLUTE offset land inside
        // sample 0's [start,end) span. The old offset-based join
        // (shdrForRegion) would misidentify this as sample 0; F6 makes the
        // join trust tsf_region::sample_id (set from the SAMPLEID generator
        // itself) whenever it's valid, so the name follows sample_id, not
        // the offset.
        std::vector<Sample> ssamp = {
            { "SampA", sine (300, 60.0), 0, 0, 44100, 60 },   // pool [0, 300)
            { "SampB", sine (300, 45.0), 0, 0, 44100, 60 },   // pool [346, 646) (300 + 46 guard)
        };
        std::vector<ZoneDesc> szones = {
            // offset lands inside its OWN sample (unambiguous either way).
            // SAMPLEID last -- see the "SAMPLEID must come LAST" note above.
            { keyRangeOp (0, 63), genOp (GEN_SAMPLEMODES, 0), genOp (GEN_STARTADDRSOFFSET, 200), genOp (GEN_SAMPLEID, 0) },
            // SAMPLEID names sample 1 (SampB, starts at 346); offset -246 makes
            // the absolute offset 346-246=100 -- inside SampA's [0,300) span
            { keyRangeOp (64, 127), genOp (GEN_SAMPLEMODES, 0), genOp (GEN_STARTADDRSOFFSET, -246), genOp (GEN_SAMPLEID, 1) },
        };
        std::vector<uint8_t> sfile = buildSf2 (ssamp, szones);
        std::string serr; auto sbank = SoundFontBank::load (sfile.data(), sfile.size(), serr);
        CHECK (sbank != nullptr);
        if (sbank != nullptr)
        {
            const Zone* zx = sbank->zoneForKey (0, 30);
            const Zone* zy = sbank->zoneForKey (0, 100);
            CHECK (zx != nullptr && zy != nullptr);
            if (zx != nullptr)
            {
                CHECK (zx->sampleName == "SampA" && zx->sampleIndex == 0);
                CHECK (zx->playStart == zx->sampleStart + 200);
            }
            if (zy != nullptr)
            {
                // the key assertion: name/index follow sample_id (SampB), NOT
                // the misleading offset (which points into SampA's range)
                CHECK (zy->sampleName == "SampB");
                CHECK (zy->sampleIndex == 1);
            }
        }
    }

    SECTION ("stereo-pair");
    {
        // Two linked halves (sampleType 4 = left, 2 = right) both covering key
        // 96, pan left at its generator default (0) -- F11 says an untouched
        // stereo half hard-pans by sampleType instead of sitting centred.
        // Distinct, DC-free sine periods per side make channel bleed audible
        // to estimatePeriod(): if F11 were absent (or wrong), each channel
        // would be a mix of BOTH periods and the zero-crossing spacing would
        // not cleanly track either one.
        // root = 96 = played note, so pitch is native (1:1) and the periods
        // below survive into the render untouched.
        std::vector<Sample> psamp = {
            { "SampL", sine (4000, 70.0),  0, 0, 44100, 96, 4 /*left*/,  1 },
            { "SampR", sine (4000, 110.0), 0, 0, 44100, 96, 2 /*right*/, 0 },
        };
        std::vector<ZoneDesc> pzones = {
            { keyRangeOp (96, 96), genOp (GEN_SAMPLEMODES, 0), genOp (GEN_SAMPLEID, 0) },
            { keyRangeOp (96, 96), genOp (GEN_SAMPLEMODES, 0), genOp (GEN_SAMPLEID, 1) },
        };
        std::vector<uint8_t> pfile = buildSf2 (psamp, pzones);
        std::string perr; auto pbank = SoundFontBank::load (pfile.data(), pfile.size(), perr);
        CHECK (pbank != nullptr);
        if (pbank != nullptr)
        {
            const Zone* pz[8];
            CHECK (pbank->findZones (0, 96, 127, pz, 8) == 2);   // a stereo pair gives 2 zones

            ScoutEngine e;
            e.prepare (44100.0);
            e.setBank (pbank.release());
            e.setPreset (0);
            e.noteOn (96, 127);
            std::vector<float> outL ((size_t) 3000, 0.0f), outR ((size_t) 3000, 0.0f);
            for (int i = 0; i < 3000; i += 64)
            {
                const int m = std::min (64, 3000 - i);
                e.process (outL.data() + i, outR.data() + i, m, 1.0f);
            }
            CHECK (std::fabs (estimatePeriod (outL, 500, 3000) - 70.0) < 0.5);
            CHECK (std::fabs (estimatePeriod (outR, 500, 3000) - 110.0) < 0.5);
            float peakL = 0.0f, peakR = 0.0f;
            for (size_t i = 500; i < 3000; ++i) { peakL = std::max (peakL, std::fabs (outL[i])); peakR = std::max (peakR, std::fabs (outR[i])); }
            CHECK (peakL > 0.3f && peakL < 0.6f);
            CHECK (peakR > 0.3f && peakR < 0.6f);
        }
    }

    SECTION ("pool-bound");
    {
        // shdr.end lies far past the real smpl chunk. TSF clamps every
        // region's `end` to the true float pool size at load time
        // (tsf_load_presets' fontSampleCount), never to the header's claim;
        // F7 makes SoundFontBank::sampleCount() equal to THAT clamp, and
        // clamps every Zone position to it too, instead of trusting shdr.end.
        std::vector<Sample> lsamp = { { "LiarSamp", ramp (200), 0, 0, 44100, 60 } };
        lsamp[0].endOverride = 5000;   // claims 5000 samples; only 200 real + 46 guard exist
        std::vector<ZoneDesc> lzones = { { keyRangeOp (0, 127), genOp (GEN_SAMPLEMODES, 0), genOp (GEN_SAMPLEID, 0) } };
        std::vector<uint8_t> lfile = buildSf2 (lsamp, lzones);
        std::string lerr; auto lbank = SoundFontBank::load (lfile.data(), lfile.size(), lerr);
        CHECK (lbank != nullptr);
        if (lbank != nullptr)
        {
            // exactly the real pool (200 pcm + 46 guard), never the 5000 lie
            CHECK (lbank->sampleCount() == 246);
            const Zone* lz = lbank->zoneForKey (0, 60);
            CHECK (lz != nullptr);
            if (lz != nullptr)
            {
                CHECK (lz->playEnd <= lbank->sampleCount());
                CHECK (lz->sampleEnd <= lbank->sampleCount());
            }

            ScoutEngine e;
            e.prepare (44100.0);
            e.setBank (lbank.release());
            e.setPreset (0);
            e.noteOn (60, 127);
            auto out = render (e, 4000);   // far more than the real (clamped) sample length
            CHECK (e.activeVoiceCount() == 0);   // one-shot zone reached its clamped end and freed -- no crash, no runaway
            float head = 0.0f; for (size_t i = 0; i < 50; ++i) head = std::max (head, std::fabs (out[i]));
            CHECK (head > 0.0f);
        }
    }

    std::printf ("%d checks, %d failures\n", g_checks, g_failed);
    if (g_failed == 0) std::printf ("ALL CHECKS PASSED\n");
    return g_failed == 0 ? 0 : 1;
}
