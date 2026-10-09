#include "PluginEditor.h"

namespace
{
juce::Rectangle<int> contentOf (juce::Rectangle<int> w) { return w.reduced (4).withTrimmedTop (19); }
const char* srcNames[] { "DRUMS", "BASS" };
const char* outNames[] { "DRUMS", "CALL", "RESPONSE" };
const char* dragWhat[] { "DRUMS", "CALL", "RESPONSE" };
}

template <typename C, typename... A>
C& DropTwoEditor::add (juce::Rectangle<int> r, const juce::String& tip, A&&... args)
{
    auto c = std::make_unique<C> (std::forward<A> (args)...);
    c->setTooltip (tip);
    c->setBounds (r);
    content.addAndMakeVisible (*c);
    auto& ref = *c;
    controls.push_back (std::move (c));
    return ref;
}

//==============================================================================
DropTwoEditor::DropTwoEditor (DropTwoAudioProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p)
{
    skin.index = proc.getTheme();
    skin.motion = DropTwoAudioProcessor::motionSetting();
    look.refreshColours();
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    content.addAndMakeVisible (header);

    themeBtn.caption = "THEME";
    themeBtn.onClick = [this] { showThemeMenu(); };
    viewBtn.caption = "VIEW";
    viewBtn.text = "SIZE";
    viewBtn.onClick = [this] { showViewMenu(); };
    presetPrev.text = "<";
    presetNext.text = ">";
    presetPrev.onClick = [this] { proc.loadFactory (proc.factoryIndex() - 1); updateLabels(); };
    presetNext.onClick = [this] { proc.loadFactory (proc.factoryIndex() + 1); updateLabels(); };
    presetName.caption = "STYLE";
    presetName.onClick = [this] { showPresetMenu(); };
    presetName.setTooltip ("Styles: which moves are on and how hard. Your drop 1 stays.");
    seedBtn.caption = "SEED";
    seedBtn.onClick = [this] { proc.newSeed(); updateLabels(); };
    seedBtn.setTooltip ("Four new variants from the same settings");
    for (auto* b : { &themeBtn, &viewBtn, &presetPrev, &presetName, &presetNext, &seedBtn }) content.addAndMakeVisible (*b);

    for (int i = 0; i < 2; ++i)
    {
        recBtn[i] = std::make_unique<ui::PlateButton> (skin);
        recBtn[i]->text = "REC";
        recBtn[i]->onClick = [this, i] { proc.arm (i); };
        recBtn[i]->setTooltip (juce::String ("Record the ") + srcNames[i] + " of drop 1 from this plug-in's MIDI input. It starts on the next bar and runs for REC LENGTH.");
        clrBtn[i] = std::make_unique<ui::PlateButton> (skin);
        clrBtn[i]->text = "CLEAR";
        clrBtn[i]->onClick = [this, i] { proc.clearSource (i); };
        content.addAndMakeVisible (*recBtn[i]);
        content.addAndMakeVisible (*clrBtn[i]);
    }
    for (int i = 0; i < 3; ++i)
    {
        dragBtn[i] = std::make_unique<ui::PlateButton> (skin);
        dragBtn[i]->text = "DRAG .MID";
        dragBtn[i]->onDragOut = [this, i] { dragOut (dragWhat[i]); };
        dragBtn[i]->onClick = [this] { hint ("Drag the button itself onto a track of your DAW: it carries a .mid file."); };
        dragBtn[i]->setTooltip (juce::String ("Drag this onto a track: the ") + outNames[i] + " of the selected variant as a .mid file");
        content.addAndMakeVisible (*dragBtn[i]);
    }
    dragAll.text = "DRAG ALL";
    dragAll.onDragOut = [this] { dragOut ("ALL"); };
    dragAll.onClick = [this] { hint ("Drag DRAG ALL onto the DAW: one .mid with drums, call and response as separate tracks."); };
    dragAll.setTooltip ("One .mid file, a track each for drums, call and response");
    exportBtn.text = "EXPORT";
    exportBtn.onClick = [this] { exportAll(); };
    exportBtn.setTooltip ("Write the selected variant (all lanes, and each lane alone) to Music / DROP II");
    dragDrop1.text = "DROP 1";
    dragDrop1.onDragOut = [this] { dragOut ("DROP1"); };
    dragDrop1.onClick = [this] { hint ("Drag DROP 1 out to get the source back as a .mid (what you recorded or dropped in)."); };
    for (auto* b : { &dragAll, &exportBtn, &dragDrop1 }) content.addAndMakeVisible (*b);

    for (auto* c : std::initializer_list<juce::Component*> { &srcDrums, &srcBass, &outDrums, &outCall, &outResp, &meter })
        content.addAndMakeVisible (*c);
    for (int v = 0; v < 4; ++v)
    {
        slots.push_back (std::make_unique<ui::VariantButton> (skin, proc, v));
        slots.back()->setTooltip ("Variant " + ids::variants[v] + ": same moves, another roll of the dice");
        content.addAndMakeVisible (*slots.back());
    }

    layout();
    idle = "Drop 1 goes in at the top (REC, or drop .mid files on a lane). Drop II comes out below: drag it into your DAW.";
    status = idle;
    content.setBounds (0, 0, kW, kH);
    applySize (DropTwoAudioProcessor::sizeSetting());
    updateLabels();
    pump (0.0f);
#if ! DR2_TOOL
    setGpu (DropTwoAudioProcessor::gpuSetting());
    startTimerHz (30);
#endif
}

DropTwoEditor::~DropTwoEditor()
{
    stopTimer();
    gl.detach();
    setLookAndFeel (nullptr);
}

void DropTwoEditor::setGpu (bool on)
{
    if (on && ! gl.isAttached()) { gl.setComponentPaintingEnabled (true); gl.setContinuousRepainting (false); gl.attachTo (*this); }
    else if (! on && gl.isAttached()) gl.detach();
}

void DropTwoEditor::applySize (int percent) { setSize (kW * percent / 100, kH * percent / 100); }
void DropTwoEditor::resized() { content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kW)); }

void DropTwoEditor::applyTheme (int index)
{
    skin.index = index;
    proc.setTheme (index);
    look.refreshColours();
    bgTheme = -1;
    updateLabels();
    content.repaint();
    for (auto* c : content.getChildren()) c->repaint();
}

//==============================================================================
void DropTwoEditor::layout()
{
    header.setBounds (0, 0, kW, 104);
    header.statusRight = 380;
    themeBtn.setBounds (kW - 160, 26, 70, 42);
    viewBtn.setBounds (kW - 84, 26, 70, 42);

    presetPrev.setBounds (10, 120, 26, 30);
    presetName.setBounds (38, 108, 210, 42);
    presetNext.setBounds (250, 120, 26, 30);
    seedBtn.setBounds (288, 108, 120, 42);
    add<ui::MenuCell> ({ 420, 108, 130, 42 }, "What plays while the host runs: nothing, drop 1, or the selected variant (MIDI out + the preview kit)",
                       skin, param (ids::play), "PLAY", this);
    add<ui::Toggle> ({ 560, 120, 112, 30 }, "The built-in kit and basses play along, so you hear drop 2 without routing MIDI",
                     skin, param (ids::preview), "PREVIEW KIT", "MIDI ONLY", this);
    add<ui::MenuCell> ({ 682, 108, 120, 42 }, "How many bars REC captures", skin, param (ids::recbars), "REC LENGTH", this);
    add<ui::Toggle> ({ 812, 120, 118, 30 }, "Run on the internal clock (TEMPO) when the host is stopped or absent",
                     skin, param (ids::run), "INT CLOCK", "HOST CLOCK", this);
    add<ui::HBar> ({ 940, 108, 112, 42 }, "Internal clock tempo", skin, param (ids::bpm), "TEMPO", this);
    add<ui::HBar> ({ 1060, 108, 110, 42 }, "Preview kit level", skin, param (ids::level), "KIT LEVEL", this);

    wSrc = { 10, 158, 790, 212 };
    wOut = { 10, 378, 790, 258 };
    wId = { 808, 158, 362, 212 };
    wVar = { 808, 378, 362, 258 };
    statusBox = { 10, 772, kW - 20, 18 };

    const auto sc = contentOf (wSrc);
    for (int i = 0; i < 2; ++i)
    {
        const int y = sc.getY() + 2 + i * 92;
        recBtn[i]->setBounds (sc.getX() + 2, y + 14, 88, 34);
        clrBtn[i]->setBounds (sc.getX() + 2, y + 52, 88, 34);
        laneSrc[i] = { sc.getX(), y, sc.getWidth(), 88 };
    }
    srcDrums.setBounds (sc.getX() + 96, sc.getY() + 2, sc.getWidth() - 98, 88);
    srcBass.setBounds (sc.getX() + 96, sc.getY() + 94, sc.getWidth() - 98, 88);

    const auto oc = contentOf (wOut);
    ui::ClipView* outs[] { &outDrums, &outCall, &outResp };
    for (int i = 0; i < 3; ++i)
    {
        const int y = oc.getY() + 2 + i * 77;
        dragBtn[i]->setBounds (oc.getX() + 2, y + 22, 88, 40);
        outs[i]->setBounds (oc.getX() + 96, y, oc.getWidth() - 98, 74);
    }

    const auto ic = contentOf (wId);
    add<ui::Dial> ({ ic.getX() + 2, ic.getY() + 4, 136, 176 }, "How much of drop 1 stays. 1 = drop 1 again. Above 25% the anchors hold: the one, the snares on 2 and 4, the first bass note of every bar.",
                   skin, param (ids::identity), "IDENTITY", this);
    meter.setBounds (ic.getX() + 146, ic.getY() + 2, ic.getWidth() - 150, ic.getHeight() - 4);

    const auto vc = contentOf (wVar);
    for (int v = 0; v < 4; ++v)
        slots[(size_t) v]->setBounds (vc.getX() + 2 + (v % 2) * 176, vc.getY() + 2 + (v / 2) * 74, 172, 70);
    dragAll.setBounds (vc.getX() + 2, vc.getY() + 152, 114, 32);
    exportBtn.setBounds (vc.getX() + 120, vc.getY() + 152, 114, 32);
    dragDrop1.setBounds (vc.getX() + 238, vc.getY() + 152, 112, 32);
    add<ui::MenuCell> ({ vc.getX() + 2, vc.getY() + 188, 114, 42 }, "MIDI channel of the drums", skin, param (ids::drumch), "DRUMS CH", this);
    add<ui::MenuCell> ({ vc.getX() + 120, vc.getY() + 188, 114, 42 }, "MIDI channel of the bass (call)", skin, param (ids::bassch), "CALL CH", this);
    add<ui::MenuCell> ({ vc.getX() + 238, vc.getY() + 188, 112, 42 }, "MIDI channel of the response", skin, param (ids::respch), "RESPONSE CH", this);

    // ---- the moves
    juce::Rectangle<int>* ws[] { &wHt, &wKick, &wLock, &wFlip, &wSplit };
    for (int i = 0; i < 5; ++i) *ws[i] = { 10 + i * 233, 644, 226, 122 };
    auto on = [&] (juce::Rectangle<int> w, const char* id, const juce::String& tip)
    {
        const auto c = contentOf (w);
        add<ui::Toggle> ({ c.getX() + 4, c.getY() + 4, 100, 28 }, tip, skin, param (id), "ON", "OFF", this);
    };
    {
        const auto c = contentOf (wHt);
        on (wHt, ids::halftime, "The later bars switch to half time: snare on 3, kicks and bass stretched. IDENTITY sets the bar where it switches.");
        add<ui::Toggle> ({ c.getX() + 4, c.getY() + 38, 100, 28 }, "Keep the hats at full speed under the half-time (the 170 stays in the hats)",
                         skin, param (ids::hthats), "HATS FULL", "HATS HALF", this);
        htInfo = std::make_unique<ui::InfoLine> (skin, [this]
        {
            const int bars = proc.source (0).bars();
            const float id = proc.apvts.getRawParameterValue (ids::identity)->load();
            const int k0 = juce::jlimit (0, bars, (int) std::floor (id * (float) bars + 0.5f));
            if (proc.apvts.getRawParameterValue (ids::halftime)->load() < 0.5f) return juce::String ("off");
            return k0 >= bars ? juce::String ("no bars switch (identity too high)") : "switches at bar " + juce::String (k0 + 1) + " of " + juce::String (bars);
        });
        htInfo->setBounds (c.getX() + 110, c.getY() + 4, c.getWidth() - 114, 62);
        content.addAndMakeVisible (*htInfo);
    }
    {
        const auto c = contentOf (wKick);
        on (wKick, ids::kickshift, "Kicks slide off their places by 1/16 steps (never onto a snare)");
        add<ui::MenuCell> ({ c.getX() + 4, c.getY() + 40, 100, 42 }, "Furthest a kick may slide", skin, param (ids::kickmax), "MAX SHIFT", this);
        add<ui::Dial> ({ c.getX() + 110, c.getY() + 2, c.getWidth() - 114, 90 }, "How many kicks move", skin, param (ids::kickamt), "AMOUNT", this);
    }
    {
        const auto c = contentOf (wLock);
        on (wLock, ids::basslock, "Bass notes snap onto the kicks (and snares) within half a beat: the bass rides the drums");
        add<ui::MenuCell> ({ c.getX() + 4, c.getY() + 40, 100, 42 }, "What the bass locks to", skin, param (ids::lockto), "LOCK TO", this);
        add<ui::Dial> ({ c.getX() + 110, c.getY() + 2, c.getWidth() - 114, 90 }, "How many bass notes lock", skin, param (ids::lockamt), "AMOUNT", this);
    }
    {
        const auto c = contentOf (wFlip);
        on (wFlip, ids::flip, "Whole bars of bass change register: up, down, every other bar, or turned upside down");
        add<ui::MenuCell> ({ c.getX() + 4, c.getY() + 40, 100, 42 }, "How the register flips", skin, param (ids::flipmode), "MODE", this);
        add<ui::Dial> ({ c.getX() + 110, c.getY() + 2, c.getWidth() - 114, 90 }, "How many bars flip", skin, param (ids::flipamt), "AMOUNT", this);
    }
    {
        const auto c = contentOf (wSplit);
        on (wSplit, ids::split, "The bass splits in two voices: every other unit answers the one before, on its own channel");
        add<ui::MenuCell> ({ c.getX() + 4, c.getY() + 40, 100, 42 }, "How long a call lasts before the answer", skin, param (ids::splitunit), "UNIT", this);
        add<ui::MenuCell> ({ c.getX() + 110, c.getY() + 40, c.getWidth() - 114, 42 }, "The response, transposed", skin, param (ids::interval), "ANSWER", this);
        add<ui::InfoLine> ({ c.getX() + 110, c.getY() + 4, c.getWidth() - 114, 32 }, "", skin, [] { return juce::String ("call: A   answer: B"); });
    }
}

void DropTwoEditor::updateLabels()
{
    presetName.text = proc.presetName();
    seedBtn.text = "NEW  #" + juce::String ((int) param (ids::seed).convertFrom0to1 (param (ids::seed).getValue())).paddedLeft ('0', 4);
    themeBtn.text = juce::String (ui::theme (skin.index).name).toUpperCase().substring (0, 6);
    for (auto* b : { &presetName, &seedBtn, &themeBtn }) b->repaint();
}

//==============================================================================
void DropTwoEditor::renderBackground()
{
    const auto& t = skin.t();
    const float s = bgScale;
    bg = juce::Image (juce::Image::RGB, juce::roundToInt (kW * s), juce::roundToInt (kH * s), true);
    juce::Graphics g (bg);
    g.addTransform (juce::AffineTransform::scale (s));
    g.fillAll (t.desk);
    g.setColour (t.panelLo.withAlpha (t.light ? 0.25f : 0.5f));
    for (int y = 104; y < kH; y += 3)
        for (int x = (y / 3 % 2) * 3; x < kW; x += 6) g.fillRect (x, y, 1, 1);

    ui::draw::window (g, wSrc, t, "DROP 1", "SOURCE");
    ui::draw::window (g, wOut, t, "DROP II", "OUT");
    ui::draw::window (g, wId, t, "IDENTITY");
    ui::draw::window (g, wVar, t, "VARIANTS");
    ui::draw::window (g, wHt, t, "HALF-TIME");
    ui::draw::window (g, wKick, t, "KICK SHIFT");
    ui::draw::window (g, wLock, t, "BASS LOCK");
    ui::draw::window (g, wFlip, t, "FLIP");
    ui::draw::window (g, wSplit, t, "CALL + RESPONSE");

    const auto sc = contentOf (wSrc);
    for (int i = 0; i < 2; ++i)
        ui::draw::smallLabel (g, srcNames[i], { sc.getX() + 4, sc.getY() + 2 + i * 92, 88, 12 }, t);
    const auto oc = contentOf (wOut);
    for (int i = 0; i < 3; ++i)
    {
        auto lbl = juce::Rectangle<int> (oc.getX() + 2, oc.getY() + 4 + i * 77, 88, 16);
        g.setColour (i == 0 ? t.text : (i == 1 ? t.accent : t.accent2));
        g.setFont (ui::labelFont (8.5f));
        g.drawText (outNames[i], lbl, juce::Justification::centredLeft);
    }
}

void DropTwoEditor::paintContent (juce::Graphics& g)
{
    const float s = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (bgTheme != skin.index || std::abs (s - bgScale) > 0.01f || ! bg.isValid())
    {
        bgScale = s;
        bgTheme = skin.index;
        renderBackground();
    }
    g.drawImage (bg, juce::Rectangle<float> (0.0f, 0.0f, (float) kW, (float) kH));
    const auto& t = skin.t();
    ui::draw::screenWell (g, statusBox, t);
    g.setColour (t.ink);
    g.setFont (ui::screenFont (17.0f));
    g.drawText ("> " + status, statusBox.reduced (6, 0), juce::Justification::centredLeft);
}

void DropTwoEditor::paintDropHint (juce::Graphics& g)
{
    if (dropLane < 0) return;
    const auto& t = skin.t();
    auto r = laneSrc[dropLane];
    g.setColour (t.accent.withAlpha (0.18f));
    g.fillRect (r);
    g.setColour (t.accent);
    g.drawRect (r, 2);
    g.setFont (ui::screenFont (22.0f));
    g.drawText (juce::String ("DROP: ") + srcNames[dropLane], r, juce::Justification::centred);
}

//==============================================================================
void DropTwoEditor::pump (float dt)
{
    header.advance (dt);
    const auto& res = proc.variants()[(size_t) proc.selectedVariant()];
    header.status[0] = "VARIANT " + ids::variants[proc.selectedVariant()] + "  " + juce::String (proc.loopLength() / 4.0, 0) + " BARS";
    header.status[1] = "IDENTITY " + juce::String (juce::roundToInt (res.identity * 100.0f)) + "%  ("
                       + juce::String (juce::roundToInt (proc.apvts.getRawParameterValue (ids::identity)->load() * 100.0f)) + ")";
    const int a = proc.armedLane();
    header.status[2] = a >= 0 ? (proc.recording() ? juce::String ("REC ") + srcNames[a] + " " + juce::String (juce::roundToInt (proc.recProgress() * 100.0f)) + "%"
                                                  : juce::String ("ARMED: ") + srcNames[a])
                              : (proc.loopPosition() >= 0.0f ? "PLAYING " + ids::plays[juce::roundToInt (proc.apvts.getRawParameterValue (ids::play)->load())]
                                                             : juce::String ("STOPPED"));
    header.repaint();

    const int gen = proc.generation.load();
    const bool changed = gen != lastGen;
    if (changed && lastGen >= 0) header.burst (0.3f + 0.3f * (float) proc.selectedVariant() / 3.0f, 0.6f);
    lastGen = gen;
    const bool moving = proc.loopPosition() >= 0.0f || proc.armedLane() >= 0;
    if (changed || moving)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &srcDrums, &srcBass, &outDrums, &outCall, &outResp }) c->repaint();
    }
    if (changed)
    {
        meter.repaint();
        for (auto& sl : slots) sl->repaint();
        if (htInfo) htInfo->repaint();
    }
    for (int i = 0; i < 2; ++i)
    {
        const bool lit = proc.armedLane() == i;
        const juce::String txt = lit ? (proc.recording() ? "REC  ..." : "ARMED") : "REC";
        if (recBtn[i]->text != txt) { recBtn[i]->text = txt; recBtn[i]->repaint(); }
    }
    if ((++frame % 15) == 0 && proc.presetName() != presetName.text) updateLabels();
}

//==============================================================================
int DropTwoEditor::laneAt (juce::Point<int> p) const
{
    for (int i = 0; i < 2; ++i) if (laneSrc[i].contains (p)) return i;
    return -1;
}

bool DropTwoEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi")) return true;
    return false;
}

void DropTwoEditor::fileDragMove (const juce::StringArray&, int x, int y)
{
    const float s = (float) getWidth() / (float) kW;
    const int lane = laneAt ({ juce::roundToInt ((float) x / s), juce::roundToInt ((float) y / s) });
    if (lane != dropLane) { dropLane = lane; content.repaint(); }
}

void DropTwoEditor::fileDragExit (const juce::StringArray&) { dropLane = -1; content.repaint(); }

void DropTwoEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const float s = (float) getWidth() / (float) kW;
    int lane = laneAt ({ juce::roundToInt ((float) x / s), juce::roundToInt ((float) y / s) });
    dropLane = -1;
    juce::StringArray loaded;
    for (auto& path : files)
    {
        if (! (path.endsWithIgnoreCase (".mid") || path.endsWithIgnoreCase (".midi"))) continue;
        const juce::File f (path);
        int target = lane;
        if (target < 0 || files.size() > 1)   // two files at once: the names decide
            target = f.getFileName().containsIgnoreCase ("bass") || f.getFileName().containsIgnoreCase ("reese") || f.getFileName().containsIgnoreCase ("sub") ? 1 : 0;
        juce::String err;
        if (proc.loadMidiFile (target, f, err)) loaded.add (juce::String (srcNames[target]) + " <- " + f.getFileName());
        else loaded.add (err);
    }
    hint (loaded.joinIntoString ("   "));
    content.repaint();
}

void DropTwoEditor::dragOut (const juce::String& what)
{
    const auto f = proc.writeMidi (what);
    if (! f.existsAsFile()) { hint ("could not write the MIDI file"); return; }
    hint ("dragging " + f.getFileName());
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void DropTwoEditor::exportAll()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DROP II");
    dir.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    juce::File last;
    for (auto* what : { "ALL", "DRUMS", "BASS", "CALL", "RESPONSE" })
    {
        const auto f = proc.writeMidi (what);
        auto dest = dir.getChildFile ("DROP II " + stamp + " " + ids::variants[proc.selectedVariant()] + " " + what + ".mid");
        f.copyFileTo (dest);
        last = dest;
    }
    hint ("exported to " + dir.getFullPathName());
#if ! DR2_TOOL
    last.revealToUser();
#endif
}

//==============================================================================
void DropTwoEditor::showThemeMenu()
{
    juce::PopupMenu m;
    for (int i = 0; i < ui::numThemes(); ++i) m.addItem (i + 1, juce::String (ui::theme (i).name).toUpperCase(), true, i == skin.index);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&themeBtn),
                     [safe = juce::Component::SafePointer<DropTwoEditor> (this)] (int r) { if (safe != nullptr && r > 0) safe->applyTheme (r - 1); });
}

void DropTwoEditor::showViewMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("SIZE");
    const int cur = juce::roundToInt (100.0f * (float) getWidth() / (float) kW);
    for (int pct : { 75, 90, 100, 125, 150 }) m.addItem (pct, juce::String (pct) + "%", true, std::abs (cur - pct) < 3);
    m.addSeparator();
    m.addItem (1, "ANIMATION", true, skin.motion);
    m.addItem (2, "GPU DRAWING", true, gl.isAttached());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&viewBtn),
                     [safe = juce::Component::SafePointer<DropTwoEditor> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0) return;
                         if (r == 1) { safe->skin.motion = ! safe->skin.motion; DropTwoAudioProcessor::setMotionSetting (safe->skin.motion); }
                         else if (r == 2) { const bool o = ! safe->gl.isAttached(); safe->setGpu (o); DropTwoAudioProcessor::setGpuSetting (o); }
                         else { safe->applySize (r); DropTwoAudioProcessor::setSizeSetting (r); }
                     });
}

void DropTwoEditor::showPresetMenu()
{
    juce::PopupMenu m;
    const auto names = DropTwoAudioProcessor::factoryNames();
    for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i], true, i == proc.factoryIndex());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetName),
                     [safe = juce::Component::SafePointer<DropTwoEditor> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0) return;
                         safe->proc.loadFactory (r - 1);
                         safe->updateLabels();
                     });
}
