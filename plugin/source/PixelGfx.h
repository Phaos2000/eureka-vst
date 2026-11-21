#pragma once

// Hand-pixelled faceplate drawing: a 16-colour indexed canvas, ordered-dither
// ramps, the 5x7 display font, the 3x5 panel print, the logo and the panel
// parts (bands, wells, buttons, knobs, the character display). Everything is
// drawn at 1x into palette indices, so nothing outside the palette can
// appear, and blown up with nearest-neighbour at whole-number zoom.
// Port of mockups/combined.html (black faceplate).

#include <juce_graphics/juce_graphics.h>

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace ambika_plugin::pixel
{
    enum Ink : uint8_t
    {
        BLK, D1, D2, D3, MID, LT, WH,  // grey ramp
        VOFF, VHALO, VON,              // display
        LED_OFF, LED_ON,               // LED
        WOOD_DK, WOOD_LT,              // wood (metal face only, unused)
        GOLD, NAVY,
        kNumInks
    };
    juce::uint32 argb (int ink);  // 0xffrrggbb

    constexpr char kDelimiter = 0x07;  // dotted column separator on the display
    constexpr char kArrow = '~';       // right arrow on the display

    class Canvas
    {
    public:
        Canvas (int width, int height);
        int width() const { return w; }
        int height() const { return h; }
        int at (int x, int y) const { return px[(size_t) (y * w + x)]; }
        const std::vector<uint8_t>& pixels() const { return px; }
        void copyFrom (const Canvas& other) { px = other.px; }

        // Drawing is limited to the clip rectangle.
        void clip (int x, int y, int cw, int ch) { cx0 = std::max (0, x); cy0 = std::max (0, y); cx1 = std::min (w, x + cw); cy1 = std::min (h, y + ch); }
        void unclip() { cx0 = 0; cy0 = 0; cx1 = w; cy1 = h; }

        void put (int x, int y, int ink)
        {
            if (x >= cx0 && y >= cy0 && x < cx1 && y < cy1)
                px[(size_t) (y * w + x)] = (uint8_t) ink;
        }
        void fill (int x, int y, int width, int height, int ink);

        // Ordered (Bayer 4x4) dither between neighbouring ramp entries; f 0..1.
        static int ramp (std::initializer_list<int> inks, double f, int x, int y);
        void line (int x0, int y0, int x1, int y1, int ink);
        // Calls inkAt (dx, dy, dx*dx + dy*dy) for each pixel of the disc; a
        // negative result leaves the pixel alone.
        template <typename Fn>
        void circle (int cx, int cy, int r, Fn&& inkAt)
        {
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                {
                    const int d = dx * dx + dy * dy;
                    if (d <= r * r + r * 0.3)
                    {
                        const int ink = inkAt (dx, dy, d);
                        if (ink >= 0)
                            put (cx + dx, cy + dy, ink);
                    }
                }
        }

        // 5x7 dot font, 6-pixel advance.
        void text5 (const juce::String& s, int x, int y, int ink);
        static int width5 (const juce::String& s) { return std::max (0, s.length() * 6 - 1); }
        // 3x5 panel print, 4-pixel advance. Lower case prints as upper case.
        void text3 (const juce::String& s, int x, int y, int ink);
        void text3c (const juce::String& s, int centreX, int y, int ink) { text3 (s, centreX - (width3 (s) >> 1), y, ink); }
        static int width3 (const juce::String& s) { return std::max (0, s.length() * 4 - 1); }

        // Character display: two lines of 5x7 cells (pitch 6, line pitch 9),
        // unlit dots shown, a halo on unlit dots next to lit ones in the cell.
        void vfd (int x, int y, const juce::String& line0, const juce::String& line1, int columns = 40);
        void vfd (int x, int y, const juce::StringArray& lines, int columns);

        // The faceplate.
        void plate();
        // Letter looks: 0 silver ramp, 1 black, 2 white, 3 red ramp.
        void logo (int x, int y, int k, int look = 0);
        void well (int x, int y, int width, int height);
        void band (int x, int y, int width);
        // Section header: style 0 grey, 1 solid navy, 2 navy under the title dithering into black.
        // Styles: 0 grey, 1 solid navy, 2 navy fade, 3 brown fade, 4 teal fade, 5 red fade,
        // 6 and 7 silver fade, 8 plain band with a red rule (silkscreen), 9 bright red fade.
        void headerBand (int x, int y, int width, int textWidth, int style);
        // One pixel outline.
        void frame (int x, int y, int width, int height, int ink);
        void led (int x, int y, bool on);
        void button (int x, int y, int width, const juce::String& label, bool on, bool withLed = true);
        // r = 15 (large) or 11 (small); value 0..1.
        void knob (int cx, int cy, int r, float value, bool enabled = true);
        // Panel print centred on cx in a band; reverse video when focused.
        void bandLabel (const juce::String& s, int cx, int bandY, bool reversed, bool enabled = true);

        // zoomHalves is the zoom in half steps: 2 = 1x, 3 = 1.5x, 4 = 2x, 6 = 3x.
        juce::Image toImage (int zoomHalves) const;
        void toImage (juce::Image& image, int zoomHalves) const;

    private:
        int w, h;
        int cx0 = 0, cy0 = 0, cx1, cy1;
        std::vector<uint8_t> px;
    };
}
