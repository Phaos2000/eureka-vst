#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "PixelGfx.h"
#include "PluginProcessor.h"
#include "PresetManager.h"

namespace ambika_plugin
{
    // The hardware faceplate: everything is drawn pixel by pixel into a
    // 640x400 canvas of 16 colours (PixelGfx) and shown at 1x, 2x or 3x with
    // nearest-neighbour scaling. Knobs drag vertically; the character display
    // shows the section being edited, as the firmware's editor does.
    class AmbikaAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
    {
    public:
        explicit AmbikaAudioProcessorEditor (AmbikaAudioProcessor&);
        ~AmbikaAudioProcessorEditor() override;

        static constexpr int canvasWidth = 640;
        static constexpr int canvasHeight = 400;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;

        // For tools and tests.
        void showPage (int index);       // 0 synth, 1 mod, 2 multi, 3 presets
        void showMatrixPage (int page);  // 0 slots 1-8, 1 slots 9-16
        void editPart (int part);
        void showSaveDialog();
        // Zoom in half steps: 2 = 1x, 3 = 1.5x, 4 = 2x, 6 = 3x.
        void setZoom (int halves);
        int zoom() const { return zoomHalves; }
        int scaled (int canvasPixels) const { return canvasPixels * zoomHalves / 2; }
        // Section header look: see Canvas::headerBand for the styles.
        void setHeaderStyle (int style);
        // Logo treatment for the red and silver header styles: 0 letters on a
        // panel in the header colour, 1 letters in the header colour on black.
        void setLogoVariant (int variant);
        // Touches a control as if it had been clicked (the display follows it);
        // engine id for part parameters, host id otherwise.
        void focusParameter (const juce::String& id, bool asEdit = false);  // asEdit: show the detailed line
        void selectSlot (int slot);  // modulation slot 0..15
        // Draws the current state at 1x and returns it.
        const pixel::Canvas& renderCanvas();

    private:
        // A knob drawn this frame.
        struct Knob
        {
            juce::RangedAudioParameter* param = nullptr;
            juce::String label, shortName, longName, section;
            int maxIndex = -1;  // limits the range (filter variants)
            bool enabled = true;
        };

        // A clickable area: a knob, or anything with a click handler.
        struct Hit
        {
            juce::Rectangle<int> area;
            int knob = -1;  // index into knobs
            std::function<void()> onClick;
            std::function<void()> onDoubleClick;
            std::function<void (int)> onWheel;
        };

        enum class Dialog { None, Save, Rename, Init };

        void timerCallback() override;
        void refresh();
        void render();
        void renderOnce();
        void buildPlate();
        const Hit* hitAt (juce::Point<int> p) const;
        juce::Point<int> toCanvas (juce::Point<int> p) const { return { p.x * 2 / zoomHalves, p.y * 2 / zoomHalves }; }
        void addHit (juce::Rectangle<int> area, std::function<void()> onClick);
        void button (int x, int y, int w, const juce::String& label, bool on, std::function<void()> onClick, bool withLed = true);

        // Parameters.
        juce::RangedAudioParameter* parameter (const juce::String& id) const;
        juce::RangedAudioParameter* partParameter (const juce::String& engineId) const;
        int engineValue (const juce::String& engineId) const;
        int rawInt (const juce::String& id) const;
        static int stepCount (const Knob& k);
        static int indexOf (const Knob& k);
        static void setIndex (const Knob& k, int index);
        juce::String valueText (const juce::RangedAudioParameter* p) const;
        static juce::String engineIdOf (const juce::String& parameterId);
        void edited();

        // Drawing.
        void drawHeader();
        void drawDisplay();
        void drawSynthPage();
        void drawModPage();
        void drawMultiPage();
        void drawPresetsPage();
        void drawDialog();
        void drawInitPopup();
        void drawScope (int x, int y, int w, int h, juce::String& caption);
        void sectionBand (int x, int y, int w, const juce::String& title, const juce::String& right = {});
        // Faint outline in the header colour around a section occupying rows y to y + h - 1.
        // It runs one pixel outside the section at the top and sides and replaces the
        // section's last row (the black line under its bottom band), so call it after the bands.
        void sectionFrame (int x, int y, int w, int h);
        void knob (int cx, int cy, int r, int labelBandY, juce::RangedAudioParameter* p, const juce::String& label,
                   const juce::String& shortName, const juce::String& section, bool enabled = true, int maxIndex = -1);
        void textField (int x, int y, int w, const juce::String& label, int field);
        void displayLines (juce::String& line0, juce::String& line1) const;
        bool focusVisible() const;
        void notify (const juce::String& message, bool isError = false);
        static juce::StringArray wrap (const juce::String& text, int columns);

        // Presets.
        void refreshLibrary();
        void applyFilter();
        juce::StringArray libraryTags() const;
        const PresetInfo* selectedPreset() const;
        void selectRow (int row);
        void loadSelected();
        void stepPreset (int direction);
        void quickSave();
        void openRename();
        void deleteSelected();
        void loadAmbikaFile();
        void importToLibrary();
        void confirmDialog();
        void closeDialog();
        bool dialogClash() const;
        static bool editText (juce::String& text, const juce::KeyPress& key, int maxLength);

        AmbikaAudioProcessor& audioProcessor;
        juce::AudioProcessorValueTreeState& state;
        PresetManager& presets;

        pixel::Canvas canvas { canvasWidth, canvasHeight };
        pixel::Canvas plateCache { canvasWidth, canvasHeight };
        std::vector<uint8_t> shownPixels;
        juce::Image image;
        int zoomHalves = 4;
        int headerStyle = 5;
        int logoVariant = 1;
        int page = 0;
        int matrixPage = 0;
        int selectedSlot = 0;
        std::vector<Hit> hits;
        std::vector<Knob> knobs;
        size_t firstPageKnob = 0;

        // Control last touched: its label prints in reverse, the display shows it.
        Knob focus;
        juce::uint32 lastEditTime = 0;

        // Dragging.
        juce::RangedAudioParameter* dragParam = nullptr;
        Knob dragKnob;
        int dragStartIndex = 0;
        int dragStartY = 0;

        // Message in the program window for a moment.
        juce::String toast;
        bool toastIsError = false;
        juce::uint32 toastTime = 0;

        // Filter response of the Surge models (measured, so cached).
        std::array<int, 5> surgeKey { -1, -1, -1, -1, -1 };
        std::vector<double> surgeCurve;

        // Preset library.
        std::vector<PresetInfo> library, filtered;
        juce::String search;
        juce::StringArray activeTags;
        int selectedRow = -1;
        int scrollRow = 0;
        bool deleteArmed = false, armedAtClick = false;
        std::unique_ptr<juce::FileChooser> chooser;

        // Save / rename dialog, INIT popup.
        Dialog dialog = Dialog::None;
        juce::File renaming;
        juce::String fieldText[4];  // name, extra tags, author, comment
        juce::StringArray dialogTags;
        int focusedField = 0;
        juce::String dialogError;

        JUCE_DECLARE_WEAK_REFERENCEABLE (AmbikaAudioProcessorEditor)
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmbikaAudioProcessorEditor)
    };
}
