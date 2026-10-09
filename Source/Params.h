// Parameter ids and choice lists.
#pragma once

#include <juce_core/juce_core.h>

namespace ids
{
    inline constexpr const char* identity = "identity", *seed = "seed", *variant = "variant",
                                 *halftime = "halftime", *hthats = "hthats",
                                 *kickshift = "kickshift", *kickamt = "kickamt", *kickmax = "kickmax",
                                 *basslock = "basslock", *lockamt = "lockamt", *lockto = "lockto",
                                 *flip = "flip", *flipmode = "flipmode", *flipamt = "flipamt",
                                 *split = "split", *splitunit = "splitunit", *interval = "interval",
                                 *play = "play", *preview = "preview", *level = "level",
                                 *drumch = "drumch", *bassch = "bassch", *respch = "respch",
                                 *recbars = "recbars", *run = "run", *bpm = "bpm";

    inline const juce::StringArray variants   { "A", "B", "C", "D" };
    inline const juce::StringArray kickMaxes  { "1/16", "2/16", "3/16" };
    inline const juce::StringArray lockTos    { "KICKS", "KICK+SNARE" };
    inline const juce::StringArray flipModes  { "UP 8VA", "DOWN 8VA", "ALTERNATE", "INVERT" };
    inline const juce::StringArray units      { "1/2 BAR", "1 BAR", "2 BARS" };
    inline const juce::StringArray intervals  { "UNISON", "+3", "+5", "+7", "+12", "-5", "-12" };
    inline const juce::StringArray plays      { "OFF", "DROP 1", "DROP II" };
    inline const juce::StringArray recBars    { "1 BAR", "2 BARS", "4 BARS", "8 BARS" };
    inline constexpr int recBarValues[]       { 1, 2, 4, 8 };
}
