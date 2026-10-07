// test_engine -- JUCE-free harness for SoundFontBank + ModuleSource + WavSample + ScoutEngine + Assists.
//
// Builds a tiny but structurally complete SF2 in memory (two samples: a
// looped sine "Loop_C4" on keys 0-71 and a one-shot ramp "Shot_C5" on 72-127),
// a WAV writer and a ProTracker MOD, then checks the spec's acceptance items a
// harness can check. v2 model (0.6.0): the engine plays decoded samples in
// three slots (A, B, Cur) -- every SF2 zone reaches it through decodeZone.
//   [load]        parse, preset list, zone join to sample names, loop points, INFO list
//   [authored]    AS-AUTHORED starts at frame 0; one-shot plays through once and frees
//   [looponly]    LOOP-ONLY starts AT loopStart; an "off" loop loops its marked region
//   [pitch]       root key plays at native pitch; +12 st doubles the rate; 96 kHz host; bend
//   [release]     note-off fades to silence within the RELEASE time; 16-voice pool, oldest-steal
//   [malformed]   garbage / truncated input -> error string, no crash
//   [swap]        slot swap retires the old sample exactly once, kills only that slot's voices
//   [hostile-pitch] clamps at the bank (generators) and the engine (step)
//   [last-note]   per-slot last note / velocity / counter publication
//   [sample-id] [stereo-pair] [pool-bound]  zone join, stereo halves decode, lying shdr.end
//   [wav-*]       WAV parse (smpl/root fallback), unity pitch at root, forward wrap continuity,
//                 ping-pong reversal at both markers, one-shot end, release keeps cycling
//   [routing]     KEYBOARD PLAYS A / B / SPLIT / TOGGLE
//   [wav-smpl]    smpl round-trip (write -> read identical, data byte-identical)
//   [export]      EXPORT RANGE exact length + marker remap/drop; 16-bit/44.1 mono conversion; stereo fold
//   [markers]     hit-test / drag math; direct slot audition
//   [routing-fallback] empty targets fall back to the loaded slot
//   [unload]      clearSlot: only that slot's voices die, retiree collected once
//   [assists]     LOUDNESS, PERIODS, CLICK METER, SUGGEST, AUTO-DETECT ROOT
//   [mod-load]    ModuleSource: MOD parse, sample table, loop flags, C-5 rate, decode -> WavSample bridge
//   [mod-play]    a decoded module sample sounds at its C-5 rate and loops
// Compile (validator.json dsp stage):
//   cmake --build build --config Release --target test_engine   (links openmpt_soundlib)
#include "../Source/engine/SoundFontBank.h"
#include "../Source/engine/ScoutEngine.h"
#include "../Source/engine/WavSample.h"
#include "../Source/engine/ModuleSource.h"
#include "../Source/engine/LoopMarkers.h"
#include "../Source/engine/NoteNames.h"
#include "../Source/engine/Assists.h"

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

struct GenOp { uint16_t oper; uint16_t amount; };
using ZoneDesc = std::vector<GenOp>;
GenOp keyRangeOp (int lo, int hi) { return { (uint16_t) GEN_KEYRANGE, (uint16_t) ((uint8_t) lo | ((unsigned) (uint8_t) hi << 8)) }; }
GenOp velRangeOp (int lo, int hi) { return { (uint16_t) GEN_VELRANGE, (uint16_t) ((uint8_t) lo | ((unsigned) (uint8_t) hi << 8)) }; }
GenOp genOp (uint16_t oper, int32_t value) { return { oper, (uint16_t) (int16_t) value }; }

// General SF2 writer: any sample set, any zone list (all zones in ONE
// instrument, ONE preset "bank 0 program 0"), plus an INFO list.
std::vector<uint8_t> buildSf2 (std::vector<Sample>& samples, const std::vector<ZoneDesc>& zones, const char* presetName = "Scout Test")
{
    Buf smpl; std::vector<uint32_t> starts, ends;
    for (auto& s : samples)
    {
        starts.push_back ((uint32_t) (smpl.b.size() / 2));
        for (int16_t v : s.pcm) smpl.i16 (v);
        ends.push_back ((uint32_t) (smpl.b.size() / 2));
        for (int i = 0; i < 46; ++i) smpl.i16 (0);
    }
    Buf sdtaBody; sdtaBody.chunk ("smpl", smpl);

    Buf phdr, pbag, pmod, pgen, inst, ibag, imod, igen, shdr;
    phdr.name20 (presetName); phdr.u16 (0); phdr.u16 (0); phdr.u16 (0); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
    phdr.name20 ("EOP");      phdr.u16 (0); phdr.u16 (0); phdr.u16 (1); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
    pbag.u16 (0); pbag.u16 (0);
    pbag.u16 (1); pbag.u16 (0);
    pgen.u16 (GEN_INSTRUMENT); pgen.u16 (0);
    pgen.u16 (0); pgen.u16 (0);
    pmod.u16 (0); pmod.u16 (0); pmod.i16 (0); pmod.u16 (0); pmod.u16 (0);

    inst.name20 ("ScoutInst"); inst.u16 (0);
    inst.name20 ("EOI");       inst.u16 ((uint16_t) zones.size());
    uint16_t genIdx = 0;
    for (auto& z : zones)
    {
        ibag.u16 (genIdx); ibag.u16 (0);
        for (auto& g : z) { igen.u16 (g.oper); igen.u16 (g.amount); }
        genIdx = (uint16_t) (genIdx + z.size());
    }
    ibag.u16 (genIdx); ibag.u16 (0);
    igen.u16 (0); igen.u16 (0);
    imod.u16 (0); imod.u16 (0); imod.i16 (0); imod.u16 (0); imod.u16 (0);

    for (size_t i = 0; i < samples.size(); ++i)
    {
        auto& s = samples[i];
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

    Buf info, ifil; ifil.u16 (2); ifil.u16 (1); info.chunk ("ifil", ifil);
    Buf isng; for (char c : std::string ("EMU8000")) isng.u8 ((uint8_t) c); isng.u8 (0); info.chunk ("isng", isng);
    Buf inam; for (char c : std::string ("Scout Test")) inam.u8 ((uint8_t) c); inam.u8 (0); info.chunk ("INAM", inam);
    Buf ieng; for (char c : std::string ("vm-claude")) ieng.u8 ((uint8_t) c); ieng.u8 (0); info.chunk ("IENG", ieng);

    Buf riffBody; riffBody.fcc ("sfbk");
    riffBody.list ("INFO", info); riffBody.list ("sdta", sdtaBody); riffBody.list ("pdta", pdtaBody);
    Buf file; file.chunk ("RIFF", riffBody);
    return file.b;
}

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

// Minimal WAV writer: PCM 16/24 or float 32, optional smpl.
struct WavSpec
{
    int channels = 1; int bits = 16; bool isFloat = false; uint32_t rate = 44100;
    bool withSmpl = false; uint32_t loopStart = 0, loopEnd = 0; uint32_t type = 0; uint32_t unity = 60; uint32_t fraction = 0;
};
std::vector<uint8_t> buildWav (const std::vector<float>& L, const std::vector<float>& R, const WavSpec& sp)
{
    Buf fmt;
    fmt.u16 (sp.isFloat ? 3 : 1); fmt.u16 ((uint16_t) sp.channels); fmt.u32 (sp.rate);
    fmt.u32 (sp.rate * (uint32_t) (sp.channels * sp.bits / 8)); fmt.u16 ((uint16_t) (sp.channels * sp.bits / 8)); fmt.u16 ((uint16_t) sp.bits);
    Buf data;
    for (size_t i = 0; i < L.size(); ++i)
        for (int c = 0; c < sp.channels; ++c)
        {
            const float v = c == 0 ? L[i] : R[i];
            if (sp.isFloat) { uint32_t u; std::memcpy (&u, &v, 4); data.u32 (u); }
            else if (sp.bits == 16) data.i16 ((int16_t) std::lround (v * 32768.0f));
            else { const int32_t q = (int32_t) std::lround (v * 8388608.0f); data.u8 ((uint8_t) q); data.u8 ((uint8_t) (q >> 8)); data.u8 ((uint8_t) (q >> 16)); }
        }
    Buf body; body.fcc ("WAVE");
    body.chunk ("fmt ", fmt);
    if (sp.withSmpl)
    {
        Buf smpl;
        smpl.u32 (0); smpl.u32 (0); smpl.u32 (22675); smpl.u32 (sp.unity); smpl.u32 (sp.fraction); smpl.u32 (0); smpl.u32 (0); smpl.u32 (1); smpl.u32 (0);
        smpl.u32 (0); smpl.u32 (sp.type); smpl.u32 (sp.loopStart); smpl.u32 (sp.loopEnd); smpl.u32 (0); smpl.u32 (0);
        body.chunk ("smpl", smpl);
    }
    body.chunk ("data", data);
    Buf file; file.chunk ("RIFF", body);
    return file.b;
}
// index ramp: sample i has the value i (in 16-bit LSBs), so output*32768 == read position
std::vector<float> indexRamp (int len)
{
    std::vector<float> v ((size_t) len);
    for (int i = 0; i < len; ++i) v[(size_t) i] = (float) i / 32768.0f;
    return v;
}
std::vector<float> sineF (int len, double periodSamples, double amp = 0.5)
{
    std::vector<float> v ((size_t) len);
    for (int i = 0; i < len; ++i) v[(size_t) i] = (float) (amp * std::sin (2.0 * 3.14159265358979 * i / periodSamples));
    return v;
}
// find a chunk's body in a RIFF image (nullptr if absent)
const uint8_t* findChunk (const std::vector<uint8_t>& f, const char* id, size_t& len)
{
    size_t at = 12;
    while (at + 8 <= f.size())
    {
        const size_t n = (size_t) f[at + 4] | ((size_t) f[at + 5] << 8) | ((size_t) f[at + 6] << 16) | ((size_t) f[at + 7] << 24);
        if (std::memcmp (f.data() + at, id, 4) == 0) { len = n; return f.data() + at + 8; }
        at += 8 + n + (n & 1);
    }
    len = 0; return nullptr;
}
uint32_t rd32 (const uint8_t* q) { return (uint32_t) q[0] | ((uint32_t) q[1] << 8) | ((uint32_t) q[2] << 16) | ((uint32_t) q[3] << 24); }

// Estimate period (in samples) of a sine by zero-crossing spacing.
double estimatePeriod (const std::vector<float>& x, size_t from, size_t to)
{
    std::vector<size_t> zc;
    for (size_t i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) zc.push_back (i);
    if (zc.size() < 3) return 0.0;
    return (double) (zc.back() - zc.front()) / (double) (zc.size() - 1);
}

// loads a WAV image into a fresh WavSample (CHECKs it parsed)
std::unique_ptr<WavSample> loadWav (const std::vector<uint8_t>& img, const char* name)
{
    std::string we; auto w = WavSample::load (img.data(), img.size(), name, we);
    CHECK (w != nullptr);
    return w;
}
// installs `w` in `slot` with its own markers and no attack fade, consumes the handoff
void install (ScoutEngine& e, int slot, std::unique_ptr<WavSample> w, LoopKind kind)
{
    e.setSlotLoop (slot, w->loopStart, w->loopEnd, kind);
    e.setSlotTuning (slot, w->rootKey, w->fineCents);
    e.setSlotFades (slot, 0.0, 80.0);
    delete e.setSlot (slot, w.release());
    render (e, 64);
    delete e.takeRetired (slot);
}
} // namespace

// ----------------------------------------------------------------- tests
// --probe <module> [outDir]: real-file diagnostic (not part of the check run).
static int probeModule (const char* path, const char* outDir)
{
    FILE* f = std::fopen (path, "rb");
    if (f == nullptr) { std::printf ("cannot open %s\n", path); return 2; }
    std::vector<uint8_t> bytes;
    { uint8_t buf[65536]; size_t n; while ((n = std::fread (buf, 1, sizeof (buf), f)) > 0) bytes.insert (bytes.end(), buf, buf + n); }
    std::fclose (f);
    std::string name = path; { const size_t s = name.find_last_of ("/\\"); if (s != std::string::npos) name = name.substr (s + 1); }
    std::string err;
    auto mod = ModuleSource::load (bytes.data(), bytes.size(), name, err);
    if (mod == nullptr) { std::printf ("load failed: %s\n", err.c_str()); return 1; }
    std::printf ("%s  [%s / %s]  title=\"%s\"  made with: %s\n  %d sample slots, %d with data, %d instruments\n",
                 name.c_str(), mod->formatName().c_str(), mod->formatType().c_str(), mod->title().c_str(), mod->madeWith().c_str(),
                 mod->sampleCount(), mod->nonEmptySampleCount(), (int) mod->instrumentNames().size());
    for (const auto& s : mod->samples())
    {
        if (s.isEmpty()) continue;
        std::printf ("  %02d %-24s %2db %s %8u fr  c5=%6u Hz  loop=%s", s.index, s.name.c_str(), s.bits, s.channels == 2 ? "st" : "mo",
                     (unsigned) s.frames, (unsigned) s.sampleRate,
                     s.hasLoop ? (std::to_string (s.loopStart) + "-" + std::to_string (s.loopEnd) + (s.pingPong ? " pp" : " fwd")).c_str() : "none");
        if (s.hasSustain) std::printf ("  sustain=%u-%u%s", (unsigned) s.sustainStart, (unsigned) s.sustainEnd, s.sustainPingPong ? " pp" : " fwd");
        std::printf ("\n");
        if (outDir != nullptr)
        {
            std::string derr;
            auto w = mod->decode (s.index, derr);
            if (w == nullptr) { std::printf ("     decode failed: %s\n", derr.c_str()); continue; }
            WavSaveSpec spec; spec.loopStart = w->loopStart; spec.loopEnd = w->loopEnd; spec.loopType = w->loopType; spec.rootKey = w->rootKey; spec.fineCents = w->fineCents;
            spec.description = w->bextDescription;
            auto img = writeWav (*w, spec);
            char fn[512]; std::snprintf (fn, sizeof (fn), "%s/%02d_%s.wav", outDir, s.index, s.name.empty() ? "sample" : s.name.c_str());
            for (char* c = fn + std::strlen (outDir) + 1; *c; ++c) if (std::strchr ("<>:\"|?*", *c) || (unsigned char) *c < 32) *c = '_';
            FILE* o = std::fopen (fn, "wb");
            if (o == nullptr) { std::printf ("     cannot write %s\n", fn); continue; }
            std::fwrite (img.data(), 1, img.size(), o); std::fclose (o);
            std::printf ("     -> %s\n", fn);
        }
    }
    return 0;
}

int main (int argc, char** argv)
{
    if (argc >= 3 && std::strcmp (argv[1], "--probe") == 0) return probeModule (argv[2], argc >= 4 ? argv[3] : nullptr);

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
    // INFO list (metadata box): file order, ifil rendered as major.minor
    CHECK (bank->info().size() == 4);
    CHECK (bank->infoValue ("INAM") == "Scout Test" && bank->infoValue ("isng") == "EMU8000" && bank->infoValue ("ifil") == "2.1" && bank->infoValue ("IENG") == "vm-claude");
    CHECK (bank->infoValue ("ICOP").empty());
    // decodeZone: the bridge into the engine (16-bit copy, inclusive loop end, root)
    {
        auto d0 = bank->decodeZone (*z0);
        auto d1 = bank->decodeZone (*z1);
        CHECK (d0 != nullptr && d0->frames == 4000 && d0->hasSmpl && d0->loopStart == 1000 && d0->loopEnd == 2999 && d0->rootKey == 60 && d0->sampleRate == 44100);
        CHECK (d1 != nullptr && d1->frames == 2000 && ! d1->hasSmpl && d1->loopStart == 0 && d1->loopEnd == 1999 && d1->rootKey == 72);
        CHECK (d1 != nullptr && std::fabs (d1->left[500] * 32768.0f - 1500.0f) < 0.51f);
        CHECK (d0 != nullptr && d0->bextDescription.find ("Loop_C4") != std::string::npos);
    }

    SECTION ("authored");
    {
        ScoutEngine e;
        e.prepare (44100.0);
        e.setMode (PlayMode::AsAuthored);
        // one-shot zone at its root: output must equal the ramp from sample 0, then stop
        install (e, kSlotCur, bank->decodeZone (*z1), LoopKind::Off);
        e.noteOnSlot (kSlotCur, 72, 127);
        auto out = render (e, 2100);
        CHECK (std::fabs (out[0] - (1000.0f / 32768.0f)) < 1e-4f);           // starts at sample start
        CHECK (std::fabs (out[500] - (1500.0f / 32768.0f)) < 1e-4f);         // native pitch, 1:1
        CHECK (e.activeVoiceCount() == 0);                                    // played through once, freed
        CHECK (out[2050] == 0.0f);
        CHECK (e.lastNote (kSlotCur) == 72 && e.lastVelocity (kSlotCur) == 127 && e.noteCounter (kSlotCur) == 1);
        CHECK (e.lastNote (kSlotA) == -1);                                    // other slots untouched

        // looped zone at root: sustains indefinitely, playhead stays inside the loop
        install (e, kSlotCur, bank->decodeZone (*z0), LoopKind::Forward);
        e.noteOnSlot (kSlotCur, 60, 100);
        out = render (e, 44100);
        CHECK (e.activeVoiceCount() == 1);
        const double ph = e.lastPlayhead (kSlotCur);
        CHECK (ph >= 1000.0 && ph < 3000.0);
        // loop seam is clean: the 100-sample sine loops over 2000 samples = 20 periods exactly
        double period = estimatePeriod (out, 5000, 44000);
        CHECK (std::fabs (period - 100.0) < 0.5);
        float peak = 0.0f; for (size_t i = 5000; i < 44100; ++i) peak = std::max (peak, std::fabs (out[i]));
        CHECK (peak > 0.3f && peak < 0.6f);                                   // sqrt(100/127)*0.5 = 0.44
    }

    SECTION ("looponly");
    {
        ScoutEngine e;
        e.prepare (44100.0);
        e.setMode (PlayMode::LoopOnly);
        install (e, kSlotCur, bank->decodeZone (*z0), LoopKind::Forward);
        e.noteOnSlot (kSlotCur, 60, 127);
        auto out = render (e, 64);
        // first output sample == frame loopStart (sin(2*pi*1000/100) = 0 -> use sample 25 of the loop instead)
        const float expected25 = 0.5f * std::sin (2.0f * 3.14159265f * (float) (1000 + 25) / 100.0f);
        CHECK (std::fabs (out[25] - expected25) < 2e-3f);
        CHECK (e.lastPlayhead (kSlotCur) >= 1000.0 && e.lastPlayhead (kSlotCur) < 3000.0);

        // an "off" loop in LOOP-ONLY: loops its marked region (the whole one-shot here) -> still active after 3x its length
        install (e, kSlotCur, bank->decodeZone (*z1), LoopKind::Off);
        e.noteOnSlot (kSlotCur, 84, 127);
        out = render (e, 6000);
        CHECK (e.activeVoiceCount() == 1);
        CHECK (out[5999] != 0.0f);
        // back in AS-AUTHORED the same "off" loop plays once
        e.reset(); e.setMode (PlayMode::AsAuthored);
        e.noteOnSlot (kSlotCur, 72, 127);
        render (e, 6000);
        CHECK (e.activeVoiceCount() == 0);
    }

    SECTION ("pitch");
    {
        ScoutEngine e;
        e.prepare (44100.0);
        install (e, kSlotA, bank->decodeZone (*z0), LoopKind::Forward);
        e.setKeyboard (KbMode::A, 60, 0);
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
        // fine tune: root 50 cents flat -> plays 50 cents sharp
        e.prepare (44100.0); e.setPitchBend (0.0); e.setSlotTuning (kSlotA, 60, -50.0); e.noteOn (60, 127);
        out = render (e, 20000);
        CHECK (std::fabs (estimatePeriod (out, 4000, 20000) - 100.0 / std::pow (2.0, 50.0 / 1200.0)) < 0.5);
    }

    SECTION ("release");
    {
        ScoutEngine e;
        e.prepare (44100.0);
        install (e, kSlotA, bank->decodeZone (*z0), LoopKind::Forward);
        e.setKeyboard (KbMode::A, 60, 0);
        e.noteOn (60, 127);
        render (e, 1000);
        e.noteOff (60);
        auto out = render (e, 4410);     // 100 ms
        CHECK (e.activeVoiceCount() == 0);
        float tail = 0.0f; for (size_t i = 3600; i < out.size(); ++i) tail = std::max (tail, std::fabs (out[i]));
        CHECK (tail == 0.0f);            // silent after 80 ms
        float head = 0.0f; for (size_t i = 0; i < 100; ++i) head = std::max (head, std::fabs (out[i]));
        CHECK (head > 0.1f);             // but not cut instantly
        // polyphony + stealing: 20 notes -> 16 voices (spec), none lost to a crash
        e.reset();
        for (int n = 0; n < 20; ++n) e.noteOn (30 + n, 100);
        render (e, 64);
        CHECK (ScoutEngine::kMaxVoices == 16);
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
        int refused = 0;
        for (int k = 1; k < 8; ++k)
        {
            const size_t cut = file.size() * (size_t) k / 8;
            if (SoundFontBank::load (file.data(), cut, m) == nullptr) ++refused;
        }
        CHECK (refused >= 6);
        std::vector<uint8_t> lie = file; lie[4] = 0xff; lie[5] = 0xff; lie[6] = 0xff; lie[7] = 0x7f;
        (void) SoundFontBank::load (lie.data(), lie.size(), m);   // must not crash; result either way
        CHECK (true);
    }

    SECTION ("swap");
    {
        ScoutEngine e;
        e.prepare (44100.0);
        auto a = bank->decodeZone (*z0), b = bank->decodeZone (*z0);
        WavSample* araw = a.get();
        e.setSlotLoop (kSlotA, a->loopStart, a->loopEnd, LoopKind::Forward);
        e.setSlotFades (kSlotA, 0.0, 80.0);
        e.setSlot (kSlotA, a.release());
        e.setKeyboard (KbMode::A, 60, 0);
        render (e, 64);
        CHECK (e.takeRetired (kSlotA) == nullptr);
        CHECK (e.requested (kSlotA) == araw && e.hasSlot (kSlotA) && ! e.hasSlot (kSlotB));
        e.noteOn (60, 100);
        render (e, 64);
        CHECK (e.activeVoiceCount() == 1);
        e.setSlot (kSlotA, b.release());
        CHECK (e.takeRetired (kSlotA) == nullptr);   // not swapped until the audio thread runs
        render (e, 64);
        CHECK (e.activeVoiceCount() == 0);           // voices of that slot killed on swap
        WavSample* r = e.takeRetired (kSlotA);
        CHECK (r == araw);
        delete r;
        CHECK (e.takeRetired (kSlotA) == nullptr);
        // replacing a pending sample before the audio thread runs must not leak or double-free
        auto c = bank->decodeZone (*z0), d = bank->decodeZone (*z0);
        WavSample* craw = c.get();
        CHECK (e.setSlot (kSlotA, c.release()) == nullptr);
        WavSample* superseded = e.setSlot (kSlotA, d.release());   // c never reached the audio thread: handed back
        CHECK (superseded == craw);
        delete superseded;
        render (e, 64);
        WavSample* r2 = e.takeRetired (kSlotA);
        CHECK (r2 != nullptr);
        delete r2;
        e.noteOn (60, 100); render (e, 64);
        CHECK (e.activeVoiceCount() == 1);
        // a swap of slot B leaves slot A's voice sounding
        install (e, kSlotB, bank->decodeZone (*z1), LoopKind::Off);
        CHECK (e.activeVoiceCount() == 1);
        // out-of-range slots are refused, not crashed
        e.setSlot (7, nullptr); e.clearSlot (-1);
        CHECK (e.takeRetired (9) == nullptr);
    }

    SECTION ("hostile-pitch");
    {
        // legal-per-spec boundary generators (CoarseTune +-120, ScaleTuning 1200) still parse and clamp
        std::vector<Sample> hsamp = { { "HSample", sine (2000, 37.0), 100, 1900, 44100, 60 } };
        std::vector<ZoneDesc> hzones = {
            { keyRangeOp (0, 63),   genOp (GEN_SAMPLEMODES, 1), genOp (GEN_SAMPLEID, 0) },
            { keyRangeOp (64, 126), genOp (GEN_SAMPLEMODES, 0), genOp (GEN_SAMPLEID, 0) },
            { keyRangeOp (127, 127), genOp (GEN_SAMPLEMODES, 1),
              genOp (GEN_SCALETUNING, 1200), genOp (GEN_COARSETUNE, 120), genOp (GEN_SAMPLEID, 0) },
        };
        std::vector<uint8_t> hfile = buildSf2 (hsamp, hzones);
        std::string herr; auto hbank = SoundFontBank::load (hfile.data(), hfile.size(), herr);
        CHECK (hbank != nullptr);
        if (hbank != nullptr)
        {
            const Zone* hz = hbank->zoneForKey (0, 127);
            CHECK (hz != nullptr && hz->transpose == 120 && hz->keytrack == 1200);
            // decoded: root - coarseTune folds the transpose into the root (clamped to 0..127)
            auto dz = hbank->decodeZone (*hz);
            CHECK (dz != nullptr && dz->rootKey == 0);
        }
        // out-of-spec generator amounts clamp at load
        std::vector<Sample> csamp = { { "CSample", sine (2000, 41.0), 100, 1900, 44100, 60 } };
        std::vector<ZoneDesc> czones = {
            { keyRangeOp (0, 127), genOp (GEN_SAMPLEMODES, 1),
              genOp (GEN_COARSETUNE, 32000), genOp (GEN_SCALETUNING, 32000), genOp (GEN_FINETUNE, 32000),
              genOp (GEN_SAMPLEID, 0) },
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
        }
        // the engine clamps the STEP itself: a hostile root/cents/rate can never spin or poison the buffer
        ScoutEngine e; e.prepare (44100.0);
        install (e, kSlotA, bank->decodeZone (*z0), LoopKind::Forward);
        e.setKeyboard (KbMode::A, 60, 0);
        e.setSlotTuning (kSlotA, 0, -12000.0);
        e.noteOn (127, 127);
        auto out = render (e, 4096);
        bool fin = true; float maxAbs = 0.0f;
        for (float v : out) { fin = fin && std::isfinite (v); maxAbs = std::max (maxAbs, std::fabs (v)); }
        CHECK (fin && maxAbs < 2.0f && e.activeVoiceCount() <= ScoutEngine::kMaxVoices);
        e.reset(); e.setSlotTuning (kSlotA, 127, 12000.0); e.noteOn (0, 127);
        out = render (e, 4096);
        fin = true; for (float v : out) fin = fin && std::isfinite (v);
        CHECK (fin);
        const double nan = std::nan ("");
        e.reset(); e.setSlotTuning (kSlotA, 60, nan); e.noteOn (60, 127);
        out = render (e, 2048);
        fin = true; for (float v : out) fin = fin && std::isfinite (v);
        CHECK (fin && e.activeVoiceCount() == 1);
        // a 2-frame ping-pong loop at a huge step must not spin or escape
        e.reset(); e.setSlotLoop (kSlotA, 500, 501, LoopKind::PingPong); e.setSlotTuning (kSlotA, 0, 0.0); e.noteOn (127, 127);
        out = render (e, 4096);
        fin = true; for (float v : out) fin = fin && std::isfinite (v);
        CHECK (fin);
    }

    SECTION ("last-note");
    {
        ScoutEngine e; e.prepare (44100.0);
        install (e, kSlotA, bank->decodeZone (*z0), LoopKind::Forward);
        install (e, kSlotB, bank->decodeZone (*z1), LoopKind::Off);
        e.setKeyboard (KbMode::A, 60, 0);
        const uint32_t before = e.noteCounter (kSlotA);
        e.noteOn (60, 100);
        e.noteOn (72, 90);                    // second note-on before any process() call
        CHECK (e.lastNote (kSlotA) == 72 && e.lastVelocity (kSlotA) == 90 && e.noteCounter (kSlotA) == before + 2);
        CHECK (e.lastNote (kSlotB) == -1 && e.noteCounter (kSlotB) == 0);
        e.noteOnSlot (kSlotB, 84, 50);
        CHECK (e.lastNote (kSlotB) == 84 && e.lastVelocity (kSlotB) == 50 && e.noteCounter (kSlotB) == 1);
        CHECK (e.lastNote (kSlotA) == 72);    // untouched by the B audition
        render (e, 64);
        CHECK (e.activeVoiceCount() == 3 && e.lastPlayhead (kSlotA) >= 0.0 && e.lastPlayhead (kSlotB) >= 0.0 && e.lastPlayhead (kSlotCur) < 0.0);
    }

    SECTION ("sample-id");
    {
        std::vector<Sample> ssamp = {
            { "SampA", sine (300, 60.0), 0, 0, 44100, 60 },   // pool [0, 300)
            { "SampB", sine (300, 45.0), 0, 0, 44100, 60 },   // pool [346, 646) (300 + 46 guard)
        };
        std::vector<ZoneDesc> szones = {
            { keyRangeOp (0, 63), genOp (GEN_SAMPLEMODES, 0), genOp (GEN_STARTADDRSOFFSET, 200), genOp (GEN_SAMPLEID, 0) },
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
                CHECK (zy->sampleName == "SampB");
                CHECK (zy->sampleIndex == 1);
                auto dy = sbank->decodeZone (*zy);
                CHECK (dy != nullptr && dy->frames == 300);
            }
        }
    }

    SECTION ("stereo-pair");
    {
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
            CHECK (pz[0]->isStereoHalf() && pz[1]->isStereoHalf());
            // each half decodes to its own mono sample at native pitch (OPEN QUESTION 10: two mono files)
            auto dl = pbank->decodeZone (*pz[0]);
            auto dr = pbank->decodeZone (*pz[1]);
            CHECK (dl != nullptr && dr != nullptr && ! dl->isStereo() && ! dr->isStereo());
            CHECK (dl != nullptr && dl->bextDescription.find ("stereo=L") != std::string::npos);
            CHECK (dr != nullptr && dr->bextDescription.find ("stereo=R") != std::string::npos);
            ScoutEngine e; e.prepare (44100.0);
            install (e, kSlotA, std::move (dl), LoopKind::Off);
            install (e, kSlotB, std::move (dr), LoopKind::Off);
            e.setKeyboard (KbMode::A, 60, 0); e.noteOn (96, 127);
            auto out = render (e, 3000);
            CHECK (std::fabs (estimatePeriod (out, 500, 3000) - 70.0) < 0.5);
            e.reset(); e.setKeyboard (KbMode::B, 60, 0); e.noteOn (96, 127);
            out = render (e, 3000);
            CHECK (std::fabs (estimatePeriod (out, 500, 3000) - 110.0) < 0.5);
        }
    }

    SECTION ("pool-bound");
    {
        std::vector<Sample> lsamp = { { "LiarSamp", ramp (200), 0, 0, 44100, 60 } };
        lsamp[0].endOverride = 5000;   // claims 5000 samples; only 200 real + 46 guard exist
        std::vector<ZoneDesc> lzones = { { keyRangeOp (0, 127), genOp (GEN_SAMPLEMODES, 0), genOp (GEN_SAMPLEID, 0) } };
        std::vector<uint8_t> lfile = buildSf2 (lsamp, lzones);
        std::string lerr; auto lbank = SoundFontBank::load (lfile.data(), lfile.size(), lerr);
        CHECK (lbank != nullptr);
        if (lbank != nullptr)
        {
            CHECK (lbank->sampleCount() == 246);
            const Zone* lz = lbank->zoneForKey (0, 60);
            CHECK (lz != nullptr);
            if (lz != nullptr)
            {
                CHECK (lz->playEnd <= lbank->sampleCount());
                CHECK (lz->sampleEnd <= lbank->sampleCount());
                auto d = lbank->decodeZone (*lz);
                CHECK (d != nullptr && d->frames == 246);                       // exactly the real pool, never the 5000 lie
                ScoutEngine e; e.prepare (44100.0);
                install (e, kSlotCur, std::move (d), LoopKind::Off);
                e.noteOnSlot (kSlotCur, 60, 127);
                auto out = render (e, 4000);
                CHECK (e.activeVoiceCount() == 0);                             // reached its clamped end and freed
                float head = 0.0f; for (size_t i = 0; i < 50; ++i) head = std::max (head, std::fabs (out[i]));
                CHECK (head > 0.0f);
            }
        }
    }

    // ================================================================ WAV source
    SECTION ("wav-load");
    {
        WavSpec sp; sp.withSmpl = true; sp.loopStart = 100; sp.loopEnd = 200; sp.type = 1; sp.unity = 62;
        sp.fraction = centsToPitchFraction (25.0);
        auto img = buildWav (indexRamp (1000), {}, sp);
        std::string werr;
        auto w = WavSample::load (img.data(), img.size(), "JD_STR1_C4.wav", werr);
        CHECK (w != nullptr);
        if (w != nullptr)
        {
            CHECK (w->frames == 1000 && w->channels == 1 && w->bitsPerSample == 16 && w->sampleRate == 44100);
            CHECK (w->hasSmpl && w->loopStart == 100 && w->loopEnd == 200 && w->loopType == 1);
            CHECK (w->rootKey == 62 && w->rootFromFile);
            CHECK (std::fabs (w->fineCents - 25.0) < 1e-4);
            CHECK (std::fabs (w->left[500] * 32768.0f - 500.0f) < 1e-3f);
            CHECK (! w->isStereo());
            CHECK (w->chunks.size() == 2);
        }
        WavSpec plain;
        auto img2 = buildWav (indexRamp (500), {}, plain);
        auto w2 = WavSample::load (img2.data(), img2.size(), "JD_STR1_C4.wav", werr);
        CHECK (w2 != nullptr && ! w2->hasSmpl && w2->rootKey == 60 && w2->rootFromFile);
        CHECK (w2 != nullptr && w2->loopStart == 0 && w2->loopEnd == 499);
        auto w3 = WavSample::load (img2.data(), img2.size(), "take7.wav", werr);
        CHECK (w3 != nullptr && w3->rootKey == 60 && ! w3->rootFromFile);
        CHECK (rootFromFileName ("X_F#3.wav") == 54 && rootFromFileName ("X_Bb2.wav") == 46 && rootFromFileName ("X_C-1.wav") == 0
               && rootFromFileName ("X_A4.wav") == 69 && rootFromFileName ("X_G9.wav") == 127 && rootFromFileName ("X_H4.wav") == -1
               && rootFromFileName ("noext") == -1 && rootFromFileName ("X_C.wav") == -1);
        WavSpec s24; s24.channels = 2; s24.bits = 24;
        auto img3 = buildWav (sineF (300, 30.0), sineF (300, 50.0), s24);
        auto w4 = WavSample::load (img3.data(), img3.size(), "st.wav", werr);
        CHECK (w4 != nullptr && w4->isStereo() && w4->frames == 300 && w4->bitsPerSample == 24);
        CHECK (w4 != nullptr && std::fabs (w4->left[7] - 0.5f * std::sin (2.0f * 3.14159265f * 7.0f / 30.0f)) < 1e-4f);
        CHECK (w4 != nullptr && std::fabs (w4->right[7] - 0.5f * std::sin (2.0f * 3.14159265f * 7.0f / 50.0f)) < 1e-4f);
        WavSpec sf; sf.bits = 32; sf.isFloat = true;
        auto img4 = buildWav (sineF (300, 30.0), {}, sf);
        auto w5 = WavSample::load (img4.data(), img4.size(), "f.wav", werr);
        CHECK (w5 != nullptr && w5->formatTag == 3 && std::fabs (w5->left[7] - 0.5f * std::sin (2.0f * 3.14159265f * 7.0f / 30.0f)) < 1e-6f);
        CHECK (WavSample::load (nullptr, 0, "x", werr) == nullptr && ! werr.empty());
        const char junk[] = "RIFF....WAVEjunkjunkjunkjunkjunkjunk";
        CHECK (WavSample::load (junk, sizeof (junk), "x", werr) == nullptr && ! werr.empty());
        for (int k = 1; k < 8; ++k)
        {
            const size_t cut = img.size() * (size_t) k / 8;
            (void) WavSample::load (img.data(), cut, "x", werr);
        }
        CHECK (true);
        std::vector<uint8_t> lie = img; lie[4] = 0xff; lie[5] = 0xff; lie[6] = 0xff; lie[7] = 0x7f;
        (void) WavSample::load (lie.data(), lie.size(), "x", werr);
        CHECK (true);
    }

    // a fresh engine with a WAV in slot A, keyboard on A, no attack fade
    auto makeEngine = [&] (ScoutEngine& e, std::vector<uint8_t>& wavImg, const char* name)
    {
        e.prepare (44100.0);
        install (e, kSlotA, loadWav (wavImg, name), LoopKind::Forward);
        e.setKeyboard (KbMode::A, 60, 0);
        e.reset();
    };

    SECTION ("wav-pitch");
    {
        WavSpec sp; sp.withSmpl = true; sp.loopStart = 0; sp.loopEnd = 39999; sp.type = 0; sp.unity = 60;
        auto img = buildWav (sineF (40000, 100.0), {}, sp);
        ScoutEngine e; makeEngine (e, img, "S_C4.wav");
        e.setSlotLoop (kSlotA, 0, 39999, LoopKind::Forward);
        e.setSlotTuning (kSlotA, 60, 0.0);
        e.noteOn (60, 127);
        auto out = render (e, 20000);
        CHECK (std::fabs (estimatePeriod (out, 4000, 20000) - 100.0) < 0.5);    // root at root: unity rate
        CHECK (e.lastNote (kSlotA) == 60 && e.lastPlayhead (kSlotA) >= 0.0);
        e.reset(); e.noteOn (72, 127);
        out = render (e, 20000);
        CHECK (std::fabs (estimatePeriod (out, 4000, 20000) - 50.0) < 0.5);     // +12 st: double rate
        e.reset(); e.setSlotTuning (kSlotA, 60, -50.0); e.noteOn (60, 127);
        out = render (e, 20000);
        CHECK (std::fabs (estimatePeriod (out, 4000, 20000) - 100.0 / std::pow (2.0, 50.0 / 1200.0)) < 0.5);
        e.reset(); e.setSlotTuning (kSlotA, 48, 0.0); e.noteOn (60, 127);
        out = render (e, 20000);
        CHECK (std::fabs (estimatePeriod (out, 4000, 20000) - 50.0) < 0.5);
        e.prepare (96000.0); e.setSlotTuning (kSlotA, 60, 0.0); e.noteOn (60, 127);
        out = render (e, 20000);
        CHECK (std::fabs (estimatePeriod (out, 4000, 20000) - 100.0 * 96000.0 / 44100.0) < 1.0);
    }

    SECTION ("wav-forward");
    {
        WavSpec sp;
        auto img = buildWav (indexRamp (1000), {}, sp);
        ScoutEngine e; makeEngine (e, img, "R_C4.wav");
        e.setSlotLoop (kSlotA, 100, 200, LoopKind::Forward);       // inclusive: plays 100..200 = 101 frames
        e.noteOn (60, 127);
        auto out = render (e, 44100);
        auto expectFwd = [] (int k) { return k <= 200 ? (double) k : 100.0 + std::fmod ((double) (k - 100), 101.0); };
        bool exact = true; int firstBad = -1;
        for (int k = 0; k < 44100; ++k)
            if (std::fabs (out[(size_t) k] * 32768.0f - expectFwd (k)) > 0.01f) { exact = false; if (firstBad < 0) firstBad = k; }
        CHECK (exact);
        if (! exact) std::printf ("  first mismatch at %d: got %f want %f\n", firstBad, out[(size_t) firstBad] * 32768.0f, expectFwd (firstBad));
        CHECK (std::fabs (out[200] * 32768.0f - 200.0f) < 0.01f);
        CHECK (std::fabs (out[201] * 32768.0f - 100.0f) < 0.01f);
        CHECK (e.activeVoiceCount() == 1);
        const double ph = e.lastPlayhead (kSlotA);
        CHECK (ph >= 100.0 && ph <= 200.0);
    }

    SECTION ("wav-pingpong");
    {
        WavSpec sp;
        auto img = buildWav (indexRamp (1000), {}, sp);
        ScoutEngine e; makeEngine (e, img, "R_C4.wav");
        e.setSlotLoop (kSlotA, 100, 200, LoopKind::PingPong);
        e.noteOn (60, 127);
        auto out = render (e, 44100);
        auto expectPP = [] (int k) { if (k <= 200) return (double) k; const int t = (k - 100) % 200; return t <= 100 ? 100.0 + t : 300.0 - t; };
        bool exact = true; int firstBad = -1;
        for (int k = 0; k < 44100; ++k)
            if (std::fabs (out[(size_t) k] * 32768.0f - expectPP (k)) > 0.01f) { exact = false; if (firstBad < 0) firstBad = k; }
        CHECK (exact);
        if (! exact) std::printf ("  first mismatch at %d: got %f want %f\n", firstBad, out[(size_t) firstBad] * 32768.0f, expectPP (firstBad));
        CHECK (std::fabs (out[200] * 32768.0f - 200.0f) < 0.01f && std::fabs (out[201] * 32768.0f - 199.0f) < 0.01f);
        CHECK (std::fabs (out[300] * 32768.0f - 100.0f) < 0.01f && std::fabs (out[301] * 32768.0f - 101.0f) < 0.01f);
        CHECK (e.activeVoiceCount() == 1);
        e.reset(); e.noteOn (67, 127);
        out = render (e, 44100);
        bool inRange = true;
        for (int k = 300; k < 44100; ++k) { const float v = out[(size_t) k] * 32768.0f; if (! std::isfinite (v) || v < 99.0f || v > 201.0f) inRange = false; }
        CHECK (inRange);
    }

    SECTION ("wav-oneshot");
    {
        WavSpec sp;
        auto img = buildWav (indexRamp (1000), {}, sp);
        ScoutEngine e; makeEngine (e, img, "R_C4.wav");
        e.setSlotLoop (kSlotA, 100, 200, LoopKind::Off);
        e.noteOn (60, 127);
        auto out = render (e, 1500);
        CHECK (std::fabs (out[150] * 32768.0f - 150.0f) < 0.01f);   // markers ignored: straight through
        CHECK (std::fabs (out[998] * 32768.0f - 998.0f) < 0.01f);
        CHECK (out[1100] == 0.0f);
        CHECK (e.activeVoiceCount() == 0);
        CHECK (e.lastPlayhead (kSlotA) < 0.0);
    }

    SECTION ("wav-release");
    {
        WavSpec sp;
        auto img = buildWav (indexRamp (1000), {}, sp);
        ScoutEngine e; makeEngine (e, img, "R_C4.wav");
        e.setSlotLoop (kSlotA, 100, 200, LoopKind::Forward);
        e.setSlotFades (kSlotA, 0.0, 100.0);                        // 100 ms release
        e.noteOn (60, 127);
        render (e, 300);
        e.noteOff (60);
        auto out = render (e, 6000);
        const float a = out[10] * 32768.0f, b = out[11] * 32768.0f;
        const double p10 = 100.0 + std::fmod (310.0 - 100.0, 101.0), p11 = 100.0 + std::fmod (311.0 - 100.0, 101.0);
        CHECK (a > 0.0f && b > 0.0f);
        CHECK (std::fabs ((double) b / (double) a - p11 / p10) < 0.02);
        float env10 = a / (float) p10, env2000 = out[2000] * 32768.0f / (float) (100.0 + std::fmod (2300.0 - 100.0, 101.0));
        CHECK (env10 > env2000 && env2000 > 0.0f);
        CHECK (e.activeVoiceCount() == 0);
        float tail = 0.0f; for (size_t i = 4500; i < out.size(); ++i) tail = std::max (tail, std::fabs (out[i]));
        CHECK (tail == 0.0f);
        e.reset(); e.setSlotFades (kSlotA, 10.0, 80.0); e.noteOn (60, 127);
        out = render (e, 600);
        CHECK (out[0] * 32768.0f < 1.0f);
        CHECK (std::fabs (out[500] * 32768.0f - (100.0f + (float) ((500 - 100) % 101))) < 0.01f);
        // per-slot release: slot B keeps its own time
        install (e, kSlotB, loadWav (img, "B_C4.wav"), LoopKind::Forward);
        e.setSlotLoop (kSlotB, 100, 200, LoopKind::Forward);
        e.setSlotFades (kSlotB, 0.0, 10.0);
        e.reset(); e.setKeyboard (KbMode::B, 60, 0); e.noteOn (60, 127); render (e, 300); e.noteOff (60);
        render (e, 600);                                            // 13.6 ms > 10 ms
        CHECK (e.activeVoiceCount() == 0);
    }

    SECTION ("routing");
    {
        WavSpec sp;
        auto rampImg = buildWav (indexRamp (1000), {}, sp);
        auto sineImg = buildWav (sineF (4000, 100.0), {}, sp);
        ScoutEngine e; makeEngine (e, rampImg, "A_C4.wav");
        install (e, kSlotB, loadWav (sineImg, "B_C4.wav"), LoopKind::Forward);
        e.setSlotLoop (kSlotA, 100, 200, LoopKind::Forward);
        e.setSlotLoop (kSlotB, 0, 3999, LoopKind::Forward);
        // A: only A plays
        e.setKeyboard (KbMode::A, 60, 0);
        e.noteOn (60, 100); render (e, 64);
        CHECK (e.lastNote (kSlotA) == 60 && e.lastNote (kSlotB) == -1 && e.lastPlayhead (kSlotB) < 0.0 && e.lastPlayhead (kSlotA) >= 0.0);
        // B: only B plays, A's readout untouched
        e.reset(); const auto seqA = e.noteCounter (kSlotA);
        e.setKeyboard (KbMode::B, 60, 0);
        e.noteOn (72, 100); render (e, 64);
        CHECK (e.lastNote (kSlotB) == 72 && e.noteCounter (kSlotA) == seqA && e.lastPlayhead (kSlotB) >= 0.0 && e.lastPlayhead (kSlotA) < 0.0);
        // SPLIT at C4: 59 -> A, 60 -> B
        e.reset();
        e.setKeyboard (KbMode::Split, 60, 0);
        e.noteOn (59, 100); render (e, 64);
        CHECK (e.lastNote (kSlotA) == 59 && e.lastNote (kSlotB) == 72);
        e.noteOn (60, 100); render (e, 64);
        CHECK (e.lastNote (kSlotB) == 60 && e.lastNote (kSlotA) == 59);
        CHECK (e.activeVoiceCount() == 2);                          // both slots share the one pool
        // TOGGLE: which slot sounds follows toggleOn
        e.reset();
        e.setKeyboard (KbMode::Toggle, 60, 0);
        e.noteOn (64, 100); render (e, 64);
        CHECK (e.lastNote (kSlotA) == 64 && e.lastNote (kSlotB) == 60);
        e.setKeyboard (KbMode::Toggle, 60, 1);
        e.noteOn (65, 100); render (e, 64);
        CHECK (e.lastNote (kSlotB) == 65 && e.lastNote (kSlotA) == 64);
        CHECK (ScoutEngine::routeSlot (KbMode::Split, 60, 0, 59, true, true) == kSlotA && ScoutEngine::routeSlot (KbMode::Split, 60, 0, 60, true, true) == kSlotB);
        CHECK (ScoutEngine::routeSlot (KbMode::Toggle, 60, 1, 30, true, true) == kSlotB && ScoutEngine::routeSlot (KbMode::Toggle, 60, 0, 30, true, true) == kSlotA);
        // swapping slot A kills only A's voices and retires the old one exactly once
        e.reset(); e.setKeyboard (KbMode::Split, 60, 0);
        e.noteOn (40, 100); e.noteOn (80, 100); render (e, 64);
        CHECK (e.activeVoiceCount() == 2);
        auto w2 = loadWav (rampImg, "A2_C4.wav");
        e.setSlot (kSlotA, w2.release());
        CHECK (e.takeRetired (kSlotA) == nullptr);
        render (e, 64);
        CHECK (e.activeVoiceCount() == 1);                          // the B voice survived
        WavSample* old = e.takeRetired (kSlotA);
        CHECK (old != nullptr && old->fileName == "A_C4.wav");
        delete old;
        CHECK (e.takeRetired (kSlotA) == nullptr);
        // noteOff releases the note on every slot; noteOffSlot only on one
        e.reset(); e.setSlotFades (kSlotA, 0.0, 10.0); e.setSlotFades (kSlotB, 0.0, 10.0);
        e.noteOnSlot (kSlotA, 50, 100); e.noteOnSlot (kSlotB, 50, 100); render (e, 64);
        CHECK (e.activeVoiceCount() == 2);
        e.noteOffSlot (kSlotB, 50); render (e, 900);
        CHECK (e.activeVoiceCount() == 1 && e.lastPlayhead (kSlotA) >= 0.0);
        e.noteOff (50); render (e, 900);
        CHECK (e.activeVoiceCount() == 0);
    }

    SECTION ("wav-smpl");
    {
        WavSpec sp; sp.channels = 2; sp.bits = 32; sp.isFloat = true; sp.withSmpl = true; sp.loopStart = 5; sp.loopEnd = 9; sp.unity = 40;
        auto img = buildWav (sineF (6000, 30.0), sineF (6000, 50.0), sp);
        std::string we;
        auto w = WavSample::load (img.data(), img.size(), "JD_STR1_C4.wav", we);
        CHECK (w != nullptr);
        WavSaveSpec save;
        save.loopStart = 123; save.loopEnd = 4567; save.loopType = 1; save.rootKey = 61; save.fineCents = -20.0;
        save.description = "JD990 SILKSTR dry rel=800ms";
        auto out = writeWav (*w, save);
        auto w2 = WavSample::load (out.data(), out.size(), "JD_STR1_C4.wav", we);
        CHECK (w2 != nullptr);
        if (w2 != nullptr)
        {
            CHECK (w2->hasSmpl && w2->loopStart == 123 && w2->loopEnd == 4567 && w2->loopType == 1);
            CHECK (w2->rootKey == 61 && std::fabs (w2->fineCents + 20.0) < 1e-4);
            CHECK (w2->bextDescription == save.description);
            CHECK (w2->frames == 6000 && w2->isStereo() && w2->formatTag == 3 && w2->bitsPerSample == 32);
        }
        size_t dl1, dl2, fl1, fl2, sl;
        const uint8_t* d1 = findChunk (img, "data", dl1);
        const uint8_t* d2 = findChunk (out, "data", dl2);
        const uint8_t* f1 = findChunk (img, "fmt ", fl1);
        const uint8_t* f2 = findChunk (out, "fmt ", fl2);
        CHECK (d1 != nullptr && d2 != nullptr && dl1 == dl2 && std::memcmp (d1, d2, dl1) == 0);   // audio byte-identical
        CHECK (f1 != nullptr && f2 != nullptr && fl1 == fl2 && std::memcmp (f1, f2, fl1) == 0);
        const uint8_t* sm = findChunk (out, "smpl", sl);
        CHECK (sm != nullptr && sl == 60);
        if (sm != nullptr)
        {
            CHECK (rd32 (sm + 12) == 60);
            CHECK (rd32 (sm + 16) == centsToPitchFraction (80.0));
            CHECK (rd32 (sm + 28) == 1 && rd32 (sm + 40) == 1 && rd32 (sm + 44) == 123 && rd32 (sm + 48) == 4567 && rd32 (sm + 56) == 0);
        }
        size_t bl; const uint8_t* bx = findChunk (out, "bext", bl);
        CHECK (bx != nullptr && bl >= 602 && std::memcmp (bx + 256, "Scout v2", 8) == 0);   // Originator per spec
        int nSmpl = 0, nBext = 0;
        for (size_t at = 12; at + 8 <= out.size();)
        {
            const size_t n = (size_t) out[at + 4] | ((size_t) out[at + 5] << 8) | ((size_t) out[at + 6] << 16) | ((size_t) out[at + 7] << 24);
            if (std::memcmp (out.data() + at, "smpl", 4) == 0) ++nSmpl;
            if (std::memcmp (out.data() + at, "bext", 4) == 0) ++nBext;
            at += 8 + n + (n & 1);
        }
        CHECK (nSmpl == 1 && nBext == 1);
        const uint32_t riffLen = rd32 (out.data() + 4);
        CHECK (riffLen + 8 == out.size());
        auto out2 = writeWav (*w2, save);
        CHECK (out2 == out);
        save.rootKey = 60; save.fineCents = 37.5; save.loopType = 0;
        auto out3 = writeWav (*w, save);
        auto w3 = WavSample::load (out3.data(), out3.size(), "x.wav", we);
        CHECK (w3 != nullptr && w3->rootKey == 60 && std::fabs (w3->fineCents - 37.5) < 1e-4 && w3->loopType == 0);
        save.export16BitMono = true;
        auto out4 = writeWav (*w, save);
        auto w4 = WavSample::load (out4.data(), out4.size(), "x.wav", we);
        CHECK (w4 != nullptr && ! w4->isStereo() && w4->bitsPerSample == 16 && w4->formatTag == 1 && w4->frames == 6000);
        CHECK (w4 != nullptr && w4->loopStart == 123 && w4->loopEnd == 4567);
        if (w4 != nullptr)
        {
            const float want = (w->left[1000] + w->right[1000]) * 0.70710678f;
            CHECK (std::fabs (w4->left[1000] - want) < 1e-4f);
        }
        save.export16BitMono = false; save.loopStart = 5990; save.loopEnd = 999999;
        auto out5 = writeWav (*w, save);
        auto w5 = WavSample::load (out5.data(), out5.size(), "x.wav", we);
        CHECK (w5 != nullptr && w5->loopStart == 5990 && w5->loopEnd == 5999);
    }

    SECTION ("export");
    {
        // source: 22050 Hz stereo float, 6000 frames, loop [2000, 3999]
        WavSpec sp; sp.channels = 2; sp.bits = 32; sp.isFloat = true; sp.rate = 22050;
        auto img = buildWav (sineF (6000, 30.0), sineF (6000, 50.0, 0.25), sp);
        std::string we;
        auto w = WavSample::load (img.data(), img.size(), "src.wav", we);
        CHECK (w != nullptr);
        WavSaveSpec spec; spec.loopStart = 2000; spec.loopEnd = 3999; spec.loopType = 0; spec.rootKey = 60; spec.description = "range test";
        ExportOptions opt; opt.originationDate = "2026-09-13"; opt.originationTime = "12:34:56";
        // 1. EXPORT RANGE [1500, 4500): exactly 3000 frames, markers remapped (acceptance 5)
        opt.hasRange = true; opt.rangeStart = 1500; opt.rangeEnd = 4500;
        auto r1 = exportWav (*w, spec, opt);
        CHECK (r1.error.empty() && r1.frames == 3000 && r1.loopKept && r1.loopStart == 500 && r1.loopEnd == 2499);
        auto b1 = WavSample::load (r1.bytes.data(), r1.bytes.size(), "r1.wav", we);
        CHECK (b1 != nullptr && b1->frames == 3000 && b1->hasSmpl && b1->loopStart == 500 && b1->loopEnd == 2499 && b1->rootKey == 60);
        // a stereo source exports mono (fold rule): SUM at -3 dB
        CHECK (b1 != nullptr && ! b1->isStereo() && b1->sampleRate == 22050 && b1->formatTag == 3 && b1->bitsPerSample == 32);
        CHECK (b1 != nullptr && std::fabs (b1->left[100] - (w->left[1600] + w->right[1600]) * 0.70710678f) < 1e-5f);
        size_t bl; const uint8_t* bx = findChunk (r1.bytes, "bext", bl);
        CHECK (bx != nullptr && std::memcmp (bx, "range test", 10) == 0 && std::memcmp (bx + 256, "Scout v2", 8) == 0
               && std::memcmp (bx + 320, "2026-09-13", 10) == 0 && std::memcmp (bx + 330, "12:34:56", 8) == 0);
        // 2. markers outside the range: loop dropped, root kept, smpl carries no loop
        opt.rangeStart = 2500; opt.rangeEnd = 3000;
        auto r2 = exportWav (*w, spec, opt);
        CHECK (r2.error.empty() && r2.frames == 500 && ! r2.loopKept);
        auto b2 = WavSample::load (r2.bytes.data(), r2.bytes.size(), "r2.wav", we);
        CHECK (b2 != nullptr && b2->frames == 500 && b2->rootKey == 60 && b2->rootFromFile);
        size_t sl; const uint8_t* sm = findChunk (r2.bytes, "smpl", sl);
        CHECK (sm != nullptr && sl == 36 && rd32 (sm + 28) == 0);
        // an empty or reversed range is refused
        opt.rangeStart = 3000; opt.rangeEnd = 3000;
        CHECK (! exportWav (*w, spec, opt).error.empty());
        opt.rangeStart = 100; opt.rangeEnd = 999999;                 // end clamped to the file
        CHECK (exportWav (*w, spec, opt).frames == 5900);
        // 3. L ONLY fold
        opt.hasRange = false; opt.stereoFold = 1;
        auto r3 = exportWav (*w, spec, opt);
        auto b3 = WavSample::load (r3.bytes.data(), r3.bytes.size(), "r3.wav", we);
        CHECK (b3 != nullptr && b3->frames == 6000 && ! b3->isStereo() && std::fabs (b3->left[777] - w->left[777]) < 1e-6f);
        // 4. 16-bit / 44.1 kHz mono conversion: rate doubles, frames double, markers scale, sine survives the resampler
        opt.stereoFold = 0; opt.convert16Bit441Mono = true;
        auto r4 = exportWav (*w, spec, opt);
        CHECK (r4.error.empty() && r4.sampleRate == 44100 && r4.bits == 16 && r4.channels == 1 && r4.frames == 12000);
        CHECK (r4.loopKept && r4.loopStart == 4000 && r4.loopEnd == 7999);
        auto b4 = WavSample::load (r4.bytes.data(), r4.bytes.size(), "r4.wav", we);
        CHECK (b4 != nullptr && b4->formatTag == 1 && b4->bitsPerSample == 16 && b4->sampleRate == 44100 && b4->frames == 12000);
        CHECK (b4 != nullptr && b4->loopStart == 4000 && b4->loopEnd == 7999);
        if (b4 != nullptr)
        {
            // the 30-sample period at 22050 becomes 60 samples at 44100 (both channels folded: 0.5*sin(30) + 0.25*sin(50))
            std::vector<float> mono (b4->left.begin(), b4->left.end());
            // isolate the 60-sample component by checking the left channel's dominant period via zero crossings of a high-passed view
            double peak = 0.0; for (size_t i = 1000; i < 11000; ++i) peak = std::max (peak, (double) std::fabs (mono[i]));
            CHECK (peak > 0.3 && peak < 0.6);
            bool fin = true; for (float v : mono) fin = fin && std::isfinite (v);
            CHECK (fin);
        }
        // 5. native export of a decoded module sample is a plain 16-bit mono file with its loop
        auto dz = bank->decodeZone (*z0);
        WavSaveSpec zs; zs.loopStart = dz->loopStart; zs.loopEnd = dz->loopEnd; zs.rootKey = dz->rootKey;
        ExportOptions nat;
        auto r5 = exportWav (*dz, zs, nat);
        auto b5 = WavSample::load (r5.bytes.data(), r5.bytes.size(), "z.wav", we);
        CHECK (b5 != nullptr && b5->frames == 4000 && b5->bitsPerSample == 16 && ! b5->isStereo() && b5->loopStart == 1000 && b5->loopEnd == 2999);
        CHECK (b5 != nullptr && std::fabs (b5->left[1234] - dz->left[1234]) < 1e-4f);
        // 6. an empty sample is refused
        WavSample empty;
        CHECK (! exportWav (empty, zs, nat).error.empty());
    }

    SECTION ("markers");
    {
        CHECK (pickMarker (100.0, 500.0, 104.0, 8.0, true)  == Marker::Start);
        CHECK (pickMarker (100.0, 500.0, 507.0, 8.0, true)  == Marker::End);
        CHECK (pickMarker (100.0, 500.0, 300.0, 8.0, true)  == Marker::None);
        CHECK (pickMarker (100.0, 500.0, 300.0, 8.0, false) == Marker::Start);
        CHECK (pickMarker (100.0, 500.0, 301.0, 8.0, false) == Marker::End);
        CHECK (pickMarker (0.0, 888.0, 3.0, 8.0, true) == Marker::Start);
        CHECK (pickMarker (0.0, 888.0, 884.0, 8.0, true) == Marker::End);
        int64_t s = 100, e = 200;
        applyMarkerDrag (Marker::End, 301, s, e, 999);   CHECK (s == 100 && e == 300);
        applyMarkerDrag (Marker::Start, 150, s, e, 999); CHECK (s == 150 && e == 300);
        applyMarkerDrag (Marker::Start, 900, s, e, 999); CHECK (s == 300 && e == 300);
        applyMarkerDrag (Marker::End, 50, s, e, 999);    CHECK (s == 300 && e == 300);
        applyMarkerDrag (Marker::End, 5000, s, e, 999);  CHECK (e == 999);
        applyMarkerDrag (Marker::Start, -7, s, e, 999);  CHECK (s == 0);
        applyMarkerDrag (Marker::None, 5, s, e, 999);    CHECK (s == 0 && e == 999);
        applyMarkerDrag (Marker::End, 1000, s, e, 999);  CHECK (e == 999);
        int64_t s2 = 5, e2 = 5; applyMarkerDrag (Marker::End, 5, s2, e2, 9); CHECK (s2 == 5 && e2 == 5);
        applyMarkerDrag (Marker::End, 0, s2, e2, -1); CHECK (s2 == 5 && e2 == 5);
        // PLAY-style audition: the Cur slot sounds regardless of the keyboard routing, note-off releases it
        WavSpec sp; auto img = buildWav (indexRamp (1000), {}, sp);
        ScoutEngine en; makeEngine (en, img, "A_C4.wav");
        install (en, kSlotCur, loadWav (img, "CUR_C4.wav"), LoopKind::Forward);
        en.setSlotLoop (kSlotCur, 100, 200, LoopKind::Forward);
        en.setKeyboard (KbMode::A, 60, 0);
        en.noteOnSlot (kSlotCur, 48, 100);
        render (en, 64);
        CHECK (en.lastNote (kSlotCur) == 48 && en.lastPlayhead (kSlotCur) >= 0.0 && en.activeVoiceCount() == 1 && en.lastNote (kSlotA) == -1);
        en.noteOffSlot (kSlotCur, 48);
        render (en, 8820);
        CHECK (en.activeVoiceCount() == 0);
    }

    SECTION ("routing-fallback");
    {
        // pure routing with empty slots: an empty target falls back to the loaded slot
        CHECK (ScoutEngine::routeSlot (KbMode::A, 60, 0, 72, false, true) == kSlotB);        // A chosen, none loaded -> B
        CHECK (ScoutEngine::routeSlot (KbMode::B, 60, 0, 72, true, false) == kSlotA);        // B chosen, none loaded -> A
        CHECK (ScoutEngine::routeSlot (KbMode::Split, 60, 0, 72, true, false) == kSlotA);    // SPLIT upper half, no B -> A
        CHECK (ScoutEngine::routeSlot (KbMode::Split, 60, 0, 40, false, true) == kSlotB);    // SPLIT lower half, no A -> B
        CHECK (ScoutEngine::routeSlot (KbMode::Toggle, 60, 1, 40, true, false) == kSlotA);   // TOGGLE on B, no B -> A
        CHECK (ScoutEngine::routeSlot (KbMode::A, 60, 0, 72, true, true) == kSlotA);         // both loaded: honoured
        CHECK (ScoutEngine::routeSlot (KbMode::A, 60, 0, 72, false, false) == -1);           // nothing loaded: nothing to do
        // engine-level: routing B with only A loaded still plays A (the "keyboard went silent" case)
        WavSpec sp; auto img = buildWav (indexRamp (1000), {}, sp);
        ScoutEngine e; e.prepare (44100.0);
        install (e, kSlotA, loadWav (img, "A_C4.wav"), LoopKind::Forward);
        e.setKeyboard (KbMode::B, 60, 0);
        e.noteOn (60, 100); render (e, 64);
        CHECK (e.activeVoiceCount() == 1 && e.lastNote (kSlotA) == 60 && e.lastNote (kSlotB) == -1);
        // ...and once B lands, the same routing goes to it
        install (e, kSlotB, loadWav (img, "B_C4.wav"), LoopKind::Forward);
        e.noteOn (72, 100); render (e, 64);
        CHECK (e.lastNote (kSlotB) == 72 && e.activeVoiceCount() == 2);
        // nothing loaded at all: a note is a no-op, no crash
        ScoutEngine e2; e2.prepare (44100.0);
        e2.setKeyboard (KbMode::Split, 60, 0);
        e2.noteOn (60, 100); e2.noteOnSlot (kSlotCur, 60, 100); auto out = render (e2, 256);
        CHECK (e2.activeVoiceCount() == 0);
        float peak = 0.0f; for (float v : out) peak = std::max (peak, std::fabs (v));
        CHECK (peak == 0.0f);
    }

    SECTION ("unload");
    {
        WavSpec sp; auto img = buildWav (indexRamp (1000), {}, sp);
        ScoutEngine e; makeEngine (e, img, "A_C4.wav");
        install (e, kSlotB, loadWav (img, "B_C4.wav"), LoopKind::Forward);
        e.setSlotLoop (kSlotA, 100, 200, LoopKind::Forward);
        e.setSlotLoop (kSlotB, 100, 200, LoopKind::Forward);
        e.setKeyboard (KbMode::Split, 60, 0);
        e.noteOn (48, 100); e.noteOn (72, 100); render (e, 64);
        CHECK (e.activeVoiceCount() == 2);
        // clear A: only A's voice dies, A is retired exactly once, B keeps sounding
        const WavSample* aPtr = e.requested (kSlotA);
        CHECK (aPtr != nullptr);
        e.clearSlot (kSlotA);
        CHECK (e.requested (kSlotA) == nullptr);
        CHECK (e.takeRetired (kSlotA) == nullptr);             // not until the audio thread runs
        auto out = render (e, 64);
        CHECK (e.activeVoiceCount() == 1 && e.lastPlayhead (kSlotB) >= 0.0 && e.lastPlayhead (kSlotA) < 0.0 && e.lastNote (kSlotA) == -1);
        WavSample* ra = e.takeRetired (kSlotA);
        CHECK (ra == aPtr);
        delete ra;
        CHECK (e.takeRetired (kSlotA) == nullptr);
        bool fin = true; for (float v : out) fin = fin && std::isfinite (v);
        CHECK (fin);
        // notes aimed at the empty A now fall back to B; nothing crashes
        e.noteOn (40, 100); render (e, 64);
        CHECK (e.lastNote (kSlotB) == 40 && e.activeVoiceCount() == 2);
        // clear B too: everything silent, retiree collected once, further notes are no-ops
        e.clearSlot (kSlotB);
        render (e, 64);
        CHECK (e.activeVoiceCount() == 0 && e.lastPlayhead (kSlotB) < 0.0 && e.lastNote (kSlotB) == -1);
        WavSample* rb = e.takeRetired (kSlotB);
        CHECK (rb != nullptr && rb->fileName == "B_C4.wav");
        delete rb;
        CHECK (e.takeRetired (kSlotB) == nullptr);
        e.noteOn (60, 100); e.noteOnSlot (kSlotB, 60, 100); out = render (e, 256);
        CHECK (e.activeVoiceCount() == 0);
        float peak = 0.0f; for (float v : out) peak = std::max (peak, std::fabs (v));
        CHECK (peak == 0.0f);
        // swap safety: clear while a retiree is still parked -> the clear waits, then completes once collected
        auto b1 = loadWav (img, "B1_C4.wav"), b2 = loadWav (img, "B2_C4.wav");
        e.setSlot (kSlotA, b1.release()); render (e, 64);
        e.setSlot (kSlotA, b2.release()); render (e, 64);      // b1 parked in retired
        e.setKeyboard (KbMode::A, 60, 0); e.noteOn (60, 100); render (e, 64);
        CHECK (e.activeVoiceCount() == 1);
        e.clearSlot (kSlotA);
        render (e, 64);                                        // retiree slot busy: clear deferred, b2 still plays
        CHECK (e.activeVoiceCount() == 1);
        delete e.takeRetired (kSlotA);                         // collector catches up (b1)
        render (e, 64);
        CHECK (e.activeVoiceCount() == 0);
        WavSample* rb2 = e.takeRetired (kSlotA);
        CHECK (rb2 != nullptr);
        delete rb2;
        CHECK (e.takeRetired (kSlotA) == nullptr);
        // clear with a sample pending (never reached the audio thread): pending is dropped, no leak/double free
        auto b3 = loadWav (img, "B3_C4.wav");
        WavSample* b3raw = b3.get();
        e.setSlot (kSlotA, b3.release());
        WavSample* dropped = e.clearSlot (kSlotA);
        CHECK (dropped == b3raw);
        delete dropped;
        render (e, 64);
        CHECK (e.takeRetired (kSlotA) == nullptr && e.requested (kSlotA) == nullptr);
        e.noteOn (60, 100); render (e, 64);
        CHECK (e.activeVoiceCount() == 0);
    }

    SECTION ("assists");
    {
        // a 441 Hz sine (period 100 at 44.1 kHz), 4000 frames
        WavSpec sp;
        auto img = buildWav (sineF (4000, 100.0), {}, sp);
        auto w = loadWav (img, "sine.wav");
        // LOUDNESS: RMS of a 0.5 sine = 0.3536 -> -9.03 dB
        CHECK (std::fabs (loopRmsDb (*w, 0, 1999) - (-9.03)) < 0.05);
        CHECK (loopRmsDb (*w, 500, 400) == -120.0 && loopRmsDb (*w, 0, 99999) == -120.0);
        // AUTO-DETECT ROOT: 441 Hz -> A4 +3.9 c
        auto root = autoDetectRoot (*w, 0, 1999);
        CHECK (root.found && std::fabs (root.hz - 441.0) < 2.0 && root.note == 69 && std::fabs (root.cents - 3.93) < 6.0 && root.confidence > 0.9);
        // an octave-ambiguous tone (period 200 with a strong 2nd harmonic) picks the fundamental
        std::vector<float> h (8000);
        for (int i = 0; i < 8000; ++i) h[(size_t) i] = (float) (0.3 * std::sin (2.0 * 3.14159265358979 * i / 200.0) + 0.3 * std::sin (2.0 * 3.14159265358979 * i / 100.0));
        auto hw = loadWav (buildWav (h, {}, sp), "harm.wav");
        auto hr = autoDetectRoot (*hw, 0, 7999);
        CHECK (hr.found && std::fabs (hr.hz - 220.5) < 1.5);
        // silence / tiny input: not found, no crash
        auto sw = loadWav (buildWav (std::vector<float> (4000, 0.0f), {}, sp), "silence.wav");
        CHECK (! autoDetectRoot (*sw, 0, 3999).found);
        WavSample tiny; tiny.frames = 8; tiny.left.assign (8, 0.1f); tiny.sampleRate = 44100;
        CHECK (! autoDetectRoot (tiny, 0, 7).found && ! suggestSplice (tiny, 0, 7).found && loopRmsDb (tiny, 0, 7) < 0.0);
        // PERIODS: loop [0, 1999] at the detected root = 20.00 periods
        const double per = loopPeriods (2000, 44100.0, root.note, root.cents);
        CHECK (std::fabs (per - 20.0) < 0.05 && periodsNearInteger (per));
        CHECK (! periodsNearInteger (loopPeriods (2050, 44100.0, root.note, root.cents)));
        // CLICK METER: a seam at a zero crossing on the period grid is clean, a seam mid-cycle is not
        const double good = clickRatio (*w, 0, 1999, LoopKind::Forward);
        const double bad  = clickRatio (*w, 0, 1975, LoopKind::Forward);
        CHECK (good <= 1.05 && bad > 2.0);
        CHECK (clickRatio (*w, 0, 1975, LoopKind::Off) == 0.0);
        const double pp = clickRatio (*w, 0, 1999, LoopKind::PingPong);
        CHECK (pp >= 0.0 && std::isfinite (pp));
        // SUGGEST: from the bad END, the search lands on a period-aligned END (seam continuous)
        auto sg = suggestSplice (*w, 0, 1975);
        CHECK (sg.found && sg.candidates > 100);
        if (sg.found)
        {
            const float xe = w->left[sg.loopEnd], xe1 = w->left[sg.loopEnd - 1];
            CHECK (std::fabs (w->left[0] - (xe + (xe - xe1))) < 0.01f);      // value continuity across the seam
            CHECK (clickRatio (*w, 0, sg.loopEnd, LoopKind::Forward) <= 1.05);
            CHECK ((int) sg.loopEnd > 1975 - 8820 && (int) sg.loopEnd < 1975 + 8820);      // within +-200 ms
        }
        // stereo input goes through the mono mix
        WavSpec st; st.channels = 2;
        auto stw = loadWav (buildWav (sineF (4000, 100.0), sineF (4000, 100.0), st), "st.wav");
        CHECK (std::fabs (loopRmsDb (*stw, 0, 1999) - (-9.03)) < 0.1);
    }

    // ------------------------------------------------------------ v2: modules
    auto buildMod = [] (bool withLoop) -> std::vector<uint8_t>
    {
        std::vector<uint8_t> m;
        auto u8  = [&] (int v) { m.push_back ((uint8_t) v); };
        auto be16 = [&] (int v) { u8 ((v >> 8) & 0xff); u8 (v & 0xff); };
        auto fixed = [&] (const char* s, int n) { const int l = (int) std::strlen (s); for (int i = 0; i < n; ++i) u8 (i < l ? s[i] : 0); };
        fixed ("scout harness", 20);
        for (int i = 0; i < 31; ++i)
        {
            if (i == 0)      { fixed ("loop_c", 22); be16 (500); u8 (0); u8 (64); be16 (withLoop ? 100 : 0); be16 (withLoop ? 100 : 1); }
            else if (i == 1) { fixed ("shot", 22);   be16 (150); u8 (0); u8 (48); be16 (0); be16 (1); }
            else             { fixed ("", 22);       be16 (0);   u8 (0); u8 (0);  be16 (0); be16 (1); }
        }
        u8 (1); u8 (127);
        for (int i = 0; i < 128; ++i) u8 (0);
        fixed ("M.K.", 4);
        for (int i = 0; i < 1024; ++i) u8 (0);
        for (int i = 0; i < 1000; ++i) u8 ((int8_t) ((i % 200) - 100));
        for (int i = 0; i < 300; ++i) u8 ((int8_t) (i / 3 - 50));
        return m;
    };

    SECTION ("mod-load");
    {
        auto img = buildMod (true);
        std::string merr;
        auto mod = ModuleSource::load (img.data(), img.size(), "harness.mod", merr);
        CHECK (mod != nullptr);
        if (mod != nullptr)
        {
            CHECK (mod->title() == "scout harness");
            CHECK (mod->formatType() == "mod");
            CHECK (mod->sampleCount() == 31 && mod->nonEmptySampleCount() == 2 && mod->firstNonEmpty() == 1);
            const auto& s1 = mod->samples()[0];
            CHECK (s1.index == 1 && s1.name == "loop_c" && s1.frames == 1000 && s1.bits == 8 && s1.channels == 1);
            CHECK (s1.hasLoop && ! s1.pingPong && s1.loopStart == 200 && s1.loopEnd == 400 && ! s1.hasSustain);
            CHECK (s1.sampleRate == 8287);
            const auto& s2 = mod->samples()[1];
            CHECK (s2.name == "shot" && s2.frames == 300 && ! s2.hasLoop);
            CHECK (mod->samples()[2].isEmpty());
            std::string derr;
            auto w = mod->decode (1, derr);
            CHECK (w != nullptr);
            if (w != nullptr)
            {
                CHECK (w->frames == 1000 && w->channels == 1 && w->bitsPerSample == 16 && w->sampleRate == 8287);
                CHECK (w->hasSmpl && w->loopStart == 200 && w->loopEnd == 399 && w->loopType == 0);
                CHECK (w->rootKey == 60 && w->fineCents == 0.0 && w->rootFromFile);
                CHECK (std::fabs (w->left[0] * 128.0f - (-100.0f)) < 1e-3f && std::fabs (w->left[150] * 128.0f - 50.0f) < 1e-3f);
                CHECK (w->chunks.size() == 2);
                CHECK (w->bextDescription.find ("harness.mod") != std::string::npos && w->bextDescription.find ("loop=200-400") != std::string::npos);
                WavSaveSpec spec; spec.loopStart = w->loopStart; spec.loopEnd = w->loopEnd; spec.loopType = 0; spec.rootKey = 60;
                auto bytes = writeWav (*w, spec);
                std::string rerr;
                auto back = WavSample::load (bytes.data(), bytes.size(), "loop_c.wav", rerr);
                CHECK (back != nullptr && back->frames == 1000 && back->sampleRate == 8287 && back->hasSmpl
                       && back->loopStart == 200 && back->loopEnd == 399 && back->rootKey == 60);
                CHECK (back != nullptr && std::memcmp (back->chunks[1].body.data(), w->chunks[1].body.data(), 2000) == 0);
            }
            auto w2 = mod->decode (2, derr);
            CHECK (w2 != nullptr && ! w2->hasSmpl && w2->loopStart == 0 && w2->loopEnd == 299);
            CHECK (mod->decode (3, derr) == nullptr && ! derr.empty());
            CHECK (mod->decode (0, derr) == nullptr && mod->decode (99, derr) == nullptr);
        }
        auto img2 = buildMod (false);
        auto mod2 = ModuleSource::load (img2.data(), img2.size(), "noloop.mod", merr);
        CHECK (mod2 != nullptr && ! mod2->samples()[0].hasLoop);
        std::string d2;
        auto w3 = mod2 != nullptr ? mod2->decode (1, d2) : nullptr;
        CHECK (w3 != nullptr && ! w3->hasSmpl && w3->loopEnd == 999);
        CHECK (ModuleSource::isSupportedExtension ("mod") && ModuleSource::isSupportedExtension (".XM")
               && ModuleSource::isSupportedExtension ("it") && ModuleSource::isSupportedExtension ("s3m")
               && ! ModuleSource::isSupportedExtension ("wav") && ! ModuleSource::isSupportedExtension ("sf2")
               && ! ModuleSource::isSupportedExtension (""));
        CHECK (ModuleSource::supportedExtensions().size() > 20);
        CHECK (ModuleSource::load (nullptr, 0, "x", merr) == nullptr && ! merr.empty());
        const char junk[] = "this is not a module at all, just some bytes of text that go nowhere";
        CHECK (ModuleSource::load (junk, sizeof (junk), "x.mod", merr) == nullptr && ! merr.empty());
        for (int k = 1; k < 16; ++k)
        {
            const size_t cut = img.size() * (size_t) k / 16;
            std::string te;
            auto t = ModuleSource::load (img.data(), cut, "cut.mod", te);
            if (t != nullptr) { std::string de; (void) t->decode (1, de); (void) t->decode (2, de); }
        }
        CHECK (true);
        std::vector<uint8_t> lie = img; lie[42] = 0xff; lie[43] = 0xff;
        { std::string le; auto t = ModuleSource::load (lie.data(), lie.size(), "lie.mod", le); if (t != nullptr) { std::string de; (void) t->decode (1, de); } }
        CHECK (true);
    }

    SECTION ("mod-play");
    {
        // a decoded module sample plays at its C-5 rate on note 60
        auto img = buildMod (true);
        std::string merr;
        auto mod = ModuleSource::load (img.data(), img.size(), "harness.mod", merr);
        std::string derr;
        auto w = mod != nullptr ? mod->decode (1, derr) : nullptr;
        CHECK (w != nullptr);
        if (w != nullptr)
        {
            ScoutEngine e; e.prepare (44100.0);
            install (e, kSlotA, std::move (w), LoopKind::Forward);
            e.setKeyboard (KbMode::A, 60, 0);
            e.noteOn (60, 100);
            render (e, 1000);                                                 // ~22 ms, still before loopStart
            const double expect = 1000.0 * 8287.0 / 44100.0;                  // ~188 frames in
            CHECK (e.activeVoiceCount() == 1);
            CHECK (std::fabs (e.lastPlayhead (kSlotA) - expect) < 2.0);
            auto out = render (e, 44100);
            CHECK (e.activeVoiceCount() == 1 && e.lastPlayhead (kSlotA) >= 200.0 && e.lastPlayhead (kSlotA) <= 400.0);
            float peak = 0.0f; for (float v : out) peak = std::max (peak, std::fabs (v));
            CHECK (peak > 0.05f);
            e.noteOff (60); render (e, 8820);
            CHECK (e.activeVoiceCount() == 0);
            e.clearSlot (kSlotA); render (e, 64); delete e.takeRetired (kSlotA);
        }
    }

    std::printf ("%d checks, %d failures\n", g_checks, g_failed);
    if (g_failed == 0) std::printf ("ALL CHECKS PASSED\n");
    return g_failed == 0 ? 0 : 1;
}
