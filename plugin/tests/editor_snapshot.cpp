// Development tool: renders every page of the pixel faceplate to PNG at 1x
// and 2x, with a chord playing so the voice LEDs are lit.
//
// Usage: ambika_editor_snapshot <output directory>
// Writes <directory>/<page>-1x.png and <page>-2x.png and prints each path.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

#include "PluginEditor.h"
#include "PluginParameters.h"
#include "PluginProcessor.h"

namespace
{
    using Editor = ambika_plugin::AmbikaAudioProcessorEditor;

    void render (ambika_plugin::AmbikaAudioProcessor& processor, juce::MidiBuffer midi)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        for (int block = 0; block < 10; ++block)
        {
            processor.processBlock (buffer, midi);
            midi.clear();
        }
    }

    void setParameter (ambika_plugin::AmbikaAudioProcessor& processor, const juce::String& id, float value)
    {
        auto* parameter = processor.parameters().getParameter (id);
        if (parameter == nullptr)
        {
            std::printf ("no parameter %s\n", id.toRawUTF8());
            return;
        }
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    int failures = 0;

    void writePng (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream stream (file);
        if (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream))
            std::printf ("%s\n", file.getFullPathName().toRawUTF8());
        else
        {
            std::printf ("could not write %s\n", file.getFullPathName().toRawUTF8());
            ++failures;
        }
    }

    void snapshot (Editor& editor, const juce::File& dir, const juce::String& name)
    {
        const auto& canvas = editor.renderCanvas();
        writePng (canvas.toImage (2), dir.getChildFile (name + "-1x.png"));
        writePng (canvas.toImage (3), dir.getChildFile (name + "-1h5x.png"));
        writePng (canvas.toImage (4), dir.getChildFile (name + "-2x.png"));
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: %s <output directory>\n", argv[0]);
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File dir { juce::String (argv[1]) };
    dir.createDirectory();

    // Never touch the user's real settings (the zoom is remembered there).
    const auto settingsFile = juce::File::createTempFile ("eureka-snapshot.settings");
    ambika_plugin::AmbikaAudioProcessor::userSettingsFile() = settingsFile;

    ambika_plugin::AmbikaAudioProcessor processor;
    // Never touch the user's real preset folder.
    const auto presetDir = juce::File::createTempFile ("eureka-snapshot-presets");
    presetDir.createDirectory();
    processor.presets().setDirectory (presetDir);
    processor.setPlayConfigDetails (0, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    juce::MidiBuffer chord;
    for (int note : { 48, 55, 60, 64 })
        chord.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
    render (processor, chord);

    std::unique_ptr<Editor> editor (dynamic_cast<Editor*> (processor.createEditor()));
    if (editor == nullptr)
    {
        std::printf ("the editor is not the pixel faceplate\n");
        return 1;
    }
    if (editor->getWidth() != editor->scaled (Editor::canvasWidth))
    {
        std::printf ("editor size %d x %d does not match the zoom\n", editor->getWidth(), editor->getHeight());
        ++failures;
    }

    // Synth page: default focus, then the detailed line after an edit, then
    // each scope.
    editor->showPage (0);
    snapshot (*editor, dir, "synth");
    editor->focusParameter ("filter_cutoff", true);
    snapshot (*editor, dir, "synth-filter-edit");
    editor->focusParameter ("env1_attack");
    snapshot (*editor, dir, "synth-envelope");
    editor->focusParameter ("lfo2_shape");
    snapshot (*editor, dir, "synth-lfo");
    editor->focusParameter ("mix_balance");
    snapshot (*editor, dir, "synth-mixer");

    // Modulation matrix, both pages; slot 14 is the mod-wheel slot.
    setParameter (processor, "mod1_amount", 40.0f);
    setParameter (processor, "mod2_amount", -25.0f);
    setParameter (processor, "mod10_amount", 63.0f);
    editor->showPage (1);
    snapshot (*editor, dir, "mod-1-8");
    editor->selectSlot (13);
    snapshot (*editor, dir, "mod-9-16");
    editor->focusParameter ("modifier2_operator");
    snapshot (*editor, dir, "mod-modifier");

    editor->showPage (2);
    snapshot (*editor, dir, "multi");

    // Two parts: voices 1-3 on part 1, 4-6 on part 2, a different patch on
    // part 2, part 2 from C3 up.
    for (int v = 0; v < 6; ++v)
        setParameter (processor, ambika_plugin::voicePartId (v), v < 3 ? 1.0f : 2.0f);
    setParameter (processor, ambika_plugin::kVoiceModeId, 1.0f);  // cyclic
    setParameter (processor, ambika_plugin::kUnisonId, 2.0f);
    setParameter (processor, "p2_osc1_shape", 21.0f);
    setParameter (processor, "p2_filter_mode", 1.0f);
    setParameter (processor, "p2_filter_resonance", 50.0f);
    setParameter (processor, "p2_env1_attack", 60.0f);
    setParameter (processor, ambika_plugin::partKeyLowId (1), 60.0f);
    juce::MidiBuffer twoParts;
    twoParts.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
    twoParts.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    render (processor, twoParts);
    snapshot (*editor, dir, "multi-2parts");

    editor->editPart (1);
    editor->showPage (0);
    editor->focusParameter ("osc1_shape");
    snapshot (*editor, dir, "synth-part2");

    // Part 2 with a Surge filter: Diode Ladder (model 2), 24 dB variant.
    setParameter (processor, "p2_filter_model", 2.0f);
    setParameter (processor, "p2_filter_variant", 3.0f);
    setParameter (processor, "p2_filter_mode", 0.0f);
    setParameter (processor, "p2_filter_cutoff", 60.0f);
    setParameter (processor, "p2_filter_resonance", 45.0f);
    editor->focusParameter ("filter_model");
    snapshot (*editor, dir, "synth-surge");

    // Presets: a few saved sounds.
    auto& presets = processor.presets();
    presets.save (0, "Warm Saw Keys", { "Keys", "Poly" }, "eureka", "Default-ish saw, slow filter.", true);
    presets.save (1, "Vox Choir", { "Vocal", "Pad" }, "eureka", "Male formant wavetable through the BP filter.", true);
    presets.save (2, "Acid Line", { "Bass", "Mono", "acid" }, "someone", {}, true);
    presets.save (3, "Glass Bell", { "Bell", "FX" }, {}, {}, true);
    setParameter (processor, "p2_filter_resonance", 30.0f);
    editor->showPage (3);
    snapshot (*editor, dir, "presets");

    // The section header looks, on the synth page.
    editor->showPage (0);
    editor->focusParameter ("osc1_shape");
    for (int style = 0; style < 10; ++style)
    {
        editor->setHeaderStyle (style);
        snapshot (*editor, dir, "header-style-" + juce::String (style));
    }

    // Comparison sheet of the header options, written beside the snapshot
    // directory. The purple row recolours the navy ink, so it is a mock-up
    // outside the 16-colour palette.
    {
        struct Option { int style; const char* label; bool purple; };
        static const Option options[] = {
            { 2, "1  Navy, white text (current)", false },
            { 3, "2  Wood brown, gold text", false },
            { 4, "3  Dark teal, bright teal text", false },
            { 5, "4  Dark red, white text", false },
            { 6, "5  Silver, black text", false },
            { 7, "6  Silver, dark red text", false },
            { 8, "7  Silkscreen: plain band, white text, red rule", false },
            { 2, "8  Royal purple, white text (would replace navy in the palette)", true },
            { 9, "9  Bright red, white text", false } };
        const int count = (int) std::size (options);
        const int cropY = 132, cropH = 220, labelH = 24, w = Editor::canvasWidth * 2;
        juce::Image sheet (juce::Image::ARGB, w, count * (cropH + labelH), true, juce::SoftwareImageType());
        {
            juce::Graphics g (sheet);
            g.fillAll (juce::Colours::black);
            g.setFont (16.0f);
            for (int i = 0; i < count; ++i)
            {
                editor->setHeaderStyle (options[i].style);
                auto image = editor->renderCanvas().toImage (4);
                if (options[i].purple)
                    for (int y = cropY; y < cropY + cropH; ++y)
                        for (int x = 0; x < w; ++x)
                            if (image.getPixelAt (x, y) == juce::Colour (0xff000080))
                                image.setPixelAt (x, y, juce::Colour (0xff4a1090));
                const int top = i * (cropH + labelH);
                g.setColour (juce::Colours::white);
                g.drawText (options[i].label, 8, top, w - 16, labelH, juce::Justification::centredLeft);
                g.drawImage (image, 0, top + labelH, w, cropH, 0, cropY, w, cropH);
            }
        }
        const auto optionsDir = dir.getSiblingFile ("header-options");
        optionsDir.createDirectory();
        writePng (sheet, optionsDir.getChildFile ("sheet.png"));
    }

    // Full-page mock-ups of the red and silver looks. Each shows the page with
    // the logo on a panel in the header colour and, under it, the top strip
    // with the other logo treatment (letters in the header colour on black).
    {
        struct Mock { int style; const char* file; const char* label; const char* other; };
        static const Mock mocks[] = {
            { 5, "mock-dark-red.png", "Dark red headers, white text.  Logo A: white letters on a dark red panel", "Logo B: red letters on black" },
            { 9, "mock-bright-red.png", "Bright red headers, white text.  Logo A: white letters on a red panel", "Logo B: red letters on black" },
            { 6, "mock-silver.png", "Silver headers, black text.  Logo A: black letters on a silver panel", "Logo B: silver letters on black (the logo as it is now)" } };
        const int w = Editor::canvasWidth * 2, pageH = Editor::canvasHeight * 2, stripH = 232, labelH = 24;
        const auto optionsDir = dir.getSiblingFile ("header-options");
        for (const auto& mock : mocks)
        {
            juce::Image out (juce::Image::ARGB, w, labelH + pageH + labelH + stripH, true, juce::SoftwareImageType());
            {
                juce::Graphics g (out);
                g.fillAll (juce::Colours::black);
                g.setFont (16.0f);
                g.setColour (juce::Colours::white);
                editor->setHeaderStyle (mock.style);
                editor->setLogoVariant (0);
                g.drawText (mock.label, 8, 0, w - 16, labelH, juce::Justification::centredLeft);
                g.drawImageAt (editor->renderCanvas().toImage (4), 0, labelH);
                editor->setLogoVariant (1);
                g.drawText (mock.other, 8, labelH + pageH, w - 16, labelH, juce::Justification::centredLeft);
                g.drawImage (editor->renderCanvas().toImage (4), 0, labelH + pageH + labelH, w, stripH, 0, 0, w, stripH);
            }
            writePng (out, optionsDir.getChildFile (mock.file));
        }
        {
            juce::Image out (juce::Image::ARGB, w, labelH + pageH + labelH + stripH, true, juce::SoftwareImageType());
            juce::Graphics g (out);
            g.fillAll (juce::Colours::black);
            g.setFont (16.0f);
            g.setColour (juce::Colours::white);
            editor->setHeaderStyle (5);
            editor->setLogoVariant (3);
            g.drawText ("Dark red headers.  Logo D: silver letters on black (default)", 8, 0, w - 16, labelH, juce::Justification::centredLeft);
            g.drawImageAt (editor->renderCanvas().toImage (4), 0, labelH);
            editor->setLogoVariant (2);
            g.drawText ("Logo C: white letters on black", 8, labelH + pageH, w - 16, labelH, juce::Justification::centredLeft);
            g.drawImage (editor->renderCanvas().toImage (4), 0, labelH + pageH + labelH, w, stripH, 0, 0, w, stripH);
            writePng (out, optionsDir.getChildFile ("mock-logo-silver.png"));
        }
        editor->setLogoVariant (3);
    }
    editor->setHeaderStyle (5);

    // Character display size options, as one labelled comparison sheet of the top bar.
    {
        namespace px = ambika_plugin::pixel;
        editor->showPage (0);
        const auto& live = editor->renderCanvas();
        px::Canvas plate (Editor::canvasWidth, Editor::canvasHeight);
        plate.plate();
        constexpr int stripH = 108, labelH = 14;
        const juce::String d = juce::String::charToString (px::kDelimiter);
        const juce::String readout = "shap  saw" + d + "parm    0" + d + "rang    0" + d + "tune    0";
        const juce::String message = "saved: warm analog brass ensemble";
        const juce::String name = "p2 warm analog brass ensemble*", tags = "brass, pad, poly, vintage, ensemble";

        auto strip = [&] (const juce::String& label, int option)
        {
            px::Canvas c (Editor::canvasWidth, labelH + stripH);
            for (int y = 0; y < stripH; ++y)
                for (int x = 0; x < Editor::canvasWidth; ++x)
                    c.put (x, labelH + y, live.at (x, y));
            c.fill (0, 0, Editor::canvasWidth, labelH, px::BLK);
            c.text7 (label, 6, 4, px::WH);
            auto restore = [&] (int x0, int y0, int w, int h)
            {
                for (int y = y0; y < y0 + h; ++y)
                    for (int x = x0; x < x0 + w; ++x)
                        c.put (x, labelH + y, plate.at (x, y));
            };
            const int o = labelH;
            if (option == 0)  // B: as built now
            {
                c.fill (131, o + 21, 239, 16, px::BLK);
                c.vfd (131, o + 21, readout, message, 40);
                c.fill (395, o + 21, 227, 16, px::BLK);
                c.vfd (395, o + 21, name, tags, 38);
            }
            else
            {
                restore (118, 4, 522, 50);
                c.well (122, o + 8, 508, 42);
                c.fill (126, o + 12, 500, 34, px::BLK);
                c.vfd (137, o + 13, readout, message, 40, 2);
                if (option == 1)  // A: small logo, narrow program window under it
                {
                    restore (0, 4, 118, 50);
                    c.well (10, o + 8, 106, 16);
                    c.fill (12, o + 10, 102, 12, px::BLK);
                    c.logo (40, o + 10, 1, 0);
                    c.well (10, o + 26, 106, 24);
                    c.fill (12, o + 28, 102, 20, px::BLK);
                    c.vfd (15, o + 30, name.substring (0, 16), tags.substring (0, 16), 16);
                }
                else  // C: no program window; the name goes in the PROGRAM caption
                {
                    auto caption = name.toUpperCase();
                    while (px::Canvas::width7 (caption) > 146)
                        caption = caption.dropLastCharacters (1);
                    c.headerBand (326, o + 92, 154, px::Canvas::width7 (caption) + 4, 5);
                    c.text7 (caption, 330, o + 94, px::WH);
                }
            }
            return c.toImage (4);
        };

        std::vector<juce::Image> strips;
        const auto before = juce::ImageFileFormat::loadFrom (dir.getSiblingFile ("before").getChildFile ("synth-2x.png"));
        if (before.isValid())
        {
            px::Canvas label (Editor::canvasWidth, labelH);
            label.fill (0, 0, Editor::canvasWidth, labelH, px::BLK);
            label.text7 ("BEFORE: LAST COMMIT (SMALLER KNOBS AND LETTERING)", 6, 4, px::WH);
            juce::Image img (juce::Image::RGB, 1280, (labelH + stripH) * 2, true);
            juce::Graphics g (img);
            g.drawImageAt (label.toImage (4), 0, 0);
            g.drawImage (before, 0, labelH * 2, 1280, stripH * 2, 0, 0, 1280, stripH * 2);
            strips.push_back (img);
        }
        strips.push_back (strip ("B: KEEP THE DISPLAY TEXT SIZE (BUILT NOW)", 0));
        strips.push_back (strip ("A: DOUBLE THE DISPLAY - SMALL LOGO, 16-COLUMN PROGRAM WINDOW UNDER IT", 1));
        strips.push_back (strip ("C: DOUBLE THE DISPLAY - NO PROGRAM WINDOW, NAME IN THE PROGRAM CAPTION", 2));
        int h = 0;
        for (auto& s : strips)
            h += s.getHeight() + 8;
        juce::Image sheet (juce::Image::RGB, 1280, h, true);
        {
            juce::Graphics g (sheet);
            g.fillAll (juce::Colours::black);
            int y = 0;
            for (auto& s : strips)
            {
                g.drawImageAt (s, 0, y);
                y += s.getHeight() + 8;
            }
        }
        const auto displayDir = dir.getParentDirectory().getSiblingFile ("display-options");
        displayDir.createDirectory();
        writePng (sheet, displayDir.getChildFile ("sheet.png"));
    }

    editor->showPage (0);
    editor->showSaveDialog();
    snapshot (*editor, dir, "save-dialog");

    // Zoom: the window follows, and the setting is kept in the plug-in state.
    editor->setZoom (6);
    if (editor->getWidth() != Editor::canvasWidth * 3 || processor.editorZoomHalves() != 6)
    {
        std::printf ("zoom 3x did not resize the editor\n");
        ++failures;
    }
    editor->setZoom (3);
    if (editor->getWidth() != 960 || editor->getHeight() != 600 || processor.editorZoomHalves() != 3)
    {
        std::printf ("zoom 1.5x did not resize the editor\n");
        ++failures;
    }

    // The last size picked is remembered for new instances, and a song cannot change it.
    {
        ambika_plugin::AmbikaAudioProcessor second;
        juce::MemoryBlock song;
        processor.getStateInformation (song);
        second.setEditorZoomHalves (6);
        second.setStateInformation (song.getData(), (int) song.getSize());
        if (second.editorZoomHalves() != 6)
        {
            std::printf ("loading a song changed the zoom\n");
            ++failures;
        }
        second.setEditorZoomHalves (3);
    }
    {
        ambika_plugin::AmbikaAudioProcessor third;
        if (third.editorZoomHalves() != 3)
        {
            std::printf ("a new instance did not open at the last zoom (got %d)\n", third.editorZoomHalves());
            ++failures;
        }
    }
    editor->setZoom (4);

    presetDir.deleteRecursively();
    settingsFile.deleteFile();
    processor.editorBeingDeleted (editor.get());
    editor.reset();
    return failures == 0 ? 0 : 1;
}
