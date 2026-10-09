// Renders the real editor to PNG without a display.   DropTwoSnapshot <out-dir>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

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

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshots"));
    out.createDirectory();

    DropTwoAudioProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    Head head;
    proc.setPlayHead (&head);
    std::unique_ptr<DropTwoEditor> ed (dynamic_cast<DropTwoEditor*> (proc.createEditor()));
    ed->applySize (100);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;

    auto runTo = [&] (double beat)
    {
        while (head.ppq < beat)
        {
            buf.clear();
            midi.clear();
            proc.processBlock (buf, midi);
            head.ppq += 512.0 / 48000.0 * head.bpm / 60.0;
        }
        proc.poll();
        for (int i = 0; i < 6; ++i) ed->pump (1.0f / 30.0f);
    };
    auto shoot = [&] (const juce::String& name, float scale = 1.0f)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
        auto f = out.getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("%s\n", f.getFullPathName().toRawUTF8());
    };

    proc.loadFactory (3);               // CALL & RESPONSE: every lane has something
    runTo (9.3);
    shoot ("drop2.png");
    shoot ("drop2@2x.png", 2.0f);
    proc.loadFactory (1);
    if (auto* p = proc.apvts.getParameter (ids::variant)) p->setValueNotifyingHost (p->convertTo0to1 (2.0f));
    proc.poll();
    ed->applyTheme (7);
    runTo (16 + 6.1);
    shoot ("theme-cyberdelia.png");
    proc.loadFactory (4);
    ed->applyTheme (4);
    runTo (32 + 13.0);
    shoot ("theme-ice.png");
    ed->applyTheme (11);
    proc.loadFactory (2);
    runTo (48 + 2.0);
    shoot ("theme-emigre.png");
    ed->applyTheme (0);
    ed.reset();
    return 0;
}
