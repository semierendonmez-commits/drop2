// The real plug-in on a fake host: playback, recording, MIDI files, state, styles.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <map>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL line %d  ", __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

struct Head final : juce::AudioPlayHead
{
    double ppq = 0, bpm = 172; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setBpm (bpm); i.setPpqPosition (ppq); i.setIsPlaying (playing);
        return i;
    }
};

static void set (DropTwoAudioProcessor& p, const juce::String& id, float v)
{
    auto* rp = p.apvts.getParameter (id);
    rp->setValueNotifyingHost (rp->convertTo0to1 (v));
}

struct Rig
{
    DropTwoAudioProcessor p;
    Head head;
    juce::AudioBuffer<float> buf { 2, 512 };
    std::map<std::pair<int, int>, int> held;    // (channel, note) -> count
    std::map<int, int> ons;                      // channel -> note-ons
    double peak = 0.0;
    Rig() { p.setPlayConfigDetails (0, 2, 48000.0, 512); p.prepareToPlay (48000.0, 512); p.setPlayHead (&head); }
    void run (double beats, std::function<void (juce::MidiBuffer&, double ppq)> feed = {})
    {
        const double step = 512.0 / 48000.0 * head.bpm / 60.0;
        for (double b = 0; b < beats; b += step)
        {
            juce::MidiBuffer midi;
            if (feed) feed (midi, head.ppq);
            buf.clear();
            p.processBlock (buf, midi);
            for (const auto m : midi)
            {
                const auto& msg = m.getMessage();
                if (msg.isNoteOn()) { held[{ msg.getChannel(), msg.getNoteNumber() }]++; ons[msg.getChannel()]++; }
                else if (msg.isNoteOff()) held[{ msg.getChannel(), msg.getNoteNumber() }]--;
            }
            peak = std::max (peak, (double) buf.getMagnitude (0, 512));
            if (head.playing) head.ppq += step;
        }
    }
    int hanging() const { int h = 0; for (auto& [k, v] : held) h += std::abs (v); return h; }
};

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // ---- the demo drop plays: drums on ch 1, bass on ch 2, the kit is heard
    {
        Rig r;
        r.run (32.0);
        CHECK (r.ons[1] > 40, "drum note-ons %d", r.ons[1]);
        CHECK (r.ons[2] > 10, "bass note-ons %d", r.ons[2]);
        CHECK (r.peak > 0.05, "preview kit silent (%f)", r.peak);
        r.head.playing = false;
        r.run (1.0);
        CHECK (r.hanging() == 0, "notes left hanging after stop: %d", r.hanging());
    }

    // ---- PLAY OFF: nothing
    {
        Rig r;
        set (r.p, ids::play, 0.0f);
        r.run (16.0);
        CHECK (r.ons.empty(), "PLAY OFF still played");
    }

    // ---- call + response: the answer comes on channel 3
    {
        Rig r;
        set (r.p, ids::split, 1.0f);
        r.p.regenerate();
        r.run (32.0);
        CHECK (r.ons[3] > 4, "response notes on ch 3: %d", r.ons[3]);
    }

    // ---- REC: one bar of kicks on every beat, starting on the next bar
    {
        Rig r;
        set (r.p, ids::recbars, 0.0f);   // 1 bar
        r.head.ppq = 2.0;                // halfway through bar 1: recording starts at beat 4
        r.p.arm (0);
        r.run (10.0, [&] (juce::MidiBuffer& m, double ppq)
        {
            const double step = 512.0 / 48000.0 * 172.0 / 60.0;
            for (int beat = 0; beat < 12; ++beat)
            {
                if (beat >= ppq && beat < ppq + step)
                {
                    const int off = juce::jmin (511, (int) std::ceil ((beat - ppq) / step * 512.0));
                    m.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), off);
                    m.addEvent (juce::MidiMessage::noteOff (1, 36), juce::jmin (511, off + 200));
                }
            }
        });
        r.p.poll();
        const auto& c = r.p.source (0);
        CHECK (c.notes.size() == 4, "recorded %zu notes", c.notes.size());
        CHECK (std::abs (c.length - 4.0) < 1e-9, "recorded length %f", c.length);
        if (c.notes.size() == 4)
            for (int i = 0; i < 4; ++i) CHECK (std::abs (c.notes[(size_t) i].start - i) < 0.02, "note %d at %f", i, c.notes[(size_t) i].start);
    }

    // ---- MIDI file out and back in
    {
        DropTwoAudioProcessor p;
        auto f = p.writeMidi ("DRUMS");
        CHECK (f.existsAsFile(), "no file");
        const auto want = p.variants()[(size_t) p.selectedVariant()].drums.notes.size();
        DropTwoAudioProcessor q;
        juce::String err;
        CHECK (q.loadMidiFile (0, f, err), "load failed: %s", err.toRawUTF8());
        CHECK (q.source (0).notes.size() == want, "round trip %zu vs %zu", q.source (0).notes.size(), want);
        f = p.writeMidi ("ALL");
        juce::FileInputStream in (f);
        juce::MidiFile mf;
        CHECK (mf.readFrom (in) && mf.getNumTracks() >= 2, "ALL has %d tracks", mf.getNumTracks());
    }

    // ---- state: the source clips travel with the project
    {
        DropTwoAudioProcessor a, b;
        dr2::Clip c;
        c.length = 8.0;
        c.notes.push_back ({ 0.0, 0.5, 40, 1.0f });
        c.notes.push_back ({ 5.0, 0.5, 44, 0.5f });
        a.setSource (0, c);
        set (a, ids::identity, 0.33f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (b.source (0).notes.size() == 2 && std::abs (b.source (0).notes[1].start - 5.0) < 1e-6, "clip not restored");
        CHECK (std::abs (b.source (0).length - 8.0) < 1e-9, "length not restored");
        CHECK (std::abs (b.apvts.getRawParameterValue (ids::identity)->load() - 0.33f) < 1e-3f, "identity not restored");
    }

    // ---- every style generates four sane variants
    for (int s = 0; s < DropTwoAudioProcessor::factoryNames().size(); ++s)
    {
        DropTwoAudioProcessor p;
        p.loadFactory (s);
        for (auto& v : p.variants())
        {
            CHECK (v.identity >= 0.0f && v.identity <= 1.0f, "identity %f", v.identity);
            CHECK (! v.drums.notes.empty(), "%s: empty drums", DropTwoAudioProcessor::factoryNames()[s].toRawUTF8());
        }
        if (s == 5) for (auto& v : p.variants()) CHECK (v.identity > 0.99f, "DROP 1 AGAIN is not drop 1 (%f)", v.identity);
    }

    // ---- the family bus: the manifest matches, the loop and its notes reach the bus, IDENTITY from a bay remakes the variants
    {
        Rig r;
        for (const auto& e : r.p.link.getManifest().errors) std::printf ("  manifest: %s\n", e.toRawUTF8());
        CHECK (r.p.link.getManifest().errors.isEmpty(), "family.json does not match the parameters");
        CHECK (r.p.link.onBus(), "not on the family bus");
        familybus::Hub reader;
        familybus::Hub::Options o; o.dataScreen = false; o.epoch = false;
        reader.open (o);
        reader.poll ([] (const familybus::Event&) {});
        r.run (32.0);
        int notes = 0, sections = 0, switches = 0;
        reader.poll ([&] (const familybus::Event& e)
        {
            if (e.source != r.p.link.getNode().slotIndex()) return;
            notes += e.type == familybus::EvNote; sections += e.type == familybus::EvSection; switches += e.type == familybus::EvSwitch;
        });
        CHECK (notes == r.ons[1] + r.ons[2] + r.ons[3], "notes on the bus %d, played %d", notes, r.ons[1] + r.ons[2] + r.ons[3]);
        CHECK (sections >= 2 && switches == 1, "sections %d, switches %d", sections, switches);
        set (r.p, ids::identity, 0.2f);
        r.p.poll();
        const float before = r.p.variants()[0].identity;
        familybus::Bay bay;
        bay.open ("TEST");
        bay.heartbeat();
        bay.setMod (r.p.link.getNode().slotIndex(), r.p.link.port ("IDENTITY"), 0.8f);
        r.p.poll();
        CHECK (r.p.variants()[0].identity > before + 0.2f, "IDENTITY from the bus: %f -> %f", before, r.p.variants()[0].identity);
        bay.close();
    }

    std::printf ("%d / %d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
