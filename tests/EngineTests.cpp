// DROP II engine tests: no JUCE. A 4-bar DnB drop (two-step kicks, snares on 2
// and 4, 8th hats, a reese-style bassline) goes through every move.
#include "engine/Transform.h"
#include <cstdio>

using namespace dr2;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static Clip drop1Drums (int kick = 36, int snare = 38, int hat = 42)
{
    Clip c;
    c.length = 16.0;
    for (int bar = 0; bar < 4; ++bar)
    {
        const double o = bar * 4.0;
        c.notes.push_back ({ o + 0.0, 0.25, kick, 1.0f });
        c.notes.push_back ({ o + 2.5, 0.25, kick, 0.9f });            // the two-step kick
        c.notes.push_back ({ o + 1.0, 0.25, snare, 1.0f });
        c.notes.push_back ({ o + 3.0, 0.25, snare, 1.0f });
        c.notes.push_back ({ o + 3.75, 0.25, snare, 0.35f });         // ghost
        for (int k = 0; k < 8; ++k) c.notes.push_back ({ o + k * 0.5, 0.125, hat, 0.6f });
    }
    c.sort();
    return c;
}

static Clip drop1Bass()
{
    Clip c;
    c.length = 16.0;
    const int line[] { 31, 31, 34, 29 };   // G1 G1 Bb1 F1
    for (int bar = 0; bar < 4; ++bar)
    {
        const double o = bar * 4.0;
        c.notes.push_back ({ o + 0.0, 1.5, line[bar], 0.9f });
        c.notes.push_back ({ o + 1.75, 0.5, line[bar] + 12, 0.8f });
        c.notes.push_back ({ o + 2.4, 1.0, line[bar], 0.85f });     // lands near the kick on 2.5
    }
    return c;
}

static Settings none()
{
    Settings s;
    s.halftime = s.kickShift = s.bassLock = s.flip = s.split = false;
    return s;
}

static bool sameClip (const Clip& a, const Clip& b)
{
    if (a.notes.size() != b.notes.size()) return false;
    for (size_t i = 0; i < a.notes.size(); ++i)
        if (std::abs (a.notes[i].start - b.notes[i].start) > 1e-9 || a.notes[i].pitch != b.notes[i].pitch) return false;
    return true;
}

int main()
{
    const auto D = drop1Drums();
    const auto B = drop1Bass();

    // ---- roles -------------------------------------------------------------------------
    {
        auto r = detectRoles (D);
        CHECK (r.kick == 36 && r.snare == 38 && r.of (42) == Role::Hat, "GM roles");
        auto pads = drop1Drums (40, 44, 47);   // a slicer's pads: no GM
        r = detectRoles (pads);
        CHECK (r.kick == 40, "pad kick %d", r.kick);
        CHECK (r.snare == 44, "pad snare %d", r.snare);
        CHECK (r.of (47) == Role::Hat, "pad hats");
    }

    // ---- identity 1 is drop 1 -------------------------------------------------------------
    {
        Settings s;
        s.halftime = s.flip = s.split = false;   // the moves that change things whatever the dice say
        s.identity = 1.0f;
        auto r = generate (D, B, s, 1);
        CHECK (sameClip (r.drums, D) && sameClip (r.bassA, B), "identity 1 changed something");
        CHECK (std::abs (r.identity - 1.0f) < 1e-6f, "measured identity %f", r.identity);
    }

    // ---- kick shift ------------------------------------------------------------------------
    {
        auto s = none();
        s.kickShift = true; s.kickAmount = 1.0f; s.identity = 0.0f; s.kickMax = 2;
        int moved = 0;
        for (std::uint32_t seed = 1; seed < 40; ++seed)
        {
            auto r = generate (D, B, s, seed);
            for (auto& n : r.drums.notes)
            {
                if (n.pitch != 36) continue;
                if (n.tag == Changed) ++moved;
                for (auto& o : r.drums.notes)
                    if (o.pitch == 38 && std::abs (o.start - n.start) < 0.12) CHECK (false, "kick on a snare (seed %u)", seed);
                CHECK (n.start >= 0.0 && n.start < 16.0, "kick out of the clip");
            }
            int kicks = 0;
            for (auto& n : r.drums.notes) kicks += n.pitch == 36;
            CHECK (kicks == 8, "kick count %d", kicks);
        }
        CHECK (moved > 100, "kicks hardly moved (%d)", moved);

        s.identity = 0.5f;   // anchors protected: the one stays
        for (std::uint32_t seed = 1; seed < 20; ++seed)
        {
            auto r = generate (D, B, s, seed);
            for (int bar = 0; bar < 4; ++bar)
            {
                bool one = false;
                for (auto& n : r.drums.notes) one = one || (n.pitch == 36 && near (n.start, bar * 4.0, 1e-9));
                CHECK (one, "the one moved (bar %d seed %u)", bar, seed);
            }
        }
    }

    // ---- half-time ------------------------------------------------------------------------
    {
        auto s = none();
        s.halftime = true; s.identity = 0.5f;          // bars 3 and 4 switch
        auto r = generate (D, B, s, 3);
        int early = 0, late = 0;
        for (auto& n : r.drums.notes)
        {
            if (n.pitch != 38 || n.velocity < 0.5f) continue;
            if (n.start < 8.0) { ++early; CHECK (near (wrapBeat (n.start), 1.0) || near (wrapBeat (n.start), 3.0), "early snare moved %f", n.start); }
            else { ++late; CHECK (near (wrapBeat (n.start), 2.0), "half-time snare not on 3: %f", n.start); }
        }
        CHECK (early == 4 && late == 2, "snares early %d late %d", early, late);
        int hats = 0;
        for (auto& n : r.drums.notes) hats += n.pitch == 42;
        CHECK (hats == 32, "hats kept at full speed (%d)", hats);
        for (auto& n : r.bassA.notes) if (n.start >= 8.0) CHECK (n.tag == Changed, "late bass not stretched");
    }

    // ---- bass lock --------------------------------------------------------------------------
    {
        auto s = none();
        s.bassLock = true; s.lockAmount = 1.0f; s.identity = 0.0f;
        auto r = generate (D, B, s, 5);
        int locked = 0;
        for (auto& n : r.bassA.notes)
        {
            bool onKick = false;
            for (auto& k : r.drums.notes) onKick = onKick || (k.pitch == 36 && near (k.start, n.start, 1e-9));
            locked += onKick;
        }
        CHECK (locked >= 8, "bass notes on kicks: %d", locked);
        for (size_t i = 0; i + 1 < r.bassA.notes.size(); ++i)
            CHECK (r.bassA.notes[i].start + r.bassA.notes[i].length <= r.bassA.notes[i + 1].start + 1e-9, "bass overlaps at %zu", i);
    }

    // ---- flip ---------------------------------------------------------------------------------
    {
        auto s = none();
        s.flip = true; s.flipMode = 2; s.flipAmount = 1.0f; s.identity = 0.0f;
        auto r = generate (D, B, s, 9);
        for (size_t i = 0; i < r.bassA.notes.size(); ++i)
        {
            const int bar = (int) (r.bassA.notes[i].start / 4.0);
            const int want = B.notes[i].pitch + (bar % 2 == 1 ? 12 : 0);
            CHECK (r.bassA.notes[i].pitch == want, "alternate flip bar %d: %d vs %d", bar, r.bassA.notes[i].pitch, want);
        }
        s.flipMode = 3;   // invert stays in the register
        r = generate (D, B, s, 9);
        for (auto& n : r.bassA.notes) CHECK (n.pitch >= 29 - 5 && n.pitch <= 29 + 24, "inverted note out of range %d", n.pitch);
    }

    // ---- call / response ------------------------------------------------------------------
    {
        auto s = none();
        s.split = true; s.splitUnit = 1; s.respInterval = 7; s.identity = 0.5f;
        auto r = generate (D, B, s, 2);
        CHECK (r.bassA.notes.size() == 6 && r.bassB.notes.size() == 6, "split sizes %zu %zu", r.bassA.notes.size(), r.bassB.notes.size());
        for (auto& n : r.bassA.notes) CHECK (((int) (n.start / 4.0)) % 2 == 0, "call in an odd bar");
        for (auto& n : r.bassB.notes) CHECK (((int) (n.start / 4.0)) % 2 == 1, "response in an even bar");
        CHECK (r.bassB.notes[0].pitch == B.notes[3].pitch + 7, "response transposed");
    }

    // ---- the identity knob is what the measurement says ---------------------------------------------------
    {
        Settings s;
        s.kickShift = s.bassLock = s.flip = true;
        s.flipMode = 0;
        float lo = 0, hi = 0;
        for (std::uint32_t seed = 1; seed <= 30; ++seed)
        {
            s.identity = 0.15f; lo += generate (D, B, s, seed).identity;
            s.identity = 0.85f; hi += generate (D, B, s, seed).identity;
        }
        lo /= 30.0f; hi /= 30.0f;
        CHECK (hi > lo + 0.15f, "identity knob barely matters: lo %f hi %f", lo, hi);
        CHECK (hi > 0.8f, "identity 0.85 measured %f", hi);
        std::printf ("measured identity: knob 0.15 -> %.2f, knob 0.85 -> %.2f\n", lo, hi);
    }

    // ---- determinism ------------------------------------------------------------------------
    {
        Settings s;
        s.halftime = true; s.split = true; s.flip = true; s.identity = 0.3f;
        auto a = generate (D, B, s, 77), b = generate (D, B, s, 77);
        CHECK (sameClip (a.drums, b.drums) && sameClip (a.bassA, b.bassA) && sameClip (a.bassB, b.bassB), "not deterministic");
    }

    std::printf ("%d / %d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
