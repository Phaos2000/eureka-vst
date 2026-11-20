#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "PluginParameters.h"
#include "PresetManager.h"
#include "engine.h"

namespace ambika_plugin
{
    class AmbikaAudioProcessor final : public juce::AudioProcessor
    {
    public:
        AmbikaAudioProcessor();
        ~AmbikaAudioProcessor() override = default;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override {}
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState& parameters() { return state; }
        PresetManager& presets() { return presetManager; }

        // For the editor: activity (VCA level 0..255) and owner part of each voice.
        uint8_t voiceLevel (int voice) const { return voiceLevels[(size_t) voice].load (std::memory_order_relaxed); }
        uint8_t voiceOwner (int voice) const { return voiceOwners[(size_t) voice].load (std::memory_order_relaxed); }

        // The part shown in the editor; saved with the project.
        int editedPart() const { return editedPartIndex.load(); }
        void setEditedPart (int part) { editedPartIndex = juce::jlimit (0, kNumParts - 1, part); }
        // Editor pixel zoom (1, 2 or 3); saved with the project.
        int editorZoom() const { return editorZoomFactor.load(); }
        void setEditorZoom (int zoom) { editorZoomFactor = juce::jlimit (1, 3, zoom); }

    private:
        void applyParameters();
        void handleMidi (const juce::MidiMessage& message);

        juce::AudioProcessorValueTreeState state;
        PresetManager presetManager { state };
        // Engine holds ~20 KB of voice state; keep it off the object.
        std::unique_ptr<ambika::plugin::Engine> engine;

        // Engine parameters per part, in engine table order.
        std::array<std::vector<std::atomic<float>*>, kNumParts> rawParameters;
        std::array<std::vector<int>, kNumParts> appliedValues;
        std::array<std::atomic<float>*, kNumVoices> rawVoiceParts {};
        std::array<int, kNumVoices> appliedVoiceParts {};
        std::atomic<float>* outputGain = nullptr;
        std::array<std::array<std::atomic<float>*, 2>, kNumParts> rawKeyRange {};
        std::array<std::array<int, 2>, kNumParts> appliedKeyRange {};
        std::atomic<float>* voiceMode = nullptr;
        std::atomic<float>* unison = nullptr;
        int appliedVoiceMode = -1, appliedUnison = -1;
        juce::SmoothedValue<float> gainSmoothed;

        std::vector<float> left, right;
        std::array<std::atomic<uint8_t>, kNumVoices> voiceLevels {};
        std::array<std::atomic<uint8_t>, kNumVoices> voiceOwners {};
        std::atomic<int> editedPartIndex { 0 };
        std::atomic<int> editorZoomFactor { 2 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmbikaAudioProcessor)
    };
}
