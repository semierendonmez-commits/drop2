// One page:
//   DROP 1    the source: drums and bass (REC from the MIDI input, or drop .mid files)   |   IDENTITY
//   DROP II   the selected variant: drums, call, response (drag each out as MIDI)       |   VARIANTS A-D
//   HALF-TIME  /  KICK SHIFT  /  BASS LOCK  /  FLIP  /  CALL + RESPONSE
#pragma once

#include <juce_opengl/juce_opengl.h>
#include "PluginProcessor.h"
#include "ui/Header.h"
#include "ui/Views.h"

class DropTwoEditor final : public juce::AudioProcessorEditor,
                            public juce::FileDragAndDropTarget,
                            public juce::DragAndDropContainer,
                            public ui::Host,
                            private juce::Timer
{
public:
    static constexpr int kW = 1180, kH = 796;

    explicit DropTwoEditor (DropTwoAudioProcessor&);
    ~DropTwoEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    void pump (float dt);
    void applyTheme (int index);
    void applySize (int percent);

    bool isLocked (const juce::String&) const override { return false; }
    void setLocked (const juce::String&, bool) override {}
    bool modActive (const juce::String&) const override { return false; }
    float modValue (const juce::String&) const override { return 0.0f; }
    bool canModulate (const juce::String&) const override { return false; }
    void addMod (int, const juce::String&) override {}
    void modMenu (juce::PopupMenu&, const juce::String&) override {}
    void hint (const juce::String& s) override { if (s != status) { status = s; content.repaint (statusBox); } }

private:
    class Content final : public juce::Component
    {
    public:
        explicit Content (DropTwoEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override { ed.paintContent (g); }
        void paintOverChildren (juce::Graphics& g) override { ed.paintDropHint (g); }
        DropTwoEditor& ed;
    };

    void timerCallback() override { pump (1.0f / 30.0f); }
    void paintContent (juce::Graphics&);
    void paintDropHint (juce::Graphics&);
    void renderBackground();
    void layout();
    void showThemeMenu();
    void showViewMenu();
    void showPresetMenu();
    void updateLabels();
    void setGpu (bool on);
    void dragOut (const juce::String& what);
    void exportAll();
    int laneAt (juce::Point<int> p) const;    // DROP 1 lane under a point (content coordinates), -1 = none
    juce::RangedAudioParameter& param (const juce::String& id) { return *proc.apvts.getParameter (id); }
    template <typename C, typename... A> C& add (juce::Rectangle<int> r, const juce::String& tip, A&&... args);

    DropTwoAudioProcessor& proc;
    ui::Skin skin;
    ui::Look look { skin };
    Content content { *this };
    juce::TooltipWindow tooltips { this, 700 };
    juce::OpenGLContext gl;

    ui::Header header { skin, "DROP II", "switch-up generator  //  half-time  //  kick shift  //  bass lock  //  flip  //  call + response" };
    ui::PlateButton themeBtn { skin }, viewBtn { skin }, presetPrev { skin }, presetName { skin }, presetNext { skin }, seedBtn { skin };
    std::unique_ptr<ui::PlateButton> recBtn[2], clrBtn[2], dragBtn[3];
    ui::PlateButton dragAll { skin }, exportBtn { skin }, dragDrop1 { skin };
    ui::ClipView srcDrums { skin, proc, ui::ClipView::SrcDrums }, srcBass { skin, proc, ui::ClipView::SrcBass };
    ui::ClipView outDrums { skin, proc, ui::ClipView::OutDrums }, outCall { skin, proc, ui::ClipView::OutCall }, outResp { skin, proc, ui::ClipView::OutResp };
    ui::IdentityMeter meter { skin, proc };
    std::vector<std::unique_ptr<ui::VariantButton>> slots;
    std::unique_ptr<ui::InfoLine> htInfo;
    std::vector<std::unique_ptr<juce::Component>> controls;

    juce::Rectangle<int> wSrc, wOut, wId, wVar, wHt, wKick, wLock, wFlip, wSplit, statusBox, laneSrc[2];
    juce::String status, idle;
    juce::Image bg;
    int bgTheme = -1, dropLane = -1, lastGen = -1, frame = 0;
    float bgScale = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DropTwoEditor)
};
