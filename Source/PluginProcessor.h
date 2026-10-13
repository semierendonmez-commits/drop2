// DROP II: give it drop 1 (a drum clip and a bass clip), it writes drop 2.
// Four variants at a time, one IDENTITY knob over all the moves, drag the
// result out as MIDI or play it live (MIDI out, or the built-in preview kit).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "engine/Transform.h"
#include "engine/Preview.h"
#include "Params.h"
#include <family/FamilyLink.h>
#include "FamilyManifest.h"

class DropTwoAudioProcessor final : public juce::AudioProcessor,
                                    private juce::AudioProcessorValueTreeState::Listener,
                                    private juce::Timer
{
public:
    DropTwoAudioProcessor();
    ~DropTwoAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "DROP II"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    // the family bus (CONDUIT / SETBOOK / BLACKBOX / DATASCREEN): after apvts, it reads the parameters
    family::Link link { *this, apvts, family::kManifest };
    void updateTrackProperties (const TrackProperties& p) override { if (p.name.has_value()) link.setInstanceName (*p.name); }

    // ---- the source and the four variants (message thread)
    enum Lane { LaneDrums = 0, LaneBass = 1 };
    const dr2::Clip& source (int lane) const { return lane == LaneDrums ? srcDrums : srcBass; }
    void setSource (int lane, dr2::Clip c);
    void clearSource (int lane);
    bool loadMidiFile (int lane, const juce::File& f, juce::String& error);
    const std::array<dr2::Result, 4>& variants() const { return results; }
    int selectedVariant() const;
    dr2::Settings currentSettings() const;
    void regenerate();                       // now (otherwise it happens on the next timer tick)
    void poll();                             // picks up a finished recording, regenerates if dirty (the timer calls it)

    // MIDI files of the selected variant, for dragging into the DAW
    juce::File writeMidi (const juce::String& what);   // "DRUMS", "CALL", "RESPONSE", "BASS", "ALL", "DROP1"

    // ---- recording drop 1 from the MIDI input
    void arm (int lane);                     // record the next N bars into this lane
    int armedLane() const { return armed.load(); }
    bool recording() const { return recActive.load(); }
    float recProgress() const { return recProgressV.load(); }

    // ---- playhead for the editor: beats into the loop, -1 = stopped
    float loopPosition() const { return loopPos.load(); }
    double loopLength() const;

    static juce::StringArray factoryNames();
    void loadFactory (int index);
    int factoryIndex() const { return factory; }
    juce::String presetName() const { return factory >= 0 ? factoryNames()[factory] : juce::String ("CUSTOM"); }
    void newSeed();

    int getTheme() const { return theme.load(); }
    void setTheme (int t);
    static int sizeSetting();
    static void setSizeSetting (int pct);
    static bool motionSetting();
    static void setMotionSetting (bool);
    static bool gpuSetting();
    static void setGpuSetting (bool);

    std::atomic<int> generation { 0 };       // bumps when the variants change (the editor repaints)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void parameterChanged (const juce::String&, float) override { dirty = true; }
    void timerCallback() override;
    void publish();
    void loadDemo();

    // what the audio thread plays: both versions, flattened into note events
    struct Event { double time; int lane; int pitch; float vel; bool on; dr2::Role role; };
    struct Playback
    {
        double length = 16.0;
        std::vector<Event> drop1, drop2;
    };
    std::atomic<Playback*> live { nullptr };
    std::vector<std::pair<std::unique_ptr<Playback>, double>> retired;   // freed a second after they stop being live

    dr2::Clip srcDrums, srcBass;
    std::array<dr2::Result, 4> results;
    std::atomic<bool> dirty { true };

    // recording
    std::atomic<int> armed { -1 };
    std::atomic<bool> recActive { false }, recDone { false };
    std::atomic<float> recProgressV { 0.0f };
    double recStart = 0.0, recLength = 16.0;
    struct Held { int pitch; double start; float vel; };
    std::vector<dr2::Note> recNotes;
    std::vector<Held> recHeld;
    int recLane = -1;

    // playback state (audio thread)
    dr2::Preview kit;
    std::array<std::array<bool, 128>, 3> sounding {};
    double lastEnd = -1.0, internalPpq = 0.0, sr = 48000.0;
    int lastPlay = -1;
    Playback* lastPb = nullptr;
    std::atomic<float> loopPos { -1.0f };

    std::atomic<int> theme { 0 };
    int factory = 0;

    struct Raw
    {
        const family::Param *variant, *play, *preview, *level, *drumch, *bassch, *respch, *recbars, *run, *bpm;
    } raw {};

    // ---- family ports (looked up once); the bays' offsets the variants were last made with
    int pSection = -1, pSwitch = -1, pNote = -1, pNewSeed = -1;
    std::vector<const family::Param*> regenParams;
    std::vector<float> regenOffsets;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DropTwoAudioProcessor)
};
