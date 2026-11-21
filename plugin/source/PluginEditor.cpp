#include "PluginEditor.h"

#include <cmath>

#include "AmbikaImport.h"
#include "PluginParameters.h"
#include "analog.h"
#include "engine.h"
#include "parameters.h"
#include "preview.h"
#include "surge_filter.h"

namespace ambika_plugin
{
    using namespace pixel;

    namespace
    {
        constexpr juce::uint32 kDetailMs = 1500;  // detailed display line after an edit
        constexpr juce::uint32 kToastMs = 3000;
        constexpr int kLibraryRows = 15;

        // Synth page rows: section band, knob centre, label band.
        constexpr int kRowY[] { 120, 188, 256, 324 };
        constexpr int kPitch = 36;

        struct KnobSpec
        {
            const char* id;
            const char* label;
            const char* shortName;
        };

        juce::String lower (const juce::String& s) { return s.toLowerCase(); }

        const ambika::plugin::ParameterInfo* findParameterInfo (const juce::String& engineId)
        {
            for (int i = 0; i < ambika::plugin::NumParameters(); ++i)
                if (engineId == ambika::plugin::GetParameter (i).id)
                    return &ambika::plugin::GetParameter (i);
            return nullptr;
        }

        double shaped (double t)
        {
            return (1.0 - std::exp (-4.0 * t)) / (1.0 - std::exp (-4.0));
        }
    }

    //==========================================================================
    AmbikaAudioProcessorEditor::AmbikaAudioProcessorEditor (AmbikaAudioProcessor& p)
        : AudioProcessorEditor (p), audioProcessor (p), state (p.parameters()), presets (p.presets())
    {
        zoomHalves = juce::jlimit (2, 6, p.editorZoomHalves());
        setOpaque (true);
        setWantsKeyboardFocus (true);
        buildPlate();
        refreshLibrary();
        setSize (scaled (canvasWidth), scaled (canvasHeight));
        render();
        canvas.toImage (image, zoomHalves);
        shownPixels = canvas.pixels();
        startTimerHz (30);
    }

    AmbikaAudioProcessorEditor::~AmbikaAudioProcessorEditor()
    {
        stopTimer();
        if (dragParam != nullptr)
            dragParam->endChangeGesture();
    }

    //==========================================================================
    void AmbikaAudioProcessorEditor::paint (juce::Graphics& g)
    {
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImageAt (image, 0, 0);
    }

    void AmbikaAudioProcessorEditor::timerCallback()
    {
        refresh();
    }

    void AmbikaAudioProcessorEditor::refresh()
    {
        render();
        if (canvas.pixels() != shownPixels || image.getWidth() != scaled (canvasWidth))
        {
            shownPixels = canvas.pixels();
            canvas.toImage (image, zoomHalves);
            repaint();
        }
    }

    const Canvas& AmbikaAudioProcessorEditor::renderCanvas()
    {
        render();
        return canvas;
    }

    void AmbikaAudioProcessorEditor::render()
    {
        renderOnce();
        // The display always follows a control of the page: pick the first one
        // when the focused control is not on it (the presets page has none).
        if (page != 3 && ! focusVisible() && knobs.size() > firstPageKnob)
        {
            focus = knobs[firstPageKnob];
            renderOnce();
        }
    }

    void AmbikaAudioProcessorEditor::renderOnce()
    {
        hits.clear();
        knobs.clear();
        canvas.unclip();
        canvas.copyFrom (plateCache);
        drawHeader();
        firstPageKnob = knobs.size();
        switch (page)
        {
            case 0: drawSynthPage(); break;
            case 1: drawModPage(); break;
            case 2: drawMultiPage(); break;
            default: drawPresetsPage(); break;
        }
        drawDisplay();
        if (dialog == Dialog::Init)
            drawInitPopup();
        else if (dialog != Dialog::None)
            drawDialog();
    }

    bool AmbikaAudioProcessorEditor::focusVisible() const
    {
        if (focus.param == nullptr)
            return false;
        for (const auto& k : knobs)
            if (k.param == focus.param && k.section == focus.section)
                return true;
        return false;
    }

    void AmbikaAudioProcessorEditor::buildPlate()
    {
        auto& c = plateCache;
        c.plate();
        c.well (24, 16, 104, 40);
        c.fill (26, 18, 100, 36, BLK);
        {
            // The logo follows the section headers.
            const bool red = headerStyle == 5 || headerStyle == 9;
            const bool silver = headerStyle == 6 || headerStyle == 7;
            if ((red || silver) && logoVariant == 0)
                for (int yy = 0; yy < 36; ++yy)
                    for (int xx = 0; xx < 100; ++xx)
                    {
                        const double f = yy / 35.0;
                        c.fill (26 + xx, 18 + yy, 1, 1,
                                silver ? Canvas::ramp ({ WH, LT, LT, MID }, f, xx, yy)
                                : headerStyle == 9 ? Canvas::ramp ({ LED_ON, LED_ON, LED_OFF }, f, xx, yy)
                                                   : Canvas::ramp ({ LED_OFF, LED_OFF, LED_OFF, BLK }, f, xx, yy));
                    }
            c.logo (32, 25, 2, logoVariant == 3 ? 0 : logoVariant == 2 ? 2 : red ? (logoVariant == 0 ? 2 : 3) : silver && logoVariant == 0 ? 1 : 0);
        }
        c.well (136, 16, 256, 40);
        c.fill (140, 20, 248, 32, BLK);
        c.well (400, 16, 216, 40);
        c.fill (404, 20, 208, 32, BLK);
    }

    //==========================================================================
    // Parameters

    juce::RangedAudioParameter* AmbikaAudioProcessorEditor::parameter (const juce::String& id) const
    {
        return state.getParameter (id);
    }

    juce::RangedAudioParameter* AmbikaAudioProcessorEditor::partParameter (const juce::String& engineId) const
    {
        return parameter (partParameterId (audioProcessor.editedPart(), engineId));
    }

    int AmbikaAudioProcessorEditor::engineValue (const juce::String& engineId) const
    {
        return rawInt (partParameterId (audioProcessor.editedPart(), engineId));
    }

    int AmbikaAudioProcessorEditor::rawInt (const juce::String& id) const
    {
        if (auto* raw = state.getRawParameterValue (id))
            return juce::roundToInt (raw->load());
        return 0;
    }

    juce::String AmbikaAudioProcessorEditor::engineIdOf (const juce::String& parameterId)
    {
        if (parameterId.length() > 3 && parameterId[0] == 'p' && juce::CharacterFunctions::isDigit (parameterId[1])
            && parameterId[2] == '_')
            return parameterId.substring (3);
        return parameterId;
    }

    int AmbikaAudioProcessorEditor::stepCount (const Knob& k)
    {
        if (k.maxIndex >= 0)
            return k.maxIndex + 1;
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (k.param))
            return choice->choices.size();
        const auto& range = k.param->getNormalisableRange();
        if (dynamic_cast<juce::AudioParameterFloat*> (k.param) != nullptr)
            return juce::roundToInt ((range.end - range.start) / 0.5f) + 1;
        return juce::roundToInt (range.end - range.start) + 1;
    }

    int AmbikaAudioProcessorEditor::indexOf (const Knob& k)
    {
        const auto& range = k.param->getNormalisableRange();
        const float step = dynamic_cast<juce::AudioParameterFloat*> (k.param) != nullptr ? 0.5f : 1.0f;
        const float raw = k.param->convertFrom0to1 (k.param->getValue());
        return juce::jlimit (0, stepCount (k) - 1, juce::roundToInt ((raw - range.start) / step));
    }

    void AmbikaAudioProcessorEditor::setIndex (const Knob& k, int index)
    {
        const auto& range = k.param->getNormalisableRange();
        const float step = dynamic_cast<juce::AudioParameterFloat*> (k.param) != nullptr ? 0.5f : 1.0f;
        index = juce::jlimit (0, stepCount (k) - 1, index);
        k.param->setValueNotifyingHost (k.param->convertTo0to1 (range.start + (float) index * step));
    }

    juce::String AmbikaAudioProcessorEditor::valueText (const juce::RangedAudioParameter* p) const
    {
        if (p == nullptr)
            return {};
        const auto engineId = engineIdOf (p->paramID);
        const int raw = juce::roundToInt (p->convertFrom0to1 (p->getValue()));
        if (engineId == "filter_variant")
        {
            const int model = rawInt (partParameterId (audioProcessor.editedPart(), "filter_model"));
            if (model <= 0 || ambika::plugin::SurgeFilter::NumSubtypes (model) == 0)
                return "-";
            return lower (juce::String (ambika::plugin::SurgeFilter::SubtypeName (model, raw)));
        }
        if (engineId == "filter_model")
        {
            if (raw <= 0)
                return "ambika svf";
            return lower (juce::String (ambika::plugin::SurgeFilter::ModelName (raw)))
                .replace ("surge ", "", true).trim();
        }
        if (dynamic_cast<const juce::AudioParameterFloat*> (p) != nullptr)
            return juce::String::formatted ("%+.1f db", p->convertFrom0to1 (p->getValue()));
        auto text = lower (p->getCurrentValueAsText());
        if (dynamic_cast<const juce::AudioParameterInt*> (p) != nullptr && p->getNormalisableRange().start < 0
            && raw > 0 && ! text.startsWithChar ('+'))
            text = "+" + text;
        return text;
    }

    void AmbikaAudioProcessorEditor::edited()
    {
        lastEditTime = juce::Time::getMillisecondCounter();
    }

    //==========================================================================
    // Panel parts

    void AmbikaAudioProcessorEditor::addHit (juce::Rectangle<int> area, std::function<void()> onClick)
    {
        Hit h;
        h.area = area;
        h.onClick = std::move (onClick);
        hits.push_back (std::move (h));
    }

    void AmbikaAudioProcessorEditor::button (int x, int y, int w, const juce::String& label, bool on,
                                             std::function<void()> onClick, bool withLed)
    {
        canvas.button (x, y, w, label, on, withLed);
        addHit ({ x - 1, y - 1, w + 2, 18 }, std::move (onClick));
    }

    void AmbikaAudioProcessorEditor::sectionBand (int x, int y, int w, const juce::String& title, const juce::String& right)
    {
        static const int titleInks[] = { LT, WH, WH, GOLD, VON, WH, BLK, LED_OFF, WH, WH };
        static const int rightInks[] = { MID, LT, LT, LT, LT, LT, LT, LT, MID, LT };
        canvas.headerBand (x, y, w, Canvas::width3 (title) + 4, headerStyle);
        canvas.text3 (title, x + 4, y + 2, titleInks[headerStyle]);
        if (right.isNotEmpty())
            canvas.text3 (right, x + w - 4 - Canvas::width3 (right), y + 2, rightInks[headerStyle]);
    }

    void AmbikaAudioProcessorEditor::sectionFrame (int x, int y, int w, int h)
    {
        static const int inks[] = { -1, NAVY, NAVY, WOOD_DK, VOFF, LED_OFF, D3, D3, LED_OFF, LED_OFF };
        if (inks[headerStyle] >= 0)
            canvas.frame (x - 1, y - 1, w + 2, h + 1, inks[headerStyle]);
    }

    void AmbikaAudioProcessorEditor::knob (int cx, int cy, int r, int labelBandY, juce::RangedAudioParameter* p,
                                           const juce::String& label, const juce::String& shortName,
                                           const juce::String& section, bool enabled, int maxIndex)
    {
        if (p == nullptr)
            return;
        Knob k;
        k.param = p;
        k.label = label;
        k.shortName = shortName;
        k.section = section;
        k.enabled = enabled;
        k.maxIndex = maxIndex;
        if (const auto* info = findParameterInfo (engineIdOf (p->paramID)))
            k.longName = lower (juce::String (info->name));
        else
            k.longName = lower (p->getName (32));
        const int steps = stepCount (k);
        const float value = steps > 1 ? (float) indexOf (k) / (float) (steps - 1) : 0.0f;
        canvas.knob (cx, cy, r, value, enabled);
        canvas.bandLabel (label, cx, labelBandY, focus.param == p, enabled);
        knobs.push_back (k);
        Hit h;
        h.area = { cx - r - 4, cy - r - 4, 2 * r + 9, labelBandY + 10 - (cy - r - 4) };
        h.knob = (int) knobs.size() - 1;
        hits.push_back (std::move (h));
    }

    const AmbikaAudioProcessorEditor::Hit* AmbikaAudioProcessorEditor::hitAt (juce::Point<int> p) const
    {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
            if (it->area.contains (p))
                return &*it;
        return nullptr;
    }

    void AmbikaAudioProcessorEditor::notify (const juce::String& message, bool isError)
    {
        toast = message;
        toastIsError = isError;
        toastTime = juce::Time::getMillisecondCounter();
    }

    juce::StringArray AmbikaAudioProcessorEditor::wrap (const juce::String& text, int columns)
    {
        juce::StringArray lines;
        for (const auto& paragraph : juce::StringArray::fromLines (text))
        {
            juce::String line;
            for (auto word : juce::StringArray::fromTokens (paragraph, " ", ""))
            {
                if (word.isEmpty())
                    continue;
                while (word.length() > columns)
                {
                    if (line.isNotEmpty())
                        lines.add (line);
                    lines.add (word.substring (0, columns));
                    word = word.substring (columns);
                    line = {};
                }
                if (line.isEmpty())
                    line = word;
                else if (line.length() + 1 + word.length() <= columns)
                    line << " " << word;
                else
                {
                    lines.add (line);
                    line = word;
                }
            }
            lines.add (line);
        }
        return lines;
    }

    //==========================================================================
    // Header: logo, display, program window, part/page/program/zoom buttons, output

    void AmbikaAudioProcessorEditor::drawHeader()
    {
        const int part = audioProcessor.editedPart();

        // Program window.
        juce::String line0 = "p" + juce::String (part + 1) + " " + lower (presets.presetName (part))
                             + (presets.isModified (part) ? "*" : "");
        juce::String line1;
        if (toast.isNotEmpty() && juce::Time::getMillisecondCounter() - toastTime < kToastMs)
            line1 = (toastIsError ? "! " : "") + lower (toast);
        else
        {
            const auto tags = presets.presetTags (part);
            line1 = tags.isEmpty() ? juce::String ("no tags") : lower (tags.joinIntoString (", "));
        }
        canvas.vfd (409, 28, line0, line1, 33);

        // The control row. Each group is a box whose edges line up with the
        // sections of the first row below; the buttons sit two pixels inside it.
        constexpr int by = 79, bandY = by + 24, boxY = by - 10, boxH = 44;

        // Parts.
        for (int i = 0; i < kNumParts; ++i)
            button (26 + i * 24, by, 20, juce::String (i + 1), i == part, [this, i] { editPart (i); });
        sectionBand (24, bandY, 144, "PART");
        sectionFrame (24, boxY, 144, boxH);

        // Pages.
        static const char* const pageNames[] { "SYNTH", "MOD", "MULTI", "PRESETS" };
        for (int i = 0; i < 4; ++i)
            button (178 + i * 36, by, 32, pageNames[i], i == page, [this, i] { showPage (i); });
        sectionBand (176, bandY, 144, "PAGE");
        sectionFrame (176, boxY, 144, boxH);

        // Program.
        button (330, by, 18, "<", false, [this] { stepPreset (-1); }, false);
        button (352, by, 18, ">", false, [this] { stepPreset (1); }, false);
        button (374, by, 24, "SAVE", false, [this] { quickSave(); }, false);
        button (402, by, 32, "SAVE AS", false, [this] { showSaveDialog(); }, false);
        button (438, by, 24, "INIT", dialog == Dialog::Init, [this] { dialog = Dialog::Init; }, false);
        sectionBand (328, bandY, 136, "PROGRAM");
        sectionFrame (328, boxY, 136, boxH);

        // Zoom.
        {
            static const char* const labels[] = { "1X", "1.5X", "2X", "3X" };
            static const int halves[] = { 2, 3, 4, 6 };
            static const int widths[] = { 18, 26, 20, 20 };
            int bx = 474;
            for (int i = 0; i < 4; ++i)
            {
                const int h = halves[i];
                button (bx, by, widths[i], labels[i], zoomHalves == h, [this, h] { setZoom (h); });
                bx += widths[i] + 4;
            }
        }
        sectionBand (472, bandY, 100, "ZOOM");
        sectionFrame (472, boxY, 100, boxH);

        // Output level.
        canvas.band (580, bandY, 36);
        sectionFrame (580, boxY, 36, boxH);
        knob (598, by + 6, 11, bandY, parameter (kOutputGainId), "OUT", "out", "output");
    }

    //==========================================================================
    // Character display

    void AmbikaAudioProcessorEditor::displayLines (juce::String& line0, juce::String& line1) const
    {
        const int part = audioProcessor.editedPart();
        if (! focusVisible())
        {
            line0 = "part " + juce::String (part + 1) + ": " + lower (presets.presetName (part))
                    + (presets.isModified (part) ? "*" : "");
            if (page == 3)
            {
                line1 = juce::String ((int) filtered.size()) + " of " + juce::String ((int) library.size()) + " presets";
                if (search.isNotEmpty())
                    line1 << " matching \"" << lower (search) << "\"";
            }
            return;
        }

        std::vector<const Knob*> group;
        for (const auto& k : knobs)
            if (k.section == focus.section && group.size() < 8)
                group.push_back (&k);

        juce::String lines[2];
        int focusLine = 0;
        for (int i = 0; i < 8; ++i)
        {
            juce::String cell;
            cell << (i % 4 == 0 ? ' ' : kDelimiter);
            if (i < (int) group.size())
            {
                auto name = group[(size_t) i]->shortName.substring (0, 4);
                if (group[(size_t) i]->param == focus.param)
                {
                    name = name.substring (0, 1).toUpperCase() + name.substring (1);
                    focusLine = i / 4;
                }
                auto value = valueText (group[(size_t) i]->param);
                if (value.length() > 4)
                    value = value.replace ("part ", "p").removeCharacters (" ");
                cell << name.paddedRight (' ', 4) << ' ' << value.substring (0, 4).paddedLeft (' ', 4);
            }
            else
                cell = cell.paddedRight (' ', 10);
            lines[i / 4] << cell;
        }

        if (lastEditTime != 0 && juce::Time::getMillisecondCounter() - lastEditTime < kDetailMs)
        {
            juce::String detail;
            detail << ' ' << focus.longName.substring (0, 18).paddedRight (' ', 18) << kDelimiter
                   << valueText (focus.param).substring (0, 20).paddedLeft (' ', 20);
            lines[focusLine] = detail;
        }
        line0 = lines[0];
        line1 = lines[1];
    }

    void AmbikaAudioProcessorEditor::drawDisplay()
    {
        juce::String line0, line1;
        displayLines (line0, line1);
        canvas.vfd (145, 28, line0, line1, 40);
    }

    //==========================================================================
    // Synth page

    void AmbikaAudioProcessorEditor::drawSynthPage()
    {
        auto row = [this] (int x, int rowIndex, const juce::String& title, const juce::String& section,
                           std::initializer_list<KnobSpec> specs, std::function<bool (const juce::String&)> enabled = {},
                           std::function<int (const juce::String&)> maxIndex = {})
        {
            const int y = kRowY[rowIndex];
            const int w = (int) specs.size() * kPitch;
            sectionBand (x, y, w, title);
            canvas.band (x, y + 43, w);
            sectionFrame (x, y, w, 53);
            int i = 0;
            for (const auto& s : specs)
            {
                const juce::String id (s.id);
                knob (x + 18 + i * kPitch, y + 25, 11, y + 43, partParameter (id), s.label, s.shortName, section,
                      enabled ? enabled (id) : true, maxIndex ? maxIndex (id) : -1);
                ++i;
            }
        };

        for (int o = 1; o <= 2; ++o)
        {
            const juce::String n (o);
            const auto id = [&] (const char* suffix) { return "osc" + n + suffix; };
            const juce::String shape = id ("_shape"), param = id ("_parameter"), range = id ("_range"), tune = id ("_tune");
            row (o == 1 ? 24 : 176, 0, "OSC " + n, "osc " + n,
                 { { shape.toRawUTF8(), "SHAPE", "shap" }, { param.toRawUTF8(), "PARAM", "parm" },
                   { range.toRawUTF8(), "RANGE", "rang" }, { tune.toRawUTF8(), "TUNE", "tune" } });
        }
        row (328, 0, "MIXER", "mixer",
             { { "mix_balance", "BALANCE", "bal" }, { "mix_operator", "XMOD", "xmod" }, { "mix_amount", "AMOUNT", "amt" },
               { "mix_sub_shape", "SUB", "sub" }, { "mix_sub_level", "SUB LVL", "slvl" }, { "mix_noise", "NOISE", "nois" },
               { "mix_fuzz", "FUZZ", "fuzz" }, { "mix_crush", "CRUSH", "crsh" } });

        const int model = engineValue ("filter_model");
        const int variants = model > 0 ? ambika::plugin::SurgeFilter::NumSubtypes (model) : 0;
        row (24, 1, "FILTER", "filter",
             { { "filter_cutoff", "CUTOFF", "cut" }, { "filter_resonance", "RESO", "res" }, { "filter_mode", "MODE", "mode" },
               { "filter_model", "MODEL", "modl" }, { "filter_variant", "VARIANT", "var" }, { "filter_env", "ENV 2", "env2" },
               { "filter_lfo", "LFO 2", "lfo2" } },
             [&] (const juce::String& id)
             {
                 if (id == "filter_mode")
                     return model == 0;
                 if (id == "filter_variant")
                     return variants > 0;
                 return true;
             },
             [&] (const juce::String& id) { return id == "filter_variant" ? juce::jmax (0, variants - 1) : -1; });

        for (int e = 1; e <= 3; ++e)
        {
            const juce::String n (e);
            juce::StringArray ids;
            for (const char* s : { "env#_attack", "env#_decay", "env#_sustain", "env#_release", "lfo#_shape", "lfo#_rate", "lfo#_sync" })
                ids.add (juce::String (s).replace ("#", n));
            const int x = e == 2 ? 24 : 284;
            const int r = e == 1 ? 1 : 2;
            row (x, r, "ENVELOPE " + n + " + LFO " + n, "env/lfo " + n,
                 { { ids[0].toRawUTF8(), "ATTACK", "atk" }, { ids[1].toRawUTF8(), "DECAY", "dec" },
                   { ids[2].toRawUTF8(), "SUSTAIN", "sus" }, { ids[3].toRawUTF8(), "RELEASE", "rel" },
                   { ids[4].toRawUTF8(), "SHAPE", "shap" }, { ids[5].toRawUTF8(), "RATE", "rate" },
                   { ids[6].toRawUTF8(), "SYNC", "sync" } });
        }
        row (544, 1, "VOICE LFO", "voice lfo", { { "voice_lfo_shape", "SHAPE", "shap" }, { "voice_lfo_rate", "RATE", "rate" } });

        // Maker's print.
        canvas.text3c ("6 PART", 580, kRowY[2] + 14, MID);
        canvas.text3c ("HYBRID", 580, kRowY[2] + 22, MID);
        canvas.text3c ("POLYSYNTH", 580, kRowY[2] + 30, MID);
        canvas.fill (548, kRowY[2] + 40, 64, 1, D3);

        row (24, 3, "PART", "part",
             { { "part_polyphony", "VOICING", "voic" }, { "part_legato", "LEGATO", "lega" }, { "part_volume", "VOLUME", "vol" },
               { "part_pan", "PAN", "pan" }, { "part_portamento", "GLIDE", "glid" }, { "part_octave", "OCTAVE", "oct" },
               { "part_tuning", "TUNE", "tune" }, { "part_spread", "SPREAD", "sprd" } });

        // Scope: the waveform, curve or response of the section being edited.
        juce::String caption;
        canvas.well (320, 336, 296, 40);
        drawScope (324, 340, 288, 32, caption);
        sectionBand (320, kRowY[3], 296, "SCOPE", caption);
        sectionFrame (320, kRowY[3], 296, 53);
    }

    void AmbikaAudioProcessorEditor::drawScope (int x, int y, int w, int h, juce::String& caption)
    {
        canvas.fill (x - 2, y - 2, w + 4, h + 4, BLK);
        canvas.fill (x, y, w, h, VOFF);
        std::vector<double> level ((size_t) w, -1.0);

        const auto id = focus.param != nullptr ? engineIdOf (focus.param->paramID) : juce::String();
        const auto& section = focus.section;
        constexpr int n = 280;
        uint8_t buffer[n];

        auto plotBuffer = [&]
        {
            for (int i = 0; i < w; ++i)
                level[(size_t) i] = buffer[i * n / w] / 255.0;
        };
        auto oscillator = [&] (int o, uint8_t* out)
        {
            const juce::String p = "osc" + juce::String (o);
            ambika::plugin::RenderOscillatorPreview ((uint8_t) engineValue (p + "_shape"), (uint8_t) engineValue (p + "_parameter"),
                                                     (int8_t) engineValue (p + "_range"), out, n);
        };

        if (section == "osc 1" || section == "osc 2")
        {
            oscillator (section.getTrailingIntValue(), buffer);
            plotBuffer();
            caption = "OSC " + juce::String (section.getTrailingIntValue());
        }
        else if (section == "filter")
        {
            const int cutoff = engineValue ("filter_cutoff"), resonance = engineValue ("filter_resonance");
            const int mode = engineValue ("filter_mode"), model = engineValue ("filter_model");
            const int variant = engineValue ("filter_variant");
            constexpr double minHz = 20.0, maxHz = 20000.0;
            constexpr int points = 120;
            if (model > 0)
            {
                const std::array<int, 5> key { cutoff, resonance, mode, model, variant };
                if (key != surgeKey)
                {
                    surgeKey = key;
                    std::vector<double> frequencies (points);
                    for (int i = 0; i < points; ++i)
                        frequencies[(size_t) i] = minHz * std::pow (maxHz / minHz, (double) i / (points - 1));
                    surgeCurve.assign (points, 0.0);
                    const double cv = juce::jlimit (0.0, 255.0, cutoff * 2.0 - 8.0);
                    ambika::plugin::SurgeFilter::MeasureResponse (
                        model, variant, ambika::plugin::AnalogVoice::CutoffToHz (cv), (float) (resonance * 4.0 / 255.0),
                        ambika::plugin::Engine::kNativeSampleRate * ambika::plugin::Engine::kOversampling,
                        frequencies.data(), surgeCurve.data(), points);
                }
            }
            for (int i = 0; i < w; ++i)
            {
                const double t = (double) i / (w - 1);
                double magnitude;
                if (model > 0)
                {
                    const double position = t * (points - 1);
                    const int index = juce::jlimit (0, points - 2, (int) position);
                    const double frac = position - index;
                    magnitude = surgeCurve[(size_t) index] * (1.0 - frac) + surgeCurve[(size_t) index + 1] * frac;
                }
                else
                    magnitude = ambika::plugin::FilterResponse ((uint8_t) cutoff, (uint8_t) resonance, (uint8_t) mode,
                                                                minHz * std::pow (maxHz / minHz, t));
                const double db = juce::Decibels::gainToDecibels (magnitude, -100.0);
                level[(size_t) i] = juce::jlimit (0.0, 1.0, (db + 36.0) / 54.0);
            }
            caption = "FILTER 20HZ-20KHZ";
        }
        else if (section.startsWith ("env/lfo") && id.startsWith ("lfo"))
        {
            const juce::String e (section.getTrailingIntValue());
            ambika::plugin::RenderLfoPreview ((uint8_t) engineValue ("lfo" + e + "_shape"), buffer, n);
            plotBuffer();
            caption = "LFO " + e;
        }
        else if (section.startsWith ("env/lfo"))
        {
            const juce::String e (section.getTrailingIntValue());
            const double wa = std::sqrt (ambika::plugin::EnvelopeSegmentSeconds ((uint8_t) engineValue ("env" + e + "_attack")));
            const double wd = std::sqrt (ambika::plugin::EnvelopeSegmentSeconds ((uint8_t) engineValue ("env" + e + "_decay")));
            const double wr = std::sqrt (ambika::plugin::EnvelopeSegmentSeconds ((uint8_t) engineValue ("env" + e + "_release")));
            const double sustain = engineValue ("env" + e + "_sustain") / 127.0;
            const double hold = w * 0.18;
            const double scale = (w - hold) / juce::jmax (wa + wd + wr, 2.4);
            const double xa = wa * scale, xd = xa + wd * scale, xs = xd + hold, xr = xs + wr * scale;
            for (int i = 0; i < w; ++i)
            {
                const double px = i;
                double v = 0.0;
                if (px < xa)
                    v = shaped (px / juce::jmax (1.0, xa));
                else if (px < xd)
                    v = 1.0 + (sustain - 1.0) * shaped ((px - xa) / juce::jmax (1.0, xd - xa));
                else if (px < xs)
                    v = sustain;
                else if (px < xr)
                    v = sustain * (1.0 - shaped ((px - xs) / juce::jmax (1.0, xr - xs)));
                level[(size_t) i] = v;
            }
            caption = "ENVELOPE " + e;
        }
        else if (section == "voice lfo")
        {
            ambika::plugin::RenderLfoPreview ((uint8_t) engineValue ("voice_lfo_shape"), buffer, n);
            plotBuffer();
            caption = "VOICE LFO";
        }
        else
        {
            uint8_t second[n];
            oscillator (1, buffer);
            oscillator (2, second);
            const int balance = engineValue ("mix_balance");
            for (int i = 0; i < n; ++i)
                buffer[i] = (uint8_t) ((buffer[i] * (63 - balance) + second[i] * balance) / 63);
            plotBuffer();
            caption = "OSC 1 + OSC 2";
        }

        // Curve, then a halo around it.
        auto yOf = [&] (double v) { return y + h - 2 - juce::roundToInt (juce::jlimit (0.0, 1.0, v) * (h - 3)); };
        for (int i = 0; i < w; ++i)
        {
            if (level[(size_t) i] < 0.0)
                continue;
            const int y1 = yOf (level[(size_t) i]);
            const int y0 = i > 0 && level[(size_t) i - 1] >= 0.0 ? yOf (level[(size_t) i - 1]) : y1;
            for (int yy = juce::jmin (y0, y1); yy <= juce::jmax (y0, y1); ++yy)
                canvas.put (x + i, yy, VON);
        }
        std::vector<juce::Point<int>> halo;
        for (int yy = y; yy < y + h; ++yy)
            for (int xx = x; xx < x + w; ++xx)
                if (canvas.at (xx, yy) == VOFF
                    && ((xx > x && canvas.at (xx - 1, yy) == VON) || (xx < x + w - 1 && canvas.at (xx + 1, yy) == VON)
                        || (yy > y && canvas.at (xx, yy - 1) == VON) || (yy < y + h - 1 && canvas.at (xx, yy + 1) == VON)))
                    halo.push_back ({ xx, yy });
        for (auto p : halo)
            canvas.put (p.x, p.y, VHALO);
    }

    //==========================================================================
    // Modulation page

    void AmbikaAudioProcessorEditor::drawModPage()
    {
        const int first = matrixPage * 8;
        sectionBand (24, 120, 592, "MODULATION MATRIX",
                     "SLOTS " + juce::String (first + 1) + "-" + juce::String (first + 8) + " OF 16");
        canvas.well (24, 132, 592, 108);
        canvas.fill (28, 136, 584, 100, BLK);

        for (int i = 0; i < 8; ++i)
        {
            const int slot = first + i;
            const juce::String n (slot + 1);
            const int y = 141 + i * 12;
            const bool selected = slot == selectedSlot;
            auto* source = partParameter ("mod" + n + "_source");
            auto* destination = partParameter ("mod" + n + "_destination");
            auto* amount = partParameter ("mod" + n + "_amount");
            if (source == nullptr || destination == nullptr || amount == nullptr)
                continue;
            const int ink = selected ? BLK : VON;
            if (selected)
                canvas.fill (31, y - 2, 578, 11, VON);
            canvas.text5 (n.paddedLeft ('0', 2), 34, y, ink);
            canvas.text5 (valueText (source).toUpperCase().substring (0, 14), 52, y, ink);
            canvas.text5 (valueText (destination).toUpperCase().substring (0, 14), 140, y, ink);
            canvas.text5 (valueText (amount).toUpperCase(), 230, y, ink);

            // Amount bar: 16 segments each way from the centre.
            const int value = juce::roundToInt (amount->convertFrom0to1 (amount->getValue()));
            const int lit = juce::roundToInt (std::abs (value) / 63.0 * 16.0);
            canvas.fill (401, y - 1, 2, 9, ink);
            for (int s = 0; s < 16; ++s)
            {
                const bool right = s < lit && value > 0, left = s < lit && value < 0;
                const int off = selected ? VHALO : VOFF;
                canvas.fill (404 + s * 6, y, 5, 7, right ? ink : off);
                canvas.fill (395 - s * 6, y, 5, 7, left ? ink : off);
            }
            if (slot == 13)
                canvas.text5 ("X MOD WHEEL", 506, y, ink);

            // Click selects the slot; dragging the row sets its amount.
            Knob k;
            k.param = amount;
            k.shortName = "amt";
            k.longName = "mod " + n + " amount";
            knobs.push_back (k);
            Hit h;
            h.area = { 29, y - 2, 582, 12 };
            h.knob = (int) knobs.size() - 1;
            h.onClick = [this, slot, amount]
            {
                selectedSlot = slot;
                focus.param = amount;
                focus.section = "slot " + juce::String (slot + 1);
                focus.shortName = "amt";
                focus.longName = "mod " + juce::String (slot + 1) + " amount";
            };
            hits.push_back (std::move (h));
        }

        sectionFrame (24, 120, 592, 121);

        // Matrix page buttons.
        sectionBand (24, kRowY[2], 72, "PAGE");
        canvas.band (24, kRowY[2] + 43, 72);
        button (26, kRowY[2] + 22, 32, "1-8", matrixPage == 0, [this] { showMatrixPage (0); });
        button (62, kRowY[2] + 22, 32, "9-16", matrixPage == 1, [this] { showMatrixPage (1); });
        sectionFrame (24, kRowY[2], 72, 53);

        // Selected slot.
        const juce::String n (selectedSlot + 1);
        const juce::String slotSection = "slot " + n;
        const int slotY = kRowY[2], slotBand = slotY + 43, slotKnob = slotY + 25;
        sectionBand (104, slotY, 108, "SLOT " + n.paddedLeft ('0', 2));
        canvas.band (104, slotBand, 108);
        knob (122, slotKnob, 11, slotBand, partParameter ("mod" + n + "_source"), "SOURCE", "src", slotSection);
        knob (158, slotKnob, 11, slotBand, partParameter ("mod" + n + "_destination"), "DEST", "dst", slotSection);
        knob (194, slotKnob, 11, slotBand, partParameter ("mod" + n + "_amount"), "AMOUNT", "amt", slotSection);
        sectionFrame (104, slotY, 108, 53);

        // Notes.
        sectionBand (220, kRowY[2], 396, "NOTES");
        canvas.well (220, kRowY[2] + 12, 396, 40);
        canvas.fill (222, kRowY[2] + 14, 392, 36, BLK);
        juce::String note0, note1;
        if (focus.section.startsWith ("modifier"))
        {
            const juce::String m (focus.section.getTrailingIntValue());
            note0 = "modifier " + m + ": " + valueText (partParameter ("modifier" + m + "_operator")) + " of "
                    + valueText (partParameter ("modifier" + m + "_in1")) + " and "
                    + valueText (partParameter ("modifier" + m + "_in2"));
            note1 = "its output is a modulation source for the matrix.";
        }
        else
        {
            note0 = "slot " + n + ": " + valueText (partParameter ("mod" + n + "_source")) + " " + juce::String::charToString (kArrow)
                    + " " + valueText (partParameter ("mod" + n + "_destination")) + ", "
                    + valueText (partParameter ("mod" + n + "_amount"));
            note1 = selectedSlot == 13 ? "slot 14 is scaled by the mod wheel, as on the hardware."
                                       : "click a row to pick its slot; drag a row to set its amount.";
        }
        canvas.vfd (226, kRowY[2] + 24, note0, note1, 64);
        sectionFrame (220, kRowY[2], 396, 53);

        // Modifiers.
        for (int m = 0; m < 4; ++m)
        {
            const int x = 24 + m * 116;
            const juce::String k (m + 1);
            const juce::String section = "modifier " + k;
            const int y = kRowY[3];
            sectionBand (x, y, 108, "MODIFIER " + k);
            canvas.band (x, y + 43, 108);
            knob (x + 18, y + 25, 11, y + 43, partParameter ("modifier" + k + "_in1"), "IN 1", "in1", section);
            knob (x + 54, y + 25, 11, y + 43, partParameter ("modifier" + k + "_operator"), "OP", "op", section);
            knob (x + 90, y + 25, 11, y + 43, partParameter ("modifier" + k + "_in2"), "IN 2", "in2", section);
            sectionFrame (x, y, 108, 53);
        }
        // Maker's print, placed as on the synth page.
        canvas.text3c ("6 PART", 552, kRowY[3] + 14, MID);
        canvas.text3c ("HYBRID", 552, kRowY[3] + 22, MID);
        canvas.text3c ("POLYSYNTH", 552, kRowY[3] + 30, MID);
        canvas.fill (520, kRowY[3] + 40, 64, 1, D3);
    }

    //==========================================================================
    // Multi page

    void AmbikaAudioProcessorEditor::drawMultiPage()
    {
        const int edited = audioProcessor.editedPart();

        // Voice cards.
        for (int v = 0; v < kNumVoices; ++v)
        {
            const int x = 24 + v * 100;
            const int owner = rawInt (voicePartId (v)) - 1;  // -1 = off
            sectionBand (x, 120, 92, "VOICE " + juce::String (v + 1));
            canvas.well (x, 132, 92, 29);
            canvas.fill (x + 2, 134, 88, 25, BLK);
            const juce::StringArray text = owner < 0 ? juce::StringArray { "off", "" }
                                                     : juce::StringArray { "part " + juce::String (owner + 1),
                                                                           lower (presets.presetName (owner)) };
            canvas.vfd (x + 7, 138, text, 13);
            canvas.band (x, 207, 92);
            knob (x + 19, 184, 11, 207, parameter (voicePartId (v)), "PART", "v" + juce::String (v + 1), "voices");
            canvas.button (x + 43, 179, 40, "EDIT", owner >= 0 && owner == edited, true);
            canvas.led (x + 61, 171, audioProcessor.voiceLevel (v) > 8);
            sectionFrame (x, 120, 92, 97);
            addHit ({ x + 42, 178, 42, 18 }, [this, owner]
            {
                if (owner >= 0)
                {
                    editPart (owner);
                    showPage (0);
                }
            });
        }

        // Allocation.
        const int mode = rawInt (kVoiceModeId);
        const int unison = rawInt (kUnisonId);
        sectionBand (24, 232, 184, "VOICE ALLOCATION");
        canvas.band (24, 275, 72);
        knob (42, 257, 11, 275, parameter (kVoiceModeId), "MODE", "mode", "allocation");
        knob (78, 257, 11, 275, parameter (kUnisonId), "UNISON", "uni", "allocation", mode != 0);
        canvas.well (100, 244, 108, 24);
        canvas.fill (102, 246, 104, 20, BLK);
        canvas.vfd (106, 248, "mode " + valueText (parameter (kVoiceModeId)),
                    "unison " + (mode == 0 ? juce::String ("-") : juce::String (unison)), 16);

        juce::String description;
        if (mode == 0)
            description = "Every part plays every note on its own cards, using its Voicing (poly, mono, 2x unison, cyclic) "
                          "from the Synth page. Parts that own cards layer. Unison applies to Cyclic and Poly.";
        else
        {
            description = mode == 1 ? "Notes step through the voice cards in order, 1, 2, 3 ... 6, whatever part each card "
                                      "plays, so successive notes can cycle through different patches."
                                    : "Each note takes the free card that has rested longest; when all are busy the "
                                      "oldest note is stolen.";
            description << (unison > 1 ? " Unison " + juce::String (unison) + ": each note plays " + juce::String (unison)
                                             + " cards at once (detuned by each part's Spread)."
                                       : juce::String (" Raise Unison to play 2-6 cards per note."));
        }
        canvas.well (24, 287, 184, 89);
        canvas.fill (26, 289, 180, 85, BLK);
        auto lines = wrap (lower (description), 29);
        lines.removeRange (9, lines.size());
        canvas.vfd (29, 292, lines, 29);
        sectionFrame (24, 232, 184, 145);

        // Key ranges.
        sectionBand (216, 232, 400, "KEY RANGES");
        canvas.well (216, 244, 400, 64);
        canvas.fill (218, 246, 396, 60, BLK);
        for (int p = 0; p < kNumParts; ++p)
        {
            const int y = 250 + p * 9;
            const int low = rawInt (partKeyLowId (p)), high = rawInt (partKeyHighId (p));
            const int ink = p == edited ? VON : VHALO;
            canvas.text5 ("p" + juce::String (p + 1) + " " + lower (noteName (low)).paddedRight (' ', 3) + " "
                              + lower (noteName (high)),
                          222, y, ink);
            for (int note = 0; note < 128; ++note)
            {
                const int pc = note % 12;
                const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
                const bool in = note >= low && note <= high;
                canvas.fill (288 + note * 2, y, 2, 7, in ? (black ? VHALO : VON) : (pc == 0 ? VHALO : VOFF));
            }
            canvas.text5 (lower (presets.presetName (p)).substring (0, 10), 550, y, ink);
        }
        sectionFrame (216, 232, 400, 77);
        for (int p = 0; p < kNumParts; ++p)
        {
            const int x = 216 + p * 68, y = kRowY[3];
            const juce::String n (p + 1);
            const juce::String section = p < 4 ? "keys 1-4" : "keys 5-6";
            sectionBand (x, y, 60, "PART " + n);
            canvas.band (x, y + 43, 60);
            knob (x + 15, y + 25, 11, y + 43, parameter (partKeyLowId (p)), "LOW", n + "lo", section);
            knob (x + 45, y + 25, 11, y + 43, parameter (partKeyHighId (p)), "HIGH", n + "hi", section);
            sectionFrame (x, y, 60, 53);
        }
    }

    //==========================================================================
    // Presets page

    juce::StringArray AmbikaAudioProcessorEditor::libraryTags() const
    {
        auto tags = PresetManager::standardTags();
        for (const auto& info : library)
            for (const auto& t : info.tags)
                if (! tags.contains (t, true))
                    tags.add (t);
        return tags;
    }

    void AmbikaAudioProcessorEditor::drawPresetsPage()
    {
        // Search.
        sectionBand (24, 120, 376, "SEARCH", "TYPE TO SEARCH");
        canvas.well (24, 132, 376, 20);
        canvas.fill (26, 134, 372, 16, BLK);
        canvas.vfd (29, 138, juce::StringArray { lower (search) + "_" }, 61);
        addHit ({ 24, 132, 376, 20 }, [this] { search.clear(); applyFilter(); });
        sectionFrame (24, 120, 376, 33);

        // Library.
        sectionBand (24, 168, 376, "LIBRARY", juce::String ((int) filtered.size()) + " PRESETS");
        canvas.well (24, 180, 376, 148);
        canvas.fill (26, 182, 372, 144, BLK);
        const int rows = (int) filtered.size();
        scrollRow = juce::jlimit (0, juce::jmax (0, rows - kLibraryRows), scrollRow);
        for (int i = 0; i < kLibraryRows; ++i)
        {
            const int row = scrollRow + i;
            const int y = 187 + i * 9;
            if (row >= rows)
                break;
            const auto& info = filtered[(size_t) row];
            const bool selected = row == selectedRow;
            if (selected)
                canvas.fill (27, y - 1, 366, 9, VON);
            const int ink = selected ? BLK : VON;
            canvas.text5 (lower (info.name).substring (0, 30), 30, y, ink);
            canvas.text5 (lower (info.tags.joinIntoString (",")).substring (0, 29), 214, y, selected ? BLK : VHALO);
            Hit h;
            h.area = { 26, y - 1, 368, 9 };
            h.onClick = [this, row] { selectRow (row); };
            h.onDoubleClick = [this, row] { selectRow (row); loadSelected(); };
            h.onWheel = [this] (int d) { scrollRow -= d * 3; };
            hits.push_back (std::move (h));
        }
        if (rows > kLibraryRows)
        {
            const int track = 142;
            const int barH = juce::jmax (6, track * kLibraryRows / rows);
            const int barY = 183 + (track - barH) * scrollRow / juce::jmax (1, rows - kLibraryRows);
            canvas.fill (395, 183, 2, track, D1);
            canvas.fill (395, barY, 2, barH, MID);
        }
        {
            Hit h;  // wheel anywhere on the list
            h.area = { 24, 180, 376, 148 };
            h.onWheel = [this] (int d) { scrollRow -= d * 3; };
            hits.insert (hits.end() - juce::jmin ((int) hits.size(), juce::jmin (kLibraryRows, juce::jmax (0, rows - scrollRow))), h);
        }

        sectionFrame (24, 168, 376, 161);

        // Tags.
        sectionBand (408, 120, 208, "TAGS");
        const auto tags = libraryTags();
        for (int i = 0; i < juce::jmin (16, tags.size()); ++i)
        {
            const auto tag = tags[i];
            const bool on = activeTags.contains (tag, true);
            button (410 + (i % 4) * 52, 133 + (i / 4) * 20, 48, tag.substring (0, 12), on, [this, tag]
            {
                if (activeTags.contains (tag, true))
                    activeTags.removeString (tag, true);
                else
                    activeTags.add (tag);
                applyFilter();
            }, false);
        }

        sectionFrame (408, 120, 208, 93);

        // Details of the selected preset.
        sectionBand (408, 228, 208, "DETAILS");
        canvas.well (408, 240, 208, 88);
        canvas.fill (410, 242, 204, 84, BLK);
        juce::StringArray lines;
        if (const auto* info = selectedPreset())
        {
            lines.add (lower (info->name));
            lines.addArray (wrap ("tags: " + (info->tags.isEmpty() ? juce::String ("-") : lower (info->tags.joinIntoString (", "))), 33));
            lines.add ("by: " + (info->author.isEmpty() ? juce::String ("-") : lower (info->author)));
            lines.add (info->factory ? juce::String ("factory program, read only")
                                     : "saved " + info->modified.formatted ("%Y-%m-%d %H:%M"));
            lines.add ({});
            lines.addArray (wrap (lower (info->comment), 33));
        }
        else
        {
            lines.addArray (wrap ("no preset selected.", 33));
            lines.add ({});
            lines.addArray (wrap ("click a preset to select it; double-click or enter loads it into part "
                                      + juce::String (audioProcessor.editedPart() + 1) + ".",
                                  33));
            lines.add ({});
            lines.addArray (wrap ("type to search. tags narrow the list to presets that have them all.", 33));
        }
        lines.removeRange (9, lines.size());
        canvas.vfd (413, 244, lines, 33);
        sectionFrame (408, 228, 208, 101);

        // Actions.
        constexpr int actionY = 344, actionButtonY = actionY + 13;
        sectionBand (24, actionY, 376, "PRESET");
        const auto* selected = selectedPreset();
        button (26, actionButtonY, 40, "LOAD", false, [this] { loadSelected(); }, false);
        button (74, actionButtonY, 40, "RENAME", false, [this] { openRename(); }, false);
        button (122, actionButtonY, 40, deleteArmed ? "SURE?" : "DELETE", deleteArmed, [this] { deleteSelected(); }, false);
        button (170, actionButtonY, 40, "FOLDER", false, [this]
        {
            auto dir = presets.getDirectory();
            dir.createDirectory();
            dir.revealToUser();
        }, false);
        button (218, actionButtonY, 40, "RESCAN", false, [this]
        {
            refreshLibrary();
            notify ("Library rescanned: " + juce::String ((int) library.size()) + " presets");
        }, false);
        if (selected != nullptr && selected->factory)
            canvas.text3 ("FACTORY: READ ONLY", 266, actionButtonY + 6, MID);
        sectionFrame (24, actionY, 376, 33);

        // Ambika files.
        sectionBand (408, actionY, 208, "IMPORT AMBIKA FILES");
        button (410, actionButtonY, 100, "TO PART", false, [this] { loadAmbikaFile(); }, false);
        button (514, actionButtonY, 100, "TO LIBRARY", false, [this] { importToLibrary(); }, false);
        sectionFrame (408, actionY, 208, 33);
    }

    //==========================================================================
    // Save / rename dialog and the INIT popup

    void AmbikaAudioProcessorEditor::textField (int x, int y, int w, const juce::String& label, int field)
    {
        const bool focused = focusedField == field;
        if (focused)
        {
            canvas.fill (x - 82, y + 5, Canvas::width3 (label) + 6, 9, LT);
            canvas.text3 (label, x - 79, y + 7, D1);
        }
        else
            canvas.text3 (label, x - 79, y + 7, LT);
        canvas.well (x, y, w, 20);
        canvas.fill (x + 2, y + 2, w - 4, 16, BLK);
        const int columns = (w - 12) / 6;
        auto text = fieldText[field] + (focused ? "_" : "");
        if (text.length() > columns)
            text = text.substring (text.length() - columns);
        canvas.text5 (text, x + 6, y + 6, VON);
        addHit ({ x - 82, y, w + 82, 20 }, [this, field] { focusedField = field; });
    }

    bool AmbikaAudioProcessorEditor::dialogClash() const
    {
        const auto name = fieldText[0].trim();
        if (name.isEmpty())
            return false;
        const auto file = presets.fileForName (name);
        if (dialog == Dialog::Rename)
            return file.existsAsFile() && file != renaming;
        return file.existsAsFile();
    }

    void AmbikaAudioProcessorEditor::drawDialog()
    {
        addHit ({ 0, 0, canvasWidth, canvasHeight }, [] {});  // modal
        const bool rename = dialog == Dialog::Rename;
        canvas.fill (71, 127, 498, 233, BLK);
        canvas.well (72, 128, 496, 232);
        sectionBand (72, 128, 496, rename ? juce::String ("RENAME PRESET")
                                          : "SAVE PART " + juce::String (audioProcessor.editedPart() + 1) + " AS PRESET");
        sectionFrame (72, 128, 496, 232);

        textField (160, 148, 392, "NAME", 0);
        if (! rename)
        {
            canvas.text3 ("TAGS", 81, 182, LT);
            const auto& standard = PresetManager::standardTags();
            for (int i = 0; i < standard.size(); ++i)
            {
                const auto tag = standard[i];
                button (160 + (i % 7) * 56, 176 + (i / 7) * 20, 52, tag, dialogTags.contains (tag, true), [this, tag]
                {
                    if (dialogTags.contains (tag, true))
                        dialogTags.removeString (tag, true);
                    else
                        dialogTags.add (tag);
                }, false);
            }
            textField (160, 220, 392, "EXTRA TAGS", 1);
            textField (160, 244, 392, "AUTHOR", 2);
            textField (160, 268, 392, "COMMENT", 3);
        }

        juce::String warning = dialogError;
        if (warning.isEmpty() && dialogClash())
            warning = rename ? "Another preset already has this name." : "A preset with this name exists. Saving will replace it.";
        if (warning.isNotEmpty())
            canvas.text3 (warning.substring (0, 98), 160, 300, LED_ON);
        if (! rename)
            canvas.text3 ("TAB: NEXT FIELD   ENTER: SAVE   ESC: CANCEL", 80, 326, MID);

        button (400, 320, 72, "CANCEL", false, [this] { closeDialog(); }, false);
        const bool canConfirm = fieldText[0].trim().isNotEmpty() && ! (rename && dialogClash());
        const juce::String confirm = rename ? "RENAME" : (dialogClash() ? "REPLACE" : "SAVE");
        canvas.button (480, 320, 72, confirm, false, false);
        if (canConfirm)
            addHit ({ 479, 319, 74, 18 }, [this] { confirmDialog(); });
        else
            canvas.text3c (confirm, 516, 325, D3);
    }

    void AmbikaAudioProcessorEditor::drawInitPopup()
    {
        addHit ({ 0, 0, canvasWidth, canvasHeight }, [this] { dialog = Dialog::None; });
        canvas.fill (399, 99, 162, 50, BLK);
        canvas.well (400, 100, 160, 48);
        const int part = audioProcessor.editedPart();
        button (408, 108, 144, "INIT PART " + juce::String (part + 1), false, [this, part]
        {
            dialog = Dialog::None;
            presets.initialise (part);
            notify ("Part " + juce::String (part + 1) + " initialised");
        }, false);
        button (408, 128, 144, "INIT ALL PARTS", false, [this]
        {
            dialog = Dialog::None;
            presets.initialiseAll();
            notify ("Everything initialised: all parts and the voice routing");
        }, false);
    }

    //==========================================================================
    // Navigation

    void AmbikaAudioProcessorEditor::showPage (int index)
    {
        page = juce::jlimit (0, 3, index);
        deleteArmed = false;
        if (page == 3)
            refreshLibrary();
        refresh();
    }

    void AmbikaAudioProcessorEditor::showMatrixPage (int newPage)
    {
        matrixPage = juce::jlimit (0, 1, newPage);
        if (selectedSlot / 8 != matrixPage)
        {
            selectedSlot = matrixPage * 8;
            focus = {};
        }
        refresh();
    }

    void AmbikaAudioProcessorEditor::editPart (int part)
    {
        audioProcessor.setEditedPart (part);
        // Keep the same control in focus on the new part.
        if (focus.param != nullptr)
        {
            const auto engineId = engineIdOf (focus.param->paramID);
            if (findParameterInfo (engineId) != nullptr)
                focus.param = partParameter (engineId);
        }
        surgeKey = { -1, -1, -1, -1, -1 };
        refresh();
    }

    void AmbikaAudioProcessorEditor::setZoom (int halves)
    {
        zoomHalves = juce::jlimit (2, 6, halves);
        audioProcessor.setEditorZoomHalves (zoomHalves);
        setSize (scaled (canvasWidth), scaled (canvasHeight));
        render();
        shownPixels = canvas.pixels();
        canvas.toImage (image, zoomHalves);
        repaint();
    }

    void AmbikaAudioProcessorEditor::setLogoVariant (int variant)
    {
        logoVariant = variant;
        setHeaderStyle (headerStyle);
    }

    void AmbikaAudioProcessorEditor::setHeaderStyle (int style)
    {
        headerStyle = juce::jlimit (0, 9, style);
        buildPlate();
        render();
        shownPixels = canvas.pixels();
        canvas.toImage (image, zoomHalves);
        repaint();
    }

    void AmbikaAudioProcessorEditor::selectSlot (int slot)
    {
        selectedSlot = juce::jlimit (0, 15, slot);
        matrixPage = selectedSlot / 8;
        page = 1;
        focusParameter ("mod" + juce::String (selectedSlot + 1) + "_amount");
    }

    void AmbikaAudioProcessorEditor::focusParameter (const juce::String& id, bool asEdit)
    {
        renderOnce();
        auto* p = findParameterInfo (id) != nullptr ? partParameter (id) : parameter (id);
        for (const auto& k : knobs)
            if (k.param == p && k.section.isNotEmpty())
            {
                focus = k;
                break;
            }
        if (asEdit)
            edited();
        render();
    }

    //==========================================================================
    // Mouse and keys

    void AmbikaAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
    {
        grabKeyboardFocus();
        armedAtClick = deleteArmed;
        deleteArmed = false;
        const auto* hit = hitAt (toCanvas (e.getPosition()));
        if (hit == nullptr)
        {
            refresh();
            return;
        }
        const Hit h = *hit;
        if (h.knob >= 0)
        {
            const Knob k = knobs[(size_t) h.knob];
            if (k.section.isNotEmpty())
                focus = k;
            if (k.enabled)
            {
                dragKnob = k;
                dragParam = k.param;
                dragStartIndex = indexOf (k);
                dragStartY = e.y;
                dragParam->beginChangeGesture();
            }
        }
        if (h.onClick)
            h.onClick();
        refresh();
    }

    void AmbikaAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragParam == nullptr)
            return;
        double pixelsPerStep = juce::jlimit (1.5, 12.0, 200.0 / stepCount (dragKnob));
        if (e.mods.isShiftDown())
            pixelsPerStep *= 4.0;
        const int index = dragStartIndex + (int) std::floor ((dragStartY - e.y) / pixelsPerStep + 0.5);
        if (juce::jlimit (0, stepCount (dragKnob) - 1, index) != indexOf (dragKnob))
        {
            setIndex (dragKnob, index);
            edited();
        }
        refresh();
    }

    void AmbikaAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
    {
        if (dragParam != nullptr)
        {
            dragParam->endChangeGesture();
            dragParam = nullptr;
        }
    }

    void AmbikaAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto* hit = hitAt (toCanvas (e.getPosition()));
        if (hit == nullptr)
            return;
        const Hit h = *hit;
        if (h.knob >= 0)
        {
            const Knob k = knobs[(size_t) h.knob];
            if (! k.enabled)
                return;
            const bool open = dragParam == k.param;
            if (! open)
                k.param->beginChangeGesture();
            k.param->setValueNotifyingHost (k.param->getDefaultValue());
            if (! open)
                k.param->endChangeGesture();
            edited();
        }
        else if (h.onDoubleClick)
            h.onDoubleClick();
        refresh();
    }

    void AmbikaAudioProcessorEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        const float delta = std::abs (wheel.deltaY) > 0.0f ? wheel.deltaY : wheel.deltaX;
        if (delta == 0.0f)
            return;
        const int direction = (delta > 0.0f) != wheel.isReversed ? 1 : -1;
        const auto* hit = hitAt (toCanvas (e.getPosition()));
        if (hit == nullptr)
            return;
        const Hit h = *hit;
        if (h.knob >= 0)
        {
            const Knob k = knobs[(size_t) h.knob];
            if (k.section.isNotEmpty())
                focus = k;
            if (k.enabled)
            {
                k.param->beginChangeGesture();
                setIndex (k, indexOf (k) + direction);
                k.param->endChangeGesture();
                edited();
            }
        }
        else if (h.onWheel)
            h.onWheel (direction);
        refresh();
    }

    bool AmbikaAudioProcessorEditor::editText (juce::String& text, const juce::KeyPress& key, int maxLength)
    {
        if (key.getKeyCode() == juce::KeyPress::backspaceKey)
        {
            text = text.dropLastCharacters (1);
            return true;
        }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'V')
        {
            text = (text + juce::SystemClipboard::getTextFromClipboard().upToFirstOccurrenceOf ("\n", false, false))
                       .substring (0, maxLength);
            return true;
        }
        const auto c = key.getTextCharacter();
        if (c >= 32 && c < 127 && ! key.getModifiers().isCommandDown())
        {
            if (text.length() < maxLength)
                text += juce::String::charToString (c);
            return true;
        }
        return false;
    }

    bool AmbikaAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
    {
        bool handled = false;
        if (dialog == Dialog::Init)
        {
            if (key == juce::KeyPress::escapeKey)
            {
                dialog = Dialog::None;
                handled = true;
            }
        }
        else if (dialog != Dialog::None)
        {
            const int fields = dialog == Dialog::Rename ? 1 : 4;
            if (key == juce::KeyPress::escapeKey)
                closeDialog();
            else if (key == juce::KeyPress::returnKey)
                confirmDialog();
            else if (key.getKeyCode() == juce::KeyPress::tabKey)
                focusedField = (focusedField + (key.getModifiers().isShiftDown() ? fields - 1 : 1)) % fields;
            else
            {
                dialogError.clear();
                editText (fieldText[focusedField], key, focusedField == 3 ? 400 : 120);
            }
            handled = true;
        }
        else if (page == 3)
        {
            const int rows = (int) filtered.size();
            if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
            {
                if (rows > 0)
                {
                    const int row = juce::jlimit (0, rows - 1, selectedRow + (key == juce::KeyPress::upKey ? -1 : 1));
                    selectRow (row);
                    if (row < scrollRow)
                        scrollRow = row;
                    else if (row >= scrollRow + kLibraryRows)
                        scrollRow = row - kLibraryRows + 1;
                }
                handled = true;
            }
            else if (key == juce::KeyPress::returnKey)
            {
                loadSelected();
                handled = true;
            }
            else if (key == juce::KeyPress::escapeKey)
            {
                search.clear();
                applyFilter();
                handled = true;
            }
            else if (editText (search, key, 60))
            {
                applyFilter();
                handled = true;
            }
        }
        if (handled)
            refresh();
        return handled;
    }

    //==========================================================================
    // Presets

    void AmbikaAudioProcessorEditor::refreshLibrary()
    {
        library = presets.scan();
        applyFilter();
    }

    void AmbikaAudioProcessorEditor::applyFilter()
    {
        const auto previous = selectedPreset() != nullptr ? selectedPreset()->file : juce::File();
        filtered.clear();
        for (const auto& info : library)
        {
            bool ok = true;
            for (const auto& tag : activeTags)
                ok = ok && info.tags.contains (tag, true);
            if (ok && search.isNotEmpty())
                ok = info.name.containsIgnoreCase (search) || info.author.containsIgnoreCase (search)
                     || info.comment.containsIgnoreCase (search) || info.tags.joinIntoString (" ").containsIgnoreCase (search);
            if (ok)
                filtered.push_back (info);
        }
        selectedRow = -1;
        for (size_t i = 0; i < filtered.size(); ++i)
            if (previous != juce::File() && filtered[i].file == previous)
                selectedRow = (int) i;
        scrollRow = juce::jlimit (0, juce::jmax (0, (int) filtered.size() - kLibraryRows), scrollRow);
    }

    const PresetInfo* AmbikaAudioProcessorEditor::selectedPreset() const
    {
        return juce::isPositiveAndBelow (selectedRow, (int) filtered.size()) ? &filtered[(size_t) selectedRow] : nullptr;
    }

    void AmbikaAudioProcessorEditor::selectRow (int row)
    {
        selectedRow = row;
    }

    void AmbikaAudioProcessorEditor::loadSelected()
    {
        const auto* info = selectedPreset();
        if (info == nullptr)
            return;
        const int part = audioProcessor.editedPart();
        const auto name = info->name;
        const auto result = presets.load (part, info->file);
        if (result.wasOk())
            notify ("Loaded \"" + name + "\" into part " + juce::String (part + 1));
        else
            notify (result.getErrorMessage(), true);
    }

    void AmbikaAudioProcessorEditor::stepPreset (int direction)
    {
        const int part = audioProcessor.editedPart();
        const auto file = presets.neighbour (part, direction);
        if (file == juce::File())
        {
            notify ("No presets saved yet. Use Save as... to create one.");
            return;
        }
        const auto result = presets.load (part, file);
        if (result.failed())
            notify (result.getErrorMessage(), true);
    }

    void AmbikaAudioProcessorEditor::quickSave()
    {
        const int part = audioProcessor.editedPart();
        if (! presets.presetFile (part).existsAsFile())
        {
            showSaveDialog();
            return;
        }
        const auto name = presets.presetName (part);
        const auto result = presets.save (part, name, presets.presetTags (part), presets.presetAuthor (part),
                                          presets.presetComment (part), true);
        if (result.wasOk())
            notify ("Saved \"" + name + "\"");
        else
            notify (result.getErrorMessage(), true);
        refreshLibrary();
    }

    void AmbikaAudioProcessorEditor::showSaveDialog()
    {
        const int part = audioProcessor.editedPart();
        const auto name = presets.presetName (part);
        fieldText[0] = name == "Init" ? juce::String() : name;
        dialogTags.clear();
        juce::StringArray extra;
        for (const auto& tag : presets.presetTags (part))
        {
            if (tag.equalsIgnoreCase ("Factory"))
                continue;
            if (PresetManager::standardTags().contains (tag, true))
                dialogTags.add (tag);
            else
                extra.add (tag);
        }
        fieldText[1] = extra.joinIntoString (", ");
        fieldText[2] = presets.presetAuthor (part);
        fieldText[3] = presets.presetComment (part);
        focusedField = 0;
        dialogError.clear();
        dialog = Dialog::Save;
        refresh();
    }

    void AmbikaAudioProcessorEditor::openRename()
    {
        const auto* info = selectedPreset();
        if (info == nullptr)
            return;
        if (info->factory)
        {
            notify ("Factory programs are read only", true);
            return;
        }
        renaming = info->file;
        fieldText[0] = info->name;
        focusedField = 0;
        dialogError.clear();
        dialog = Dialog::Rename;
    }

    void AmbikaAudioProcessorEditor::deleteSelected()
    {
        const auto* info = selectedPreset();
        if (info == nullptr)
            return;
        if (info->factory)
        {
            notify ("Factory programs are read only", true);
            return;
        }
        if (! armedAtClick)
        {
            deleteArmed = true;
            return;
        }
        const auto name = info->name;
        const auto result = presets.remove (info->file);
        if (result.wasOk())
            notify ("Moved \"" + name + "\" to the Trash");
        else
            notify (result.getErrorMessage(), true);
        refreshLibrary();
    }

    void AmbikaAudioProcessorEditor::loadAmbikaFile()
    {
        const int part = audioProcessor.editedPart();
        chooser = std::make_unique<juce::FileChooser> ("Load an Ambika file into part " + juce::String (part + 1),
                                                       juce::File(), ambika_format::kFilePatterns);
        juce::Component::SafePointer<AmbikaAudioProcessorEditor> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe, part] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (safe == nullptr || file == juce::File())
                                      return;
                                  const auto result = safe->presets.load (part, file);
                                  if (result.wasOk())
                                      safe->notify ("Loaded \"" + file.getFileName() + "\"");
                                  else
                                      safe->notify (result.getErrorMessage(), true);
                                  safe->refresh();
                              });
    }

    void AmbikaAudioProcessorEditor::importToLibrary()
    {
        chooser = std::make_unique<juce::FileChooser> ("Add Ambika files to the eureka! library", juce::File(),
                                                       ambika_format::kFilePatterns);
        juce::Component::SafePointer<AmbikaAudioProcessorEditor> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  const auto files = fc.getResults();
                                  if (safe == nullptr || files.isEmpty())
                                      return;
                                  juce::StringArray errors;
                                  const int added = safe->presets.importToLibrary (files, errors);
                                  juce::String message = "Added " + juce::String (added) + " preset(s) to Imported";
                                  if (! errors.isEmpty())
                                      message << " (" << errors.size() << " skipped: " << errors[0] << ")";
                                  safe->notify (message, added == 0);
                                  safe->refreshLibrary();
                                  safe->refresh();
                              });
    }

    void AmbikaAudioProcessorEditor::confirmDialog()
    {
        const auto name = fieldText[0].trim();
        if (name.isEmpty())
            return;
        if (dialog == Dialog::Rename)
        {
            if (dialogClash())
                return;
            const auto result = presets.rename (renaming, name);
            if (result.failed())
            {
                dialogError = result.getErrorMessage();
                return;
            }
            notify ("Renamed to \"" + name + "\"");
        }
        else if (dialog == Dialog::Save)
        {
            auto tags = dialogTags;
            for (const auto& t : juce::StringArray::fromTokens (fieldText[1], ",", ""))
                if (t.trim().isNotEmpty() && ! tags.contains (t.trim(), true))
                    tags.add (t.trim());
            const auto result = presets.save (audioProcessor.editedPart(), name, tags, fieldText[2].trim(),
                                              fieldText[3].trim(), true);
            if (result.failed())
            {
                dialogError = result.getErrorMessage();
                return;
            }
            notify ("Saved \"" + name + "\"");
        }
        closeDialog();
        refreshLibrary();
    }

    void AmbikaAudioProcessorEditor::closeDialog()
    {
        dialog = Dialog::None;
        dialogError.clear();
    }
}
