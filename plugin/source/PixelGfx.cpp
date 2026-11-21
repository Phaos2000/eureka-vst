#include "PixelGfx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

namespace ambika_plugin::pixel
{
    namespace
    {
        constexpr juce::uint32 kPalette[kNumInks] = {
            0xff000000, 0xff101418, 0xff2a2a2e, 0xff4a4a50, 0xff808084, 0xffc0c0c0, 0xffffffff,
            0xff0f2e28, 0xff2f8f78, 0xff5ff0c8,
            0xff5a1008, 0xffff3a2a,
            0xff4e2810, 0xff8c4c22,
            0xffd8b04a, 0xff000080,
        };

        constexpr int kBayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

        // JavaScript's Math.round (halves go up), as in the mock.
        int jsRound (double v) { return (int) std::floor (v + 0.5); }

        double hash (double n)
        {
            const double s = std::sin (n * 12.9898) * 43758.5453;
            return s - std::floor (s);
        }

        // 5x7 display font: 7 rows of 5 bits, as hex pairs. HD44780-style
        // lower case; '~' is the right arrow, 0x07 the dotted delimiter.
        struct Glyph5Def { char c; const char* rows; };
        constexpr Glyph5Def kFont5[] = {
            { 'A', "0E11111F111111" }, { 'B', "1E11111E11111E" }, { 'C', "0E11101010110E" }, { 'D', "1E11111111111E" },
            { 'E', "1F10101E10101F" }, { 'F', "1F10101E101010" }, { 'G', "0E11101711110F" }, { 'H', "1111111F111111" },
            { 'I', "0E04040404040E" }, { 'J', "0702020202120C" }, { 'K', "11121418141211" }, { 'L', "1010101010101F" },
            { 'M', "111B1515111111" }, { 'N', "11111915131111" }, { 'O', "0E11111111110E" }, { 'P', "1E11111E101010" },
            { 'Q', "0E11111115120D" }, { 'R', "1E11111E141211" }, { 'S', "0F10100E01011E" }, { 'T', "1F040404040404" },
            { 'U', "1111111111110E" }, { 'V', "11111111110A04" }, { 'W', "1111111515150A" }, { 'X', "11110A040A1111" },
            { 'Y', "11110A04040404" }, { 'Z', "1F01020408101F" },
            { 'a', "00000E010F110F" }, { 'b', "1010161911111E" }, { 'c', "00000E1010110E" }, { 'd', "01010D1311110F" },
            { 'e', "00000E111F100E" }, { 'f', "0609081C080808" }, { 'g', "000F11110F010E" }, { 'h', "10101619111111" },
            { 'i', "04000C0404040E" }, { 'j', "0200060202120C" }, { 'k', "10101214181412" }, { 'l', "0C04040404040E" },
            { 'm', "00001A15151111" }, { 'n', "00001619111111" }, { 'o', "00000E1111110E" }, { 'p', "00001E111E1010" },
            { 'q', "00000D130F0101" }, { 'r', "00001619101010" }, { 's', "00000E100E011E" }, { 't', "08081C08080906" },
            { 'u', "0000111111130D" }, { 'v', "00001111110A04" }, { 'w', "0000111115150A" }, { 'x', "0000110A040A11" },
            { 'y', "000011110F010E" }, { 'z', "00001F0204081F" },
            { '0', "0E11131519110E" }, { '1', "040C040404040E" }, { '2', "0E11010204081F" }, { '3', "1F02040201110E" },
            { '4', "02060A121F0202" }, { '5', "1F101E0101110E" }, { '6', "0608101E11110E" }, { '7', "1F010204080808" },
            { '8', "0E11110E11110E" }, { '9', "0E11110F01020C" },
            { '-', "0000001F000000" }, { '.', "00000000000C0C" }, { ':', "000C0C000C0C00" }, { '/', "00010204081000" },
            { '!', "04040404040004" }, { '+', "0004041F040400" }, { '>', "08040201020408" }, { '|', "04040404040404" },
            { '=', "00001F001F0000" }, { '&', "0C12140815120D" }, { '#', "0A0A1F0A1F0A0A" }, { '<', "02040810080402" },
            { '*', "0004150E150400" }, { '(', "02040808080402" }, { ')', "08040202020408" }, { ',', "000000000C0408" },
            { '\'', "0C040800000000" }, { '"', "0A0A0A00000000" }, { '%', "18190204081303" }, { '?', "0E110102040004" },
            { '_', "0000000000001F" }, { '[', "0E08080808080E" }, { ']', "0E02020202020E" }, { ';', "000C0C000C0408" },
            { '@', "0E11010D15150E" }, { '$', "040F140E051E04" }, { '^', "040A1100000000" },
            { kArrow, "0004021F020400" }, { kDelimiter, "04000400040004" },
        };

        // 3x5 panel print: 15 bits, row by row.
        struct Glyph3Def { char c; const char* bits; };
        constexpr Glyph3Def kFont3[] = {
            { 'A', "010101111101101" }, { 'B', "110101110101110" }, { 'C', "011100100100011" }, { 'D', "110101101101110" },
            { 'E', "111100110100111" }, { 'F', "111100110100100" }, { 'G', "011100101101011" }, { 'H', "101101111101101" },
            { 'I', "111010010010111" }, { 'J', "001001001101010" }, { 'K', "101101110101101" }, { 'L', "100100100100111" },
            { 'M', "101111111101101" }, { 'N', "110101101101101" }, { 'O', "010101101101010" }, { 'P', "110101110100100" },
            { 'Q', "010101101110011" }, { 'R', "110101110101101" }, { 'S', "011100010001110" }, { 'T', "111010010010010" },
            { 'U', "101101101101111" }, { 'V', "101101101101010" }, { 'W', "101101111111101" }, { 'X', "101101010101101" },
            { 'Y', "101101010010010" }, { 'Z', "111001010100111" },
            { '0', "111101101101111" }, { '1', "010110010010111" }, { '2', "110001010100111" }, { '3', "110001010001110" },
            { '4', "101101111001001" }, { '5', "111100110001110" }, { '6', "011100111101111" }, { '7', "111001010010010" },
            { '8', "111101111101111" }, { '9', "111101111001110" },
            { '-', "000000111000000" }, { '/', "001001010100100" }, { '+', "000010111010000" }, { '.', "000000000000010" },
            { ':', "000010000010000" }, { '<', "001010100010001" }, { '>', "100010001010100" }, { '!', "010010010000010" },
            { '?', "110001010000010" }, { '=', "000111000111000" }, { '#', "101111101111101" }, { ',', "000000000010100" },
            { '\'', "010010000000000" }, { '(', "001010010010001" }, { ')', "100010010010100" }, { '_', "000000000000111" },
            { '%', "101001010100101" }, { '*', "000101010101000" }, { '&', "010101010101011" }, { kArrow, "000001111001000" },
        };

        struct Fonts
        {
            std::array<std::array<uint8_t, 7>, 128> g5 {};
            std::array<uint16_t, 128> g3 {};
            Fonts()
            {
                for (const auto& d : kFont5)
                    for (int i = 0; i < 7; ++i)
                        g5[(size_t) d.c][(size_t) i] = (uint8_t) std::stoi (std::string (d.rows + i * 2, 2), nullptr, 16);
                for (const auto& d : kFont3)
                {
                    uint16_t bits = 0;
                    for (int i = 0; i < 15; ++i)
                        if (d.bits[i] == '1')
                            bits |= (uint16_t) (1u << i);
                    g3[(size_t) d.c] = bits;
                }
            }
        };
        const Fonts& fonts()
        {
            static const Fonts f;
            return f;
        }

        const uint8_t* glyph5 (juce::juce_wchar c)
        {
            return fonts().g5[(size_t) (c > 0 && c < 128 ? c : ' ')].data();
        }
        bool dot5 (const uint8_t* g, int row, int col)
        {
            return row >= 0 && row < 7 && col >= 0 && col < 5 && ((g[row] >> (4 - col)) & 1) != 0;
        }

        // Hand-drawn logo glyphs.
        struct LogoGlyph { char c; const char* rows[11]; };
        const LogoGlyph kLogo[] = {
            { 'e', { "", "", "", "", ".1111.", "11..11", "111111", "11....", "11....", "11..11", ".1111." } },
            { 'u', { "", "", "", "", "11..11", "11..11", "11..11", "11..11", "11..11", "11.111", ".11.11" } },
            { 'r', { "", "", "", "", "11.11.", "111.11", "111...", "11....", "11....", "11....", "11...." } },
            { 'k', { "11....", "11....", "11....", "11....", "11..11", "11.11.", "1111..", "111...", "1111..", "11.11.", "11..11" } },
            { 'a', { "", "", "", "", ".1111.", "....11", ".11111", "11..11", "11..11", "11.111", ".11.11" } },
            { '!', { "11", "11", "11", "11", "11", "11", "11", "11", "", "11", "11" } },
        };
    }

    juce::uint32 argb (int ink) { return kPalette[ink & 15]; }

    Canvas::Canvas (int width, int height) : w (width), h (height), cx1 (width), cy1 (height), px ((size_t) (width * height), BLK) {}

    void Canvas::fill (int x, int y, int width, int height, int ink)
    {
        const int x0 = std::max (0, x), y0 = std::max (0, y);
        const int x1 = std::min (w, x + width), y1 = std::min (h, y + height);
        for (int yy = y0; yy < y1; ++yy)
            std::fill (px.begin() + yy * w + x0, px.begin() + yy * w + std::max (x0, x1), (uint8_t) ink);
    }

    int Canvas::ramp (std::initializer_list<int> inks, double f, int x, int y)
    {
        const int n = (int) inks.size();
        f = std::clamp (f, 0.0, 1.0);
        const double v = f * (n - 1);
        const int i = (int) std::floor (v);
        const int step = (v - i) * 16.0 > kBayer[y & 3][x & 3] ? 1 : 0;
        return *(inks.begin() + std::min (n - 1, i + step));
    }

    void Canvas::text5 (const juce::String& s, int x, int y, int ink)
    {
        for (auto c : s)
        {
            const auto* g = glyph5 (c);
            for (int i = 0; i < 7; ++i)
                for (int b = 0; b < 5; ++b)
                    if (dot5 (g, i, b))
                        put (x + b, y + i, ink);
            x += 6;
        }
    }

    void Canvas::text3 (const juce::String& s, int x, int y, int ink)
    {
        for (auto c : s.toUpperCase())
        {
            const auto bits = fonts().g3[(size_t) (c > 0 && c < 128 ? c : ' ')];
            for (int i = 0; i < 15; ++i)
                if ((bits >> i) & 1)
                    put (x + i % 3, y + i / 3, ink);
            x += 4;
        }
    }

    void Canvas::vfd (int x, int y, const juce::String& line0, const juce::String& line1, int columns)
    {
        vfd (x, y, juce::StringArray { line0, line1 }, columns);
    }

    void Canvas::vfd (int x, int y, const juce::StringArray& lines, int columns)
    {
        for (int li = 0; li < lines.size(); ++li)
        {
            const auto s = lines[li].paddedRight (' ', columns).substring (0, columns);
            for (int ci = 0; ci < columns; ++ci)
            {
                const auto* g = glyph5 (s[ci]);
                for (int i = 0; i < 7; ++i)
                    for (int b = 0; b < 5; ++b)
                    {
                        int ink = VOFF;
                        if (dot5 (g, i, b))
                            ink = VON;
                        else if (dot5 (g, i - 1, b) || dot5 (g, i + 1, b) || dot5 (g, i, b - 1) || dot5 (g, i, b + 1))
                            ink = VHALO;
                        put (x + ci * 6 + b, y + li * 9 + i, ink);
                    }
            }
        }
    }

    void Canvas::logo (int x, int y, int k, int look)
    {
        int cx = 0;
        for (const char ch : std::string ("eureka!"))
        {
            for (const auto& g : kLogo)
            {
                if (g.c != ch)
                    continue;
                int gw = 0;
                for (int i = 0; i < 11; ++i)
                {
                    const int len = (int) std::strlen (g.rows[i]);
                    gw = std::max (gw, len);
                    for (int j = 0; j < len; ++j)
                        if (g.rows[i][j] == '1')
                        {
                            const double f = i / 10.0;
                            const int ink = look == 1 ? (int) BLK
                                          : look == 2 ? ramp ({ WH, WH, WH, LT }, f, cx + j, i)
                                          : look == 3 ? ramp ({ LED_ON, LED_ON, LED_OFF }, f, cx + j, i)
                                                      : ramp ({ WH, LT, LT, MID, D3 }, f, cx + j, i);
                            fill (x + (cx + j) * k, y + i * k, k, k, ink);
                        }
                }
                cx += gw + 1;
            }
        }
    }

    void Canvas::plate()
    {
        for (int y = 0; y < h; ++y)
        {
            const double f = 0.55 - 0.45 * y / h + 0.08 * (hash (y) - 0.5);
            for (int x = 0; x < w; ++x)
                put (x, y, ramp ({ BLK, D1, D2, D3 }, f, x, y));
        }
        fill (0, 0, w, 1, MID);
        fill (0, h - 1, w, 1, BLK);
    }

    void Canvas::band (int x, int y, int width)
    {
        fill (x, y, width, 9, D1);
        fill (x, y + 9, width, 1, BLK);
    }

    void Canvas::headerBand (int x, int y, int width, int textWidth, int style)
    {
        if (style <= 0 || style == 8)
        {
            band (x, y, width);
            if (style == 8)
                fill (x, y + 8, width, 1, LED_ON);
            return;
        }
        static const int fills[] = { NAVY, NAVY, NAVY, WOOD_DK, VOFF, LED_OFF, LT, LT, D1, LED_ON };
        const int ink = fills[std::clamp (style, 0, 9)];
        const int solid = style == 1 ? width : std::min (width, textWidth + 10);
        fill (x, y, solid, 9, ink);
        const int fade = std::min (width - solid, 40);
        for (int dx = 0; dx < width - solid; ++dx)
            for (int row = 0; row < 9; ++row)
            {
                const int cx = x + solid + dx, cy = y + row;
                const double f = (dx + 0.5) / std::max (1, fade);
                put (cx, cy, dx >= fade ? (int) BLK
                                 : ink == LT ? ramp ({ LT, MID, D3, D2, BLK }, f, cx, cy)
                                 : ink == LED_ON ? ramp ({ LED_ON, LED_OFF, BLK }, f, cx, cy)
                                                 : ramp ({ ink, (int) BLK }, f, cx, cy));
            }
        fill (x, y + 9, width, 1, BLK);
    }

    void Canvas::frame (int x, int y, int width, int height, int ink)
    {
        fill (x, y, width, 1, ink);
        fill (x, y + height - 1, width, 1, ink);
        fill (x, y, 1, height, ink);
        fill (x + width - 1, y, 1, height, ink);
    }

    void Canvas::well (int x, int y, int width, int height)
    {
        fill (x, y, width, height, D2);
        fill (x, y, width, 1, D3);
        fill (x, y, 1, height, D3);
        fill (x, y + height - 1, width, 1, BLK);
        fill (x + width - 1, y, 1, height, BLK);
    }

    void Canvas::led (int x, int y, bool on)
    {
        fill (x, y, 4, 3, on ? LED_ON : LED_OFF);
        if (on)
            put (x + 1, y, WH);
    }

    void Canvas::button (int x, int y, int width, const juce::String& label, bool on, bool withLed)
    {
        if (withLed)
            led (x + (width >> 1) - 2, y - 8, on);
        fill (x - 1, y - 1, width + 2, 18, BLK);
        fill (x, y, width, 16, on ? D1 : D2);
        fill (x, y, width, 1, on ? BLK : D3);
        fill (x, y + 15, width, 1, on ? D3 : BLK);
        fill (x, y, 1, 16, on ? BLK : D3);
        fill (x + width - 1, y, 1, 16, on ? D3 : BLK);
        text3c (label, x + (width >> 1), y + (on ? 6 : 5), on ? WH : LT);
    }

    void Canvas::line (int x0, int y0, int x1, int y1, int ink)
    {
        const int n = std::max (std::abs (x1 - x0), std::abs (y1 - y0));
        if (n == 0)
        {
            put (x0, y0, ink);
            return;
        }
        for (int i = 0; i <= n; ++i)
            put (jsRound (x0 + (double) (x1 - x0) * i / n), jsRound (y0 + (double) (y1 - y0) * i / n), ink);
    }

    void Canvas::knob (int cx, int cy, int r, float value, bool enabled)
    {
        const bool large = r >= 15;
        const int tickR = r + (large ? 4 : 3);
        const int capR = large ? 7 : 5;
        const double pi = juce::MathConstants<double>::pi;
        for (int k = 0; k <= 10; ++k)
        {
            const double a = (-135.0 + 27.0 * k) * pi / 180.0;
            put (cx + jsRound (std::sin (a) * tickR), cy - jsRound (std::cos (a) * tickR), enabled ? MID : D3);
        }
        circle (cx + 1, cy + 1, r, [] (int, int, int) { return (int) BLK; });
        circle (cx, cy, r, [&] (int dx, int dy, int d)
        {
            return d > (r - 1) * (r - 1) ? (int) BLK : ramp ({ BLK, D1, D2, D3, MID }, 0.42 - (dx + dy) / (2.6 * r), cx + dx, cy + dy);
        });
        const double capScale = 15.0 * capR / 7.0;
        circle (cx, cy, capR, [&] (int dx, int dy, int d)
        {
            if (d > (capR - 1) * (capR - 1))
                return (int) D3;
            return enabled ? ramp ({ D3, MID, LT, WH }, 0.5 - (dx + dy) / capScale, cx + dx, cy + dy)
                           : ramp ({ D2, D3, MID }, 0.5 - (dx + dy) / capScale, cx + dx, cy + dy);
        });
        const double a = (-135.0 + 270.0 * juce::jlimit (0.0f, 1.0f, value)) * pi / 180.0;
        line (cx + jsRound (std::sin (a) * (capR + 1)), cy - jsRound (std::cos (a) * (capR + 1)),
              cx + jsRound (std::sin (a) * (r - 2)), cy - jsRound (std::cos (a) * (r - 2)), enabled ? WH : MID);
    }

    void Canvas::bandLabel (const juce::String& s, int cx, int bandY, bool reversed, bool enabled)
    {
        const int tw = width3 (s);
        if (reversed)
        {
            fill (cx - (tw >> 1) - 3, bandY, tw + 6, 9, LT);
            text3c (s, cx, bandY + 2, D1);
        }
        else
        {
            text3c (s, cx, bandY + 2, enabled ? LT : D3);
        }
    }

    juce::Image Canvas::toImage (int zoomHalves) const
    {
        juce::Image image;
        toImage (image, zoomHalves);
        return image;
    }

    void Canvas::toImage (juce::Image& image, int zoomHalves) const
    {
        zoomHalves = std::max (2, zoomHalves);
        const int outW = w * zoomHalves / 2;
        const int outH = h * zoomHalves / 2;
        if (! image.isValid() || image.getWidth() != outW || image.getHeight() != outH
            || image.getFormat() != juce::Image::ARGB)
            image = juce::Image (juce::Image::ARGB, outW, outH, false, juce::SoftwareImageType());

        std::array<juce::PixelARGB, kNumInks> colours;
        for (int i = 0; i < kNumInks; ++i)
        {
            const auto c = kPalette[i];
            colours[(size_t) i].setARGB (0xff, (juce::uint8) (c >> 16), (juce::uint8) (c >> 8), (juce::uint8) c);
        }
        // Nearest neighbour; at 1.5x every other source pixel is two output pixels wide.
        std::vector<int> sourceX ((size_t) outW);
        for (int x = 0; x < outW; ++x)
            sourceX[(size_t) x] = x * 2 / zoomHalves;
        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < outH; ++y)
        {
            auto* dest = data.getLinePointer (y);
            const auto* row = &px[(size_t) ((y * 2 / zoomHalves) * w)];
            for (int x = 0; x < outW; ++x)
            {
                *reinterpret_cast<juce::PixelARGB*> (dest) = colours[row[sourceX[(size_t) x]]];
                dest += data.pixelStride;
            }
        }
    }
}
