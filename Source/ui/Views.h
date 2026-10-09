// The views that only DROP II has: clip strips, variant slots, the identity meter.
#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

namespace ui
{

//==============================================================================
// Horizontal bar bound to a float parameter: drag sideways, double-click resets.
class HBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    HBar (Skin& s, juce::RangedAudioParameter& p, juce::String lbl, Host* h)
        : skin (s), param (p), label (std::move (lbl)), host (h), attach (p, [this] (float) { repaint(); }, nullptr)
    {
        attach.sendInitialUpdate();
        setRepaintsOnMouseActivity (true);
    }
    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        auto r = getLocalBounds();
        draw::smallLabel (g, label, r.removeFromTop (12), t);
        draw::screenWell (g, r.reduced (2), t);
        auto in = r.reduced (4);
        const float v = param.getValue();
        const int cells = juce::jmax (4, in.getWidth() / 6);
        for (int i = 0; i < cells; ++i)
        {
            const float f = (float) i / (float) (cells - 1);
            auto cell = juce::Rectangle<int> (in.getX() + i * in.getWidth() / cells, in.getY(), in.getWidth() / cells - 1, in.getHeight());
            g.setColour (f <= v && v > 0.0f ? t.thermal (0.25f + 0.75f * f) : t.inkDim.withAlpha (0.35f));
            g.fillRect (cell);
        }
        const auto txt = param.getCurrentValueAsText();
        g.setFont (screenFont (17.0f));
        const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), txt) + 10;
        auto plate = in.withSizeKeepingCentre (juce::jmin (in.getWidth(), tw), in.getHeight() - 2);
        g.setColour (t.screen.withAlpha (0.85f));
        g.fillRect (plate);
        g.setColour (isMouseOverOrDragging() ? t.accent2 : t.ink);
        g.drawText (txt, plate, juce::Justification::centred);
    }
    void mouseEnter (const juce::MouseEvent&) override { if (host != nullptr && getTooltip().isNotEmpty()) host->hint (getTooltip()); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { draw::contextMenu (host, param.getParameterID(), this); return; }
        attach.beginGesture();
        start = param.getValue();
        dragging = true;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float sens = e.mods.isShiftDown() ? 900.0f : (float) juce::jmax (60, getWidth());
        attach.setValueAsPartOfGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, start + (float) e.getDistanceFromDragStartX() / sens)));
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) attach.endGesture(); dragging = false; }
    void mouseDoubleClick (const juce::MouseEvent&) override { attach.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        attach.setValueAsCompleteGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, param.getValue() + w.deltaY * 0.05f)));
    }

private:
    Skin& skin;
    juce::RangedAudioParameter& param;
    juce::String label;
    Host* host;
    juce::ParameterAttachment attach;
    float start = 0.0f;
    bool dragging = false;
};

//==============================================================================
// A clip as a strip: drums on four rows (kick, snare, hats, perc), bass by pitch.
// The DROP II lanes colour what changed and outline where drop 1 had it.
class ClipView : public juce::Component
{
public:
    enum Which { SrcDrums, SrcBass, OutDrums, OutCall, OutResp };
    ClipView (Skin& s, DropTwoAudioProcessor& p, Which w) : skin (s), proc (p), which (w) { setOpaque (true); }

    std::function<void()> onRepaintNeeded;

    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        auto r = getLocalBounds();
        g.setColour (t.panel);
        g.fillRect (r);
        r.reduce (2, 2);
        draw::screenWell (g, r, t);
        auto in = r.reduced (4, 3);

        const auto& res = proc.variants()[(size_t) proc.selectedVariant()];
        const dr2::Clip* clip = nullptr;
        const dr2::Clip* ref = nullptr;
        const dr2::Clip* other = nullptr;    // call and response share one pitch scale
        switch (which)
        {
            case SrcDrums: clip = &proc.source (0); break;
            case SrcBass:  clip = &proc.source (1); break;
            case OutDrums: clip = &res.drums; ref = &proc.source (0); break;
            case OutCall:  clip = &res.bassA; ref = &proc.source (1); other = &res.bassB; break;
            case OutResp:  clip = &res.bassB; other = &res.bassA; break;
        }
        const bool drums = which == SrcDrums || which == OutDrums;
        const double L = proc.loopLength();
        auto xOf = [&] (double beat) { return (float) in.getX() + (float) (beat / L) * (float) in.getWidth(); };

        // grid
        for (int b = 0; b <= (int) L; ++b)
        {
            const bool bar = b % 4 == 0;
            g.setColour (t.inkDim.withAlpha (bar ? 0.9f : 0.35f));
            g.fillRect (xOf (b), (float) in.getY(), bar ? 1.5f : 1.0f, (float) in.getHeight());
        }
        g.setFont (screenFont (13.0f));
        g.setColour (t.inkDim.brighter (0.5f));
        for (int bar = 0; bar < (int) (L / 4.0); ++bar)
            g.drawText (juce::String (bar + 1), juce::Rectangle<float> (xOf (bar * 4.0) + 3.0f, (float) in.getY(), 20.0f, 11.0f), juce::Justification::topLeft);

        // rows
        dr2::DrumRoles roles = dr2::detectRoles (proc.source (0));
        int lo = 127, hi = 0;
        auto span = [&] (const dr2::Clip* c) { if (c) for (auto& n : c->notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); } };
        if (! drums) { span (clip); span (ref); span (other); if (lo > hi) { lo = 24; hi = 48; } lo -= 1; hi += 1; }
        const float rowsTop = (float) in.getY() + 11.0f, rowsH = (float) in.getHeight() - 12.0f;
        auto rowRect = [&] (const dr2::Note& n) -> juce::Rectangle<float>
        {
            if (drums)
            {
                const int row = (int) roles.of (n.pitch);       // 0 kick (bottom) .. 3 perc (top)
                const float h = rowsH / 4.0f;
                return { 0.0f, rowsTop + rowsH - h * (float) (row + 1) + 1.0f, 0.0f, h - 2.0f };
            }
            const float h = juce::jmax (2.0f, rowsH / (float) (hi - lo + 1));
            return { 0.0f, rowsTop + rowsH - h * (float) (n.pitch - lo + 1), 0.0f, juce::jmax (2.0f, h - 1.0f) };
        };
        if (drums)
        {
            static const char* names[] { "K", "S", "H", "P" };
            g.setFont (screenFont (13.0f));
            for (int row = 0; row < 4; ++row)
            {
                const float h = rowsH / 4.0f, y = rowsTop + rowsH - h * (float) (row + 1);
                g.setColour (t.ink.withAlpha (row % 2 ? 0.04f : 0.08f));
                g.fillRect ((float) in.getX(), y, (float) in.getWidth(), h);
                g.setColour (t.inkDim.brighter (0.6f));
                g.drawText (names[row], juce::Rectangle<float> ((float) in.getRight() - 12.0f, y, 10.0f, h), juce::Justification::centredRight);
            }
        }

        auto noteRect = [&] (const dr2::Note& n)
        {
            auto rr = rowRect (n);
            const double len = drums ? 0.2 : n.length;
            return rr.withX (xOf (n.start) + 1.0f).withWidth (juce::jmax (3.0f, xOf (n.start + len) - xOf (n.start) - 1.0f));
        };

        // where drop 1 had notes that moved
        if (ref != nullptr)
            for (const auto& n : ref->notes)
            {
                bool still = false;
                for (const auto& m : clip->notes) still = still || (std::abs (m.start - n.start) < 1e-6 && m.pitch == n.pitch);
                if (other) for (const auto& m : other->notes) still = still || (std::abs (m.start - n.start) < 1e-6);
                if (still) continue;
                g.setColour (t.inkDim.brighter (0.3f));
                g.drawRect (noteRect (n), 1.0f);
            }

        if (clip != nullptr)
            for (const auto& n : clip->notes)
            {
                const auto nr = noteRect (n);
                // drop 1 in the screen's own ink; in drop II what stayed is neutral, what moved is lit
                const bool out = which != SrcDrums && which != SrcBass;
                const juce::Colour same = out ? (t.light ? t.ink : t.text) : t.ink;
                // on a light theme the accent is dark: it would vanish on the screen
                const juce::Colour lit = t.light ? t.accent2 : t.accent, lit2 = t.light ? t.thermal (0.62f) : t.accent2;
                juce::Colour col = n.tag == dr2::Changed ? lit : (n.tag == dr2::Added ? lit2 : same);
                if (which == OutResp) col = n.tag == dr2::Changed ? lit2 : same;
                g.setColour (col.withAlpha (0.35f + 0.65f * juce::jlimit (0.0f, 1.0f, n.velocity)));
                g.fillRect (nr);
                if (n.tag != dr2::Same) { g.setColour (col); g.drawRect (nr, 1.0f); }
            }

        if (clip == nullptr || clip->notes.empty())
        {
            g.setColour (t.ink.withAlpha (0.8f));
            g.setFont (screenFont (17.0f));
            const auto msg = which == SrcDrums || which == SrcBass ? "REC, or drop a .mid here"
                           : (which == OutResp ? "switch CALL / RESP on to split the bass" : "nothing here");
            g.drawText (msg, in, juce::Justification::centred);
        }

        // recording
        const int lane = which == SrcDrums ? 0 : (which == SrcBass ? 1 : -1);
        if (lane >= 0 && proc.armedLane() == lane)
        {
            g.setColour (t.accent.withAlpha (0.18f));
            g.fillRect (in);
            g.setColour (t.accent);
            g.setFont (screenFont (18.0f));
            g.drawText (proc.recording() ? "RECORDING " + juce::String (juce::roundToInt (proc.recProgress() * 100.0f)) + "%"
                                         : "ARMED: recording starts on the next bar", in, juce::Justification::centred);
            if (proc.recording()) g.fillRect ((float) in.getX(), (float) in.getBottom() - 3.0f, (float) in.getWidth() * proc.recProgress(), 3.0f);
        }

        const float pos = proc.loopPosition();
        if (pos >= 0.0f)
        {
            g.setColour (t.accent2);
            g.fillRect (xOf (pos) - 1.0f, (float) in.getY(), 2.0f, (float) in.getHeight());
        }
        draw::scanlines (g, r, t, 0.1f);
    }

private:
    Skin& skin;
    DropTwoAudioProcessor& proc;
    Which which;
};

//==============================================================================
// One of the four variants: its letter, what is left of drop 1, a thumbnail.
class VariantButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    VariantButton (Skin& s, DropTwoAudioProcessor& p, int index) : skin (s), proc (p), v (index) { setRepaintsOnMouseActivity (true); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        const bool sel = proc.selectedVariant() == v;
        auto r = getLocalBounds();
        g.setColour (t.panelLo.darker (0.5f));
        g.drawRect (r);
        auto in = r.reduced (1);
        g.setColour (sel ? t.panel.darker (0.15f) : (isMouseOver() ? t.panel.brighter (0.07f) : t.panel));
        g.fillRect (in);
        draw::bevel (g, in, t, ! sel, 2);
        if (sel) { g.setColour (t.accent); g.fillRect (in.reduced (3, 0).removeFromBottom (3)); }

        auto letter = in.removeFromLeft (46).reduced (4);
        draw::screenWell (g, letter, t);
        g.setColour (sel ? t.accent : t.ink);
        g.setFont (screenFont (40.0f));
        g.drawText (ids::variants[v], letter, juce::Justification::centred);

        const auto& res = proc.variants()[(size_t) v];
        auto info = in.reduced (6, 4);
        auto top = info.removeFromTop (18);
        g.setFont (screenFont (17.0f));
        g.setColour (t.text);
        g.drawText ("IDENTITY " + juce::String (juce::roundToInt (res.identity * 100.0f)) + "%", top, juce::Justification::centredLeft);

        // thumbnail: drum onsets as ticks, changed ones lit
        auto thumb = info.reduced (0, 4);
        draw::screenWell (g, thumb, t);
        const double L = proc.loopLength();
        const auto roles = dr2::detectRoles (proc.source (0));
        for (const auto& n : res.drums.notes)
        {
            const auto role = roles.of (n.pitch);
            if (role == dr2::Role::Hat) continue;
            const float x = (float) thumb.getX() + (float) (n.start / L) * (float) thumb.getWidth();
            const float h = (float) thumb.getHeight() * (role == dr2::Role::Kick ? 0.45f : 0.8f);
            g.setColour (n.tag == dr2::Same ? (t.light ? t.ink : t.text).withAlpha (0.55f) : (t.light ? t.accent2 : t.accent));
            g.fillRect (x, (float) thumb.getBottom() - h, 2.0f, h);
        }
        for (const auto& n : res.bassB.notes)
        {
            const float x = (float) thumb.getX() + (float) (n.start / L) * (float) thumb.getWidth();
            g.setColour (t.accent2);
            g.fillRect (x, (float) thumb.getY() + 1.0f, 3.0f, 3.0f);
        }
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        auto* p = proc.apvts.getParameter (ids::variant);
        p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
    }

private:
    Skin& skin;
    DropTwoAudioProcessor& proc;
    int v;
};

//==============================================================================
// What is left of drop 1: measured, per part, against the IDENTITY knob.
class IdentityMeter : public juce::Component
{
public:
    IdentityMeter (Skin& s, DropTwoAudioProcessor& p) : skin (s), proc (p) {}
    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        const auto& res = proc.variants()[(size_t) proc.selectedVariant()];
        const float knob = proc.apvts.getRawParameterValue (ids::identity)->load();
        auto r = getLocalBounds();
        const struct { const char* name; float v; } rows[] { { "DRUMS", res.idDrums }, { "BASS", res.idBass }, { "ALL", res.identity } };
        const int rh = (r.getHeight() - 30) / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto row = r.removeFromTop (rh);
            draw::smallLabel (g, rows[i].name, row.removeFromTop (12), t);
            auto well = row.reduced (2, 2);
            draw::screenWell (g, well, t);
            auto bar = well.reduced (3);
            const int cells = bar.getWidth() / 6;
            for (int c = 0; c < cells; ++c)
            {
                const float f = (float) (c + 1) / (float) cells;
                g.setColour (f <= rows[i].v ? t.thermal (0.25f + 0.75f * f) : t.inkDim.withAlpha (0.3f));
                g.fillRect (bar.getX() + c * 6, bar.getY(), 5, bar.getHeight());
            }
            const float kx = (float) bar.getX() + knob * (float) bar.getWidth();
            g.setColour (t.accent2);
            g.fillRect (kx - 1.0f, (float) well.getY(), 2.0f, (float) well.getHeight());
            g.setFont (screenFont (16.0f));
            const auto txt = juce::String (juce::roundToInt (rows[i].v * 100.0f)) + "%";
            auto plate = bar.removeFromRight (40);
            g.setColour (t.screen.withAlpha (0.85f));
            g.fillRect (plate);
            g.setColour (t.ink);
            g.drawText (txt, plate, juce::Justification::centred);
        }
        g.setFont (screenFont (16.0f));
        g.setColour (t.text);
        g.drawFittedText (knob >= 0.25f ? "ANCHORS HELD: the one, 2 + 4, first bass note of each bar"
                                        : "ANCHORS FREE: everything may move",
                          r.reduced (2, 0), juce::Justification::centredLeft, 2);
    }

private:
    Skin& skin;
    DropTwoAudioProcessor& proc;
};

//==============================================================================
class InfoLine : public juce::Component, public juce::SettableTooltipClient
{
public:
    InfoLine (Skin& s, std::function<juce::String()> f) : skin (s), text (std::move (f)) {}
    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        draw::screenWell (g, getLocalBounds().reduced (2), t);
        g.setColour (t.ink);
        g.setFont (screenFont (16.0f));
        g.drawFittedText (text(), getLocalBounds().reduced (7, 2), juce::Justification::centredLeft, 2);
    }
private:
    Skin& skin;
    std::function<juce::String()> text;
};

} // namespace ui
