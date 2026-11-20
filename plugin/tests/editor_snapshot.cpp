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

    editor->showPage (0);
    editor->showSaveDialog();
    snapshot (*editor, dir, "save-dialog");

    // The three section header looks, on the synth page.
    editor->showPage (0);
    editor->focusParameter ("osc1_shape");
    for (int style = 0; style < 3; ++style)
    {
        editor->setHeaderStyle (style);
        snapshot (*editor, dir, "header-style-" + juce::String (style));
    }
    editor->setHeaderStyle (2);

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
    editor->setZoom (4);

    presetDir.deleteRecursively();
    processor.editorBeingDeleted (editor.get());
    editor.reset();
    return failures == 0 ? 0 : 1;
}
