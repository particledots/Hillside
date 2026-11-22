// Hillside's own look (as in the approved draft, 2026-11-22): dark slate-green panel, sage curve and switches, amber
// lights. Original; nothing taken from any module maker's artwork.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace HsStyle
{
    inline const juce::Colour panel     { 0xff1b1f1c };
    inline const juce::Colour display   { 0xff121513 };
    inline const juce::Colour line      { 0xff2c352f };
    inline const juce::Colour line2     { 0xff3a463d };
    inline const juce::Colour knob      { 0xff2a2f2b };
    inline const juce::Colour text      { 0xffc9d2c6 };
    inline const juce::Colour bright    { 0xffe8ece4 };
    inline const juce::Colour dim       { 0xff8a968c };
    inline const juce::Colour faint     { 0xff5f6b62 };
    inline const juce::Colour sage      { 0xffa9cf9f };
    inline const juce::Colour sageMid   { 0xff7f9a79 };
    inline const juce::Colour sageDark  { 0xff3b5a3e };
    inline const juce::Colour amber     { 0xfff2b24c };
    inline const juce::Colour amberText { 0xfff2d9a8 };
    inline const juce::Colour amberDeep { 0xff3d3220 };

    inline constexpr float kDimAlpha = 0.38f;   // controls that arrive in a later milestone

    inline juce::Font font (float size) { return juce::Font (juce::FontOptions (size)); }
}
