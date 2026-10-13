#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using API = juce::AudioParameterInt;
using APB = juce::AudioParameterBool;

juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

std::unique_ptr<APF> fparam (const juce::String& id, const juce::String& name, float lo, float hi, float def,
                             std::function<juce::String (float, int)> text = pct)
{
    return std::make_unique<APF> (juce::ParameterID (id, 1), name, juce::NormalisableRange<float> (lo, hi), def,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text)));
}
std::unique_ptr<APC> cparam (const juce::String& id, const juce::String& name, const juce::StringArray& c, int def)
{
    return std::make_unique<APC> (juce::ParameterID (id, 1), name, c, def);
}
std::unique_ptr<APB> bparam (const juce::String& id, const juce::String& name, bool def)
{
    return std::make_unique<APB> (juce::ParameterID (id, 1), name, def);
}
std::unique_ptr<API> iparam (const juce::String& id, const juce::String& name, int lo, int hi, int def)
{
    return std::make_unique<API> (juce::ParameterID (id, 1), name, lo, hi, def);
}

juce::File settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Semi").getChildFile ("DROP II").getChildFile ("settings.xml");
}
juce::var readSetting (const juce::String& key, const juce::var& def)
{
    if (auto xml = juce::XmlDocument::parse (settingsFile()))
        if (xml->hasAttribute (key)) return xml->getStringAttribute (key);
    return def;
}
void writeSetting (const juce::String& key, const juce::String& value)
{
    auto f = settingsFile();
    f.getParentDirectory().createDirectory();
    std::unique_ptr<juce::XmlElement> xml = juce::XmlDocument::parse (f);
    if (xml == nullptr) xml = std::make_unique<juce::XmlElement> ("DROPII");
    xml->setAttribute (key, value);
    xml->writeTo (f);
}

struct PV { const char* id; float v; };
struct Factory { const char* name; std::vector<PV> values; };
const std::vector<Factory>& factories()
{
    static const std::vector<Factory> f {
        { "SWITCH-UP", {} },
        { "HALF-TIME DROP", { { "halftime", 1 }, { "identity", 0.5f }, { "kickshift", 0 }, { "basslock", 1 }, { "lockamt", 0.6f } } },
        { "ROLLER FLIP", { { "flip", 1 }, { "flipmode", 2 }, { "flipamt", 0.8f }, { "kickamt", 0.4f }, { "identity", 0.55f } } },
        { "CALL & RESPONSE", { { "split", 1 }, { "splitunit", 1 }, { "interval", 3 }, { "basslock", 1 }, { "identity", 0.6f } } },
        { "FULL VIP", { { "halftime", 1 }, { "flip", 1 }, { "flipmode", 3 }, { "split", 1 }, { "splitunit", 2 }, { "interval", 4 },
                        { "identity", 0.3f }, { "kickamt", 0.9f }, { "lockamt", 1.0f }, { "lockto", 1 } } },
        { "DROP 1 AGAIN", { { "identity", 1.0f } } },
    };
    return f;
}

// the drop that is there before you give it yours: two-step kicks, snares on 2 and 4, a reese line
dr2::Clip demoDrums()
{
    dr2::Clip c;
    c.length = 16.0;
    for (int bar = 0; bar < 4; ++bar)
    {
        const double o = bar * 4.0;
        c.notes.push_back ({ o + 0.0, 0.25, 36, 1.0f });
        c.notes.push_back ({ o + 2.5, 0.25, 36, 0.9f });
        if (bar % 2 == 1) c.notes.push_back ({ o + 3.25, 0.25, 36, 0.7f });
        c.notes.push_back ({ o + 1.0, 0.25, 38, 1.0f });
        c.notes.push_back ({ o + 3.0, 0.25, 38, 1.0f });
        c.notes.push_back ({ o + 1.75, 0.25, 38, 0.3f });
        if (bar == 3) c.notes.push_back ({ o + 3.5, 0.25, 38, 0.6f });
        for (int k = 0; k < 8; ++k) c.notes.push_back ({ o + k * 0.5 + 0.0, 0.125, (k % 4 == 3) ? 46 : 42, k % 2 ? 0.45f : 0.65f });
    }
    c.sort();
    return c;
}
dr2::Clip demoBass()
{
    dr2::Clip c;
    c.length = 16.0;
    const int line[] { 31, 31, 34, 29 };
    for (int bar = 0; bar < 4; ++bar)
    {
        const double o = bar * 4.0;
        c.notes.push_back ({ o + 0.0, 1.5, line[bar], 0.9f });
        c.notes.push_back ({ o + 1.75, 0.5, line[bar] + 12, 0.8f });
        c.notes.push_back ({ o + 2.4, 1.0, line[bar], 0.85f });
        if (bar % 2 == 1) c.notes.push_back ({ o + 3.5, 0.4, line[bar] + 7, 0.7f });
    }
    return c;
}

juce::XmlElement* clipToXml (const dr2::Clip& c, const juce::String& name)
{
    auto* e = new juce::XmlElement (name);
    e->setAttribute ("length", c.length);
    juce::StringArray parts;
    for (const auto& n : c.notes)
        parts.add (juce::String (n.start, 5) + ":" + juce::String (n.length, 5) + ":" + juce::String (n.pitch) + ":" + juce::String (n.velocity, 3));
    e->setAttribute ("notes", parts.joinIntoString (";"));
    return e;
}
dr2::Clip clipFromXml (const juce::XmlElement& e)
{
    dr2::Clip c;
    c.length = e.getDoubleAttribute ("length", 16.0);
    for (auto& part : juce::StringArray::fromTokens (e.getStringAttribute ("notes"), ";", ""))
    {
        auto f = juce::StringArray::fromTokens (part, ":", "");
        if (f.size() < 4) continue;
        c.notes.push_back ({ f[0].getDoubleValue(), f[1].getDoubleValue(), f[2].getIntValue(), (float) f[3].getDoubleValue() });
    }
    c.sort();
    return c;
}
} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout DropTwoAudioProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (fparam (ids::identity, "Identity", 0.0f, 1.0f, 0.6f));
    l.add (iparam (ids::seed, "Seed", 1, 9999, 1));
    l.add (cparam (ids::variant, "Variant", ids::variants, 0));
    l.add (bparam (ids::halftime, "Half-time", false));
    l.add (bparam (ids::hthats, "Half-time keeps hats", true));
    l.add (bparam (ids::kickshift, "Kick shift", true));
    l.add (fparam (ids::kickamt, "Kick shift amount", 0.0f, 1.0f, 0.7f));
    l.add (cparam (ids::kickmax, "Kick shift max", ids::kickMaxes, 1));
    l.add (bparam (ids::basslock, "Bass lock", true));
    l.add (fparam (ids::lockamt, "Bass lock amount", 0.0f, 1.0f, 0.8f));
    l.add (cparam (ids::lockto, "Bass lock to", ids::lockTos, 0));
    l.add (bparam (ids::flip, "Register flip", false));
    l.add (cparam (ids::flipmode, "Flip mode", ids::flipModes, 2));
    l.add (fparam (ids::flipamt, "Flip amount", 0.0f, 1.0f, 0.6f));
    l.add (bparam (ids::split, "Call / response", false));
    l.add (cparam (ids::splitunit, "Call unit", ids::units, 1));
    l.add (cparam (ids::interval, "Response interval", ids::intervals, 0));
    l.add (cparam (ids::play, "Play", ids::plays, 2));
    l.add (bparam (ids::preview, "Preview kit", true));
    l.add (fparam (ids::level, "Preview level", -24.0f, 6.0f, -3.0f, [] (float v, int) { return juce::String (v, 1) + " DB"; }));
    l.add (iparam (ids::drumch, "Drums channel", 1, 16, 1));
    l.add (iparam (ids::bassch, "Call channel", 1, 16, 2));
    l.add (iparam (ids::respch, "Response channel", 1, 16, 3));
    l.add (cparam (ids::recbars, "Record length", ids::recBars, 2));
    l.add (bparam (ids::run, "Internal clock", false));
    l.add (fparam (ids::bpm, "Tempo", 80.0f, 200.0f, 172.0f, [] (float v, int) { return juce::String (v, 1); }));
    return l;
}

DropTwoAudioProcessor::DropTwoAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DROPII", layout())
{
    auto P = [this] (const char* id) { return link.raw (id); };     // knob + CONDUIT
    raw.variant = P (ids::variant); raw.play = P (ids::play); raw.preview = P (ids::preview); raw.level = P (ids::level);
    raw.drumch = P (ids::drumch); raw.bassch = P (ids::bassch); raw.respch = P (ids::respch); raw.recbars = P (ids::recbars);
    raw.run = P (ids::run); raw.bpm = P (ids::bpm);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) apvts.addParameterListener (rp->getParameterID(), this);

    pSection = link.port ("SECTION"); pSwitch = link.port ("SWITCH"); pNote = link.port ("NOTE"); pNewSeed = link.port ("NEWSEED");
    // the variants are made on the message thread: a bay moving what they are made of makes them again (poll)
    for (const char* id : { ids::identity, ids::seed, ids::variant, ids::kickamt, ids::lockamt, ids::flipamt })
        regenParams.push_back (link.raw (id));
    regenOffsets.assign (regenParams.size(), 0.0f);
    // BLACKBOX: back to a recorded moment = its seed (the listener regenerates)
    link.onRecall = [this] (uint32_t s, int)
    {
        auto* p = apvts.getParameter (ids::seed);
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (1u + s % 9999u)));
    };

    theme = (int) readSetting ("theme", 0);
    recNotes.reserve (4096);
    recHeld.reserve (256);
    loadDemo();
    if (wrapperType == wrapperType_Standalone) apvts.getParameter (ids::run)->setValueNotifyingHost (1.0f);
    regenerate();
#if ! DR2_HEADLESS
    startTimerHz (30);
#endif
}

DropTwoAudioProcessor::~DropTwoAudioProcessor()
{
    stopTimer();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) apvts.removeParameterListener (rp->getParameterID(), this);
    delete live.exchange (nullptr);
}

void DropTwoAudioProcessor::loadDemo()
{
    srcDrums = demoDrums();
    srcBass = demoBass();
}

bool DropTwoAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void DropTwoAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    kit.prepare (sampleRate);
    lastEnd = -1.0;
}

//==============================================================================
dr2::Settings DropTwoAudioProcessor::currentSettings() const
{
    auto v = [this] (const char* id) { return link.raw (id)->load(); };
    auto i = [&] (const char* id) { return juce::roundToInt (v (id)); };
    dr2::Settings s;
    s.identity = v (ids::identity);
    s.halftime = v (ids::halftime) > 0.5f;
    s.halftimeHats = v (ids::hthats) > 0.5f;
    s.kickShift = v (ids::kickshift) > 0.5f;
    s.kickAmount = v (ids::kickamt);
    s.kickMax = i (ids::kickmax) + 1;
    s.bassLock = v (ids::basslock) > 0.5f;
    s.lockAmount = v (ids::lockamt);
    s.lockTo = i (ids::lockto);
    s.flip = v (ids::flip) > 0.5f;
    s.flipMode = i (ids::flipmode);
    s.flipAmount = v (ids::flipamt);
    s.split = v (ids::split) > 0.5f;
    s.splitUnit = i (ids::splitunit);
    s.respInterval = dr2::kIntervals[juce::jlimit (0, 6, i (ids::interval))];
    return s;
}

int DropTwoAudioProcessor::selectedVariant() const { return juce::jlimit (0, 3, juce::roundToInt (raw.variant->load())); }

double DropTwoAudioProcessor::loopLength() const { return juce::jmax (4.0, juce::jmax (srcDrums.length, srcBass.length)); }

void DropTwoAudioProcessor::regenerate()
{
    const auto s = currentSettings();
    const auto seed = (std::uint32_t) juce::roundToInt (link.raw (ids::seed)->load());
    for (size_t k = 0; k < regenParams.size(); ++k) regenOffsets[k] = regenParams[k]->offset();
    for (int v = 0; v < 4; ++v)
        results[(size_t) v] = dr2::generate (srcDrums, srcBass, s, seed * 31u + (std::uint32_t) v * 7919u + 1u);
    publish();
    dirty = false;
    generation.fetch_add (1);
}

void DropTwoAudioProcessor::publish()
{
    auto pb = std::make_unique<Playback>();
    pb->length = loopLength();
    const auto roles = dr2::detectRoles (srcDrums);

    auto addClip = [&] (std::vector<Event>& out, const dr2::Clip& c, int lane)
    {
        const double cl = juce::jmax (1.0, c.length);
        const int reps = juce::jmax (1, (int) std::floor (pb->length / cl + 1.0e-9));
        for (int r = 0; r < reps; ++r)
            for (const auto& n : c.notes)
            {
                const double t = n.start + r * cl;
                if (t >= pb->length - 1.0e-9) continue;
                const double e = juce::jmin (pb->length, t + juce::jmax (0.02, n.length));
                const auto role = lane == 0 ? roles.of (n.pitch) : dr2::Role::Perc;
                out.push_back ({ t, lane, n.pitch, n.velocity, true, role });
                out.push_back ({ e, lane, n.pitch, 0.0f, false, role });
            }
    };
    addClip (pb->drop1, srcDrums, 0);
    addClip (pb->drop1, srcBass, 1);
    const auto& r = results[(size_t) selectedVariant()];
    addClip (pb->drop2, r.drums, 0);
    addClip (pb->drop2, r.bassA, 1);
    addClip (pb->drop2, r.bassB, 2);
    for (auto* v : { &pb->drop1, &pb->drop2 })
        std::stable_sort (v->begin(), v->end(), [] (const Event& a, const Event& b) { return a.time < b.time || (a.time == b.time && ! a.on && b.on); });

    auto* old = live.exchange (pb.release());
    if (old != nullptr) retired.push_back ({ std::unique_ptr<Playback> (old), juce::Time::getMillisecondCounterHiRes() });
}

void DropTwoAudioProcessor::timerCallback() { poll(); }

void DropTwoAudioProcessor::poll()
{
    if (recDone.exchange (false))
    {
        if (! recNotes.empty())
        {
            dr2::Clip c;
            c.length = recLength;
            c.notes = recNotes;
            c.sort();
            setSource (recLane, std::move (c));
        }
        generation.fetch_add (1);
    }
    // CONDUIT: NEWSEED does what the SEED button does; a bay's offset on what the variants are made of remakes them
    if (link.triggered (pNewSeed)) newSeed();
    for (size_t k = 0; k < regenParams.size() && ! dirty; ++k)
        if (std::abs (regenParams[k]->offset() - regenOffsets[k]) > 1.0f / 256.0f) dirty = true;
    if (dirty) regenerate();
    const double now = juce::Time::getMillisecondCounterHiRes();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (auto& p) { return now - p.second > 1000.0; }), retired.end());
}

void DropTwoAudioProcessor::setSource (int lane, dr2::Clip c)
{
    c.length = juce::jlimit (4.0, 64.0, std::ceil (c.length / 4.0 - 1.0e-9) * 4.0);
    c.sort();
    (lane == LaneDrums ? srcDrums : srcBass) = std::move (c);
    regenerate();
}

void DropTwoAudioProcessor::clearSource (int lane)
{
    dr2::Clip c;
    c.length = loopLength();
    (lane == LaneDrums ? srcDrums : srcBass) = c;
    regenerate();
}

void DropTwoAudioProcessor::arm (int lane)
{
    armed = armed.load() == lane ? -1 : lane;   // pressing again disarms
}

//==============================================================================
bool DropTwoAudioProcessor::loadMidiFile (int lane, const juce::File& f, juce::String& error)
{
    juce::FileInputStream in (f);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in)) { error = "not a MIDI file"; return false; }
    const int tpq = mf.getTimeFormat();
    if (tpq <= 0) { error = "SMPTE-timed MIDI files are not supported"; return false; }

    dr2::Clip c;
    double minStart = 1e9, maxEnd = 0.0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto* e = seq.getEventPointer (i);
            if (! e->message.isNoteOn()) continue;
            const double s = e->message.getTimeStamp() / tpq;
            const double end = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() / tpq : s + 0.25;
            c.notes.push_back ({ s, juce::jmax (0.02, end - s), e->message.getNoteNumber(), e->message.getFloatVelocity() });
            minStart = juce::jmin (minStart, s);
            maxEnd = juce::jmax (maxEnd, end);
        }
    }
    if (c.notes.empty()) { error = "no notes in " + f.getFileName(); return false; }
    const double offset = std::floor (minStart / 4.0) * 4.0;   // a clip that starts at bar 9 of the song still starts at 0 here
    for (auto& n : c.notes) n.start -= offset;
    c.length = juce::jmax (4.0, std::ceil ((maxEnd - offset) / 4.0 - 0.02) * 4.0);
    setSource (lane, std::move (c));
    return true;
}

juce::File DropTwoAudioProcessor::writeMidi (const juce::String& what)
{
    const auto& r = results[(size_t) selectedVariant()];
    constexpr int tpq = 960;
    auto track = [&] (const dr2::Clip& c, int ch, const juce::String& name)
    {
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0.0);
        for (const auto& n : c.notes)
        {
            const double on = n.start * tpq, off = (n.start + n.length) * tpq;
            seq.addEvent (juce::MidiMessage::noteOn (ch, n.pitch, juce::jlimit (0.01f, 1.0f, n.velocity)), on);
            seq.addEvent (juce::MidiMessage::noteOff (ch, n.pitch), off);
        }
        seq.addEvent (juce::MidiMessage::endOfTrack(), juce::jmax (c.length, loopLength()) * tpq);
        seq.updateMatchedPairs();
        return seq;
    };
    const int dch = juce::roundToInt (raw.drumch->load()), bch = juce::roundToInt (raw.bassch->load()), rch = juce::roundToInt (raw.respch->load());

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    if (what == "DRUMS") mf.addTrack (track (r.drums, dch, "DROP II DRUMS"));
    else if (what == "CALL") mf.addTrack (track (r.bassA, bch, "DROP II CALL"));
    else if (what == "RESPONSE") mf.addTrack (track (r.bassB, rch, "DROP II RESPONSE"));
    else if (what == "BASS")
    {
        dr2::Clip both = r.bassA;
        both.notes.insert (both.notes.end(), r.bassB.notes.begin(), r.bassB.notes.end());
        both.sort();
        mf.addTrack (track (both, bch, "DROP II BASS"));
    }
    else if (what == "DROP1")
    {
        mf.addTrack (track (srcDrums, dch, "DROP 1 DRUMS"));
        mf.addTrack (track (srcBass, bch, "DROP 1 BASS"));
    }
    else
    {
        mf.addTrack (track (r.drums, dch, "DROP II DRUMS"));
        mf.addTrack (track (r.bassA, bch, "DROP II CALL"));
        if (! r.bassB.notes.empty()) mf.addTrack (track (r.bassB, rch, "DROP II RESPONSE"));
    }

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("DROP II");
    dir.createDirectory();
    const auto name = "DROP II " + ids::variants[selectedVariant()] + " " + what + ".mid";
    auto f = dir.getChildFile (name);
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (os.openedOk()) mf.writeTo (os);
    return f;
}

//==============================================================================
void DropTwoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    bool playing = false;
    double ppq = 0.0, bpm = 120.0;
    bool hostValid = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (pos->getPpqPosition().hasValue())
            {
                hostValid = true;
                playing = pos->getIsPlaying();
                ppq = *pos->getPpqPosition();
                bpm = pos->getBpm().orFallback (120.0);
            }
    if (! (hostValid && playing) && raw.run->load() > 0.5f)
    {
        playing = true;
        bpm = raw.bpm->load();
        ppq = internalPpq;
        internalPpq += n / sr * bpm / 60.0;
    }
    const double bps = bpm / 60.0 / sr;
    const double end = ppq + n * bps;

    // ---- record drop 1 from the input --------------------------------------------------------
    const int arm = armed.load();
    if (arm >= 0 && ! recActive.load() && playing)
    {
        const double bar = std::ceil (ppq / 4.0 - 1.0e-9) * 4.0;
        if (bar < end)
        {
            recStart = bar;
            recLength = ids::recBarValues[juce::jlimit (0, 3, juce::roundToInt (raw.recbars->load()))] * 4.0;
            recLane = arm;
            recNotes.clear();
            recHeld.clear();
            recActive = true;
        }
    }
    if (recActive.load())
    {
        for (const auto m : midi)
        {
            const auto& msg = m.getMessage();
            const double t = ppq + m.samplePosition * bps - recStart;
            if (t < 0.0 || t >= recLength) continue;
            if (msg.isNoteOn() && recHeld.size() < recHeld.capacity())
                recHeld.push_back ({ msg.getNoteNumber(), t, msg.getFloatVelocity() });
            else if (msg.isNoteOff())
                for (size_t i = 0; i < recHeld.size(); ++i)
                    if (recHeld[i].pitch == msg.getNoteNumber())
                    {
                        if (recNotes.size() < recNotes.capacity())
                            recNotes.push_back ({ recHeld[i].start, juce::jmax (0.02, t - recHeld[i].start), recHeld[i].pitch, recHeld[i].vel });
                        recHeld.erase (recHeld.begin() + (long) i);
                        break;
                    }
        }
        recProgressV = (float) juce::jlimit (0.0, 1.0, (end - recStart) / recLength);
        if (! playing || end >= recStart + recLength)
        {
            for (auto& h : recHeld)
                if (recNotes.size() < recNotes.capacity())
                    recNotes.push_back ({ h.start, juce::jmax (0.02, recLength - h.start), h.pitch, h.vel });
            recHeld.clear();
            recActive = false;
            armed = -1;
            recDone = true;
        }
    }
    midi.clear();

    // ---- play ----------------------------------------------------------------------------------
    Playback* pb = live.load (std::memory_order_acquire);
    const int play = juce::roundToInt (raw.play->load());
    const int ch[3] { juce::roundToInt (raw.drumch->load()), juce::roundToInt (raw.bassch->load()), juce::roundToInt (raw.respch->load()) };
    const bool kitOn = raw.preview->load() > 0.5f;
    // a mono bus gets the kit once into the one buffer (render adds to L and R, which are the same)
    const float level = juce::Decibels::decibelsToGain (raw.level->load()) * (buffer.getNumChannels() > 1 ? 1.0f : 0.5f);

    auto allOff = [&] (int offset)
    {
        for (int lane = 0; lane < 3; ++lane)
            for (int p = 0; p < 128; ++p)
                if (sounding[(size_t) lane][(size_t) p])
                {
                    midi.addEvent (juce::MidiMessage::noteOff (ch[lane], p), offset);
                    sounding[(size_t) lane][(size_t) p] = false;
                }
        kit.reset();
    };

    if (! playing || pb == nullptr || play == 0)
    {
        allOff (0);
        lastEnd = -1.0;
        loopPos = -1.0f;
        lastPlay = play;
        lastPb = pb;
        return;
    }
    // SWITCH: what plays changed (DROP 1 / DROP II, or new variants): a = the variant, b = 1 drop 1, 2 drop II
    if (pb != lastPb || play != lastPlay) link.emit (pSwitch, play == 2 ? juce::roundToInt (raw.variant->load()) : -1, play, 0.0f, 0, ppq);
    if (pb != lastPb || play != lastPlay || std::abs (ppq - lastEnd) > 1.0e-3) allOff (0);
    lastPb = pb;
    lastPlay = play;
    lastEnd = end;

    const auto& evs = play == 1 ? pb->drop1 : pb->drop2;
    const double L = pb->length;
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : outL;
    int rendered = 0;
    for (double k = std::floor (ppq / L) * L; k < end; k += L)
    {
        // SECTION: the loop starts again: a = 1 drop 1, 2 drop II, f = its length in bars
        if (k >= ppq) link.emit (pSection, play, 0, (float) (L / 4.0), 0, k);
        for (const auto& e : evs)
        {
            const double t = k + e.time;
            if (t < ppq || t >= end) continue;
            const int off = juce::jlimit (0, n - 1, (int) ((t - ppq) / bps));
            if (kitOn && off > rendered) { kit.render (outL, outR, rendered, off, level); rendered = off; }
            auto& s = sounding[(size_t) e.lane][(size_t) e.pitch];
            if (e.on)
            {
                if (s) midi.addEvent (juce::MidiMessage::noteOff (ch[e.lane], e.pitch), off);
                midi.addEvent (juce::MidiMessage::noteOn (ch[e.lane], e.pitch, juce::jlimit (0.01f, 1.0f, e.vel)), off);
                // NOTE: a = the pitch, b = velocity, u = the lane (0 drums, 1 bass / call, 2 response)
                link.emit (pNote, e.pitch, juce::jlimit (1, 127, juce::roundToInt (e.vel * 127.0f)), 0.0f, (uint32_t) e.lane, t);
                s = true;
                kit.noteOn ((dr2::Preview::Lane) e.lane, e.role, e.pitch, e.vel);
            }
            else if (s)
            {
                midi.addEvent (juce::MidiMessage::noteOff (ch[e.lane], e.pitch), off);
                s = false;
                kit.noteOff ((dr2::Preview::Lane) e.lane, e.pitch);
            }
        }
    }
    if (kitOn) kit.render (outL, outR, rendered, n, level);
    loopPos = (float) std::fmod (end, L);
}

//==============================================================================
juce::StringArray DropTwoAudioProcessor::factoryNames()
{
    juce::StringArray s;
    for (auto& f : factories()) s.add (f.name);
    return s;
}

void DropTwoAudioProcessor::loadFactory (int index)
{
    const auto& all = factories();
    index = (index % (int) all.size() + (int) all.size()) % (int) all.size();
    static const juce::StringArray keep { ids::seed, ids::variant, ids::play, ids::preview, ids::level, ids::drumch, ids::bassch,
                                          ids::respch, ids::recbars, ids::run, ids::bpm };
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! keep.contains (rp->getParameterID())) rp->setValueNotifyingHost (rp->getDefaultValue());
    for (const auto& pv : all[(size_t) index].values)
        if (auto* rp = apvts.getParameter (pv.id)) rp->setValueNotifyingHost (rp->convertTo0to1 (pv.v));
    factory = index;
    regenerate();
}

void DropTwoAudioProcessor::newSeed()
{
    auto* p = apvts.getParameter (ids::seed);
    p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::Random::getSystemRandom().nextInt ({ 1, 9999 })));
    regenerate();
}

void DropTwoAudioProcessor::setTheme (int t) { theme = t; writeSetting ("theme", juce::String (t)); }
int DropTwoAudioProcessor::sizeSetting() { return juce::jlimit (60, 200, (int) readSetting ("size", 100)); }
void DropTwoAudioProcessor::setSizeSetting (int pct) { writeSetting ("size", juce::String (pct)); }
bool DropTwoAudioProcessor::motionSetting() { return (int) readSetting ("motion", 1) != 0; }
void DropTwoAudioProcessor::setMotionSetting (bool m) { writeSetting ("motion", m ? "1" : "0"); }
bool DropTwoAudioProcessor::gpuSetting() { return (int) readSetting ("gpu", 1) != 0; }
void DropTwoAudioProcessor::setGpuSetting (bool on) { writeSetting ("gpu", on ? "1" : "0"); }

void DropTwoAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    auto* extra = xml->createNewChildElement ("DROPII");
    extra->setAttribute ("theme", theme.load());
    extra->setAttribute ("preset", factory);
    extra->addChildElement (clipToXml (srcDrums, "DRUMS"));
    extra->addChildElement (clipToXml (srcBass, "BASS"));
    copyXmlToBinary (*xml, dest);
}

void DropTwoAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (auto* extra = xml->getChildByName ("DROPII"))
        {
            theme = extra->getIntAttribute ("theme", theme.load());
            factory = extra->getIntAttribute ("preset", -1);
            if (auto* d = extra->getChildByName ("DRUMS")) srcDrums = clipFromXml (*d);
            if (auto* b = extra->getChildByName ("BASS")) srcBass = clipFromXml (*b);
            xml->removeChildElement (extra, true);
        }
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
        regenerate();
    }
}

juce::AudioProcessorEditor* DropTwoAudioProcessor::createEditor() { return new DropTwoEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DropTwoAudioProcessor(); }
