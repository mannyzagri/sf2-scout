// ModuleSource -- see the header. Thin wrapper over OpenMPT::CSoundFile from
// the vendored libopenmpt soundlib (LIBOPENMPT_BUILD). The soundlib is only
// ever used as a PARSER here: no mixing, no playback, no pattern rendering.
#include "ModuleSource.h"

#include "common/stdafx.h"
#include "common/FileReader.h"
#include "common/mptFileIO.h"
#include "common/mptString.h"
#include "soundlib/Sndfile.h"
#include "soundlib/ModSample.h"

#include "mpt/base/span.hpp"
#include "mpt/io_read/filecursor_memory.hpp"

#include <algorithm>
#include <cstring>

// The soundlib routes MPT_ASSERT through this hook; upstream defines it in the
// public libopenmpt layer we do not compile. A failed assertion here is a
// malformed-file symptom, never fatal: swallow it (the loader returns false).
namespace OpenMPT
{
MPT_NOINLINE void AssertHandler (const mpt::source_location&, const char*, const char*) {}
}

namespace sf2scout
{

struct ModuleSource::Impl
{
    std::unique_ptr<OpenMPT::CSoundFile> song;
};

ModuleSource::~ModuleSource()
{
    delete impl_;
}

namespace
{
std::string toUtf8 (const OpenMPT::mpt::ustring& u)
{
    return OpenMPT::mpt::ToCharset (OpenMPT::mpt::Charset::UTF8, u);
}

std::string fromModCharset (const OpenMPT::CSoundFile& s, const char* raw)
{
    if (raw == nullptr) return {};
    return OpenMPT::mpt::ToCharset (OpenMPT::mpt::Charset::UTF8, s.GetCharsetInternal(), std::string (raw));
}

std::string trimRight (std::string s)
{
    while (! s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

std::string lowerNoDot (std::string ext)
{
    if (! ext.empty() && ext[0] == '.') ext.erase (0, 1);
    for (auto& c : ext) c = (char) std::tolower ((unsigned char) c);
    return ext;
}
} // namespace

std::unique_ptr<ModuleSource> ModuleSource::load (const void* data, size_t size, const std::string& fileName, std::string& error)
{
    error.clear();
    if (data == nullptr || size < 4) { error = "not a module (too short)"; return nullptr; }

    std::unique_ptr<ModuleSource> out (new ModuleSource());
    out->fileName_ = fileName;
    out->impl_ = new Impl();

    try
    {
        auto song = std::make_unique<OpenMPT::CSoundFile>();
        OpenMPT::mpt::span<const std::byte> bytes ((const std::byte*) data, size);
        OpenMPT::FileCursor cursor = OpenMPT::mpt::IO::make_FileCursor<OpenMPT::mpt::PathString> (bytes);
        // samples yes, patterns/plugins no: we are a container reader
        const auto flags = static_cast<OpenMPT::CSoundFile::ModLoadingFlags> (OpenMPT::CSoundFile::loadCompleteModule
                                                                             & ~OpenMPT::CSoundFile::loadPluginData
                                                                             & ~OpenMPT::CSoundFile::loadPluginInstance);
        if (! song->Create (cursor, flags))
        {
            error = "not a recognised tracker module";
            return nullptr;
        }

        const OpenMPT::CSoundFile& s = *song;
        out->title_      = trimRight (fromModCharset (s, s.m_songName.c_str()));
        out->formatName_ = toUtf8 (s.m_modFormat.formatName);
        out->formatType_ = toUtf8 (s.m_modFormat.type);
        out->madeWith_   = toUtf8 (s.m_modFormat.madeWithTracker);
        out->message_    = s.m_songMessage.GetFormatted (OpenMPT::SongMessage::leLF);

        for (OpenMPT::INSTRUMENTINDEX i = 1; i <= s.GetNumInstruments(); ++i)
            out->instrumentNames_.push_back (trimRight (fromModCharset (s, s.GetInstrumentName (i))));

        for (OpenMPT::SAMPLEINDEX i = 1; i <= s.GetNumSamples(); ++i)
        {
            const OpenMPT::ModSample& m = s.GetSample (i);
            ModuleSampleInfo info;
            info.index    = (int) i;
            info.name     = trimRight (fromModCharset (s, s.GetSampleName (i)));
            info.fileName = trimRight (fromModCharset (s, m.filename.buf));
            if (m.HasSampleData())
            {
                info.frames     = (uint32_t) m.nLength;
                info.bits       = m.GetElementarySampleSize() == 2 ? 16 : 8;
                info.channels   = m.GetNumChannels();
                info.sampleRate = m.GetSampleRate (s.GetType());
                info.hasLoop    = m.uFlags[OpenMPT::CHN_LOOP] && m.nLoopEnd > m.nLoopStart;
                info.pingPong   = info.hasLoop && m.uFlags[OpenMPT::CHN_PINGPONGLOOP];
                info.loopStart  = (uint32_t) std::min<OpenMPT::SmpLength> (m.nLoopStart, m.nLength);
                info.loopEnd    = (uint32_t) std::min<OpenMPT::SmpLength> (m.nLoopEnd, m.nLength);
                info.hasSustain = m.uFlags[OpenMPT::CHN_SUSTAINLOOP] && m.nSustainEnd > m.nSustainStart;
                info.sustainPingPong = info.hasSustain && m.uFlags[OpenMPT::CHN_PINGPONGSUSTAIN];
                info.sustainStart = (uint32_t) std::min<OpenMPT::SmpLength> (m.nSustainStart, m.nLength);
                info.sustainEnd   = (uint32_t) std::min<OpenMPT::SmpLength> (m.nSustainEnd, m.nLength);
                info.defaultVolume = (int) (m.nVolume / 4);
            }
            out->samples_.push_back (std::move (info));
        }
        out->impl_->song = std::move (song);
    }
    catch (const std::exception& e)
    {
        error = std::string ("module load failed: ") + e.what();
        return nullptr;
    }
    catch (...)
    {
        error = "module load failed";
        return nullptr;
    }
    return out;
}

bool ModuleSource::isSupportedExtension (const std::string& ext)
{
    const std::string e = lowerNoDot (ext);
    if (e.empty()) return false;
    try { return OpenMPT::CSoundFile::IsExtensionSupported (e); }
    catch (...) { return false; }
}

std::vector<std::string> ModuleSource::supportedExtensions()
{
    std::vector<std::string> out;
    try
    {
        for (const char* e : OpenMPT::CSoundFile::GetSupportedExtensions (false))
            if (e != nullptr) out.emplace_back (e);
    }
    catch (...) {}
    return out;
}

int ModuleSource::nonEmptySampleCount() const
{
    int n = 0;
    for (const auto& s : samples_) if (! s.isEmpty()) ++n;
    return n;
}

int ModuleSource::firstNonEmpty() const
{
    for (const auto& s : samples_) if (! s.isEmpty()) return s.index;
    return -1;
}

std::unique_ptr<WavSample> ModuleSource::decode (int index, std::string& error) const
{
    error.clear();
    if (impl_ == nullptr || impl_->song == nullptr) { error = "no module loaded"; return nullptr; }
    if (index < 1 || index > sampleCount()) { error = "sample index out of range"; return nullptr; }
    const ModuleSampleInfo& info = samples_[(size_t) index - 1];
    if (info.isEmpty()) { error = "sample slot " + std::to_string (index) + " is empty"; return nullptr; }

    const OpenMPT::ModSample& m = impl_->song->GetSample ((OpenMPT::SAMPLEINDEX) index);
    if (! m.HasSampleData()) { error = "sample has no data"; return nullptr; }

    const uint32_t frames = info.frames;
    const int ch = info.channels;
    std::vector<int16_t> pcm ((size_t) frames * (size_t) ch);
    if (m.GetElementarySampleSize() == 2)
    {
        const int16_t* src = m.sample16();
        std::memcpy (pcm.data(), src, pcm.size() * sizeof (int16_t));
    }
    else
    {
        const int8_t* src = m.sample8();
        for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = (int16_t) (src[i] * 256);
    }

    // which loop the decoded sample carries (see header)
    bool haveLoop = false, pingPong = false; uint32_t ls = 0, le = 0;
    if (info.hasSustain)      { haveLoop = true; pingPong = info.sustainPingPong; ls = info.sustainStart; le = info.sustainEnd; }
    else if (info.hasLoop)    { haveLoop = true; pingPong = info.pingPong;        ls = info.loopStart;    le = info.loopEnd; }

    std::string name = info.name.empty() ? ("sample " + std::to_string (index)) : info.name;
    std::string wavName = name + ".wav";
    auto w = WavSample::fromPcm16 (std::move (pcm), ch, info.sampleRate, wavName);
    w->hasSmpl   = haveLoop;
    if (haveLoop && le > ls)
    {
        w->loopStart = ls;
        w->loopEnd   = le - 1;                                // exclusive -> inclusive
        w->loopType  = pingPong ? 1 : 0;
    }
    else
    {
        w->loopStart = 0;
        w->loopEnd   = frames > 0 ? frames - 1 : 0;
        w->loopType  = 0;
    }
    w->rootKey = 60; w->fineCents = 0.0; w->rootFromFile = true;
    w->bextDescription = "module=" + fileName_ + " (" + formatType_ + ") sample=" + std::to_string (index) + " \"" + name + "\""
                       + " bits=" + std::to_string (info.bits) + " c5=" + std::to_string (info.sampleRate) + "Hz"
                       + (info.hasLoop ? (std::string (" loop=") + std::to_string (info.loopStart) + "-" + std::to_string (info.loopEnd) + (info.pingPong ? " pingpong" : " fwd")) : std::string (" loop=none"))
                       + (info.hasSustain ? (std::string (" sustain=") + std::to_string (info.sustainStart) + "-" + std::to_string (info.sustainEnd) + (info.sustainPingPong ? " pingpong" : " fwd")) : std::string());
    return w;
}

} // namespace sf2scout
