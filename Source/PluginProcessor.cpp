#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    namespace IDs
    {
        const juce::Identifier editorWidth  { "editorWidth" };
        const juce::Identifier editorHeight { "editorHeight" };
        const juce::Identifier stateVersion { "stateVersion" };
    }

    constexpr int currentStateVersion = 1;

    //==========================================================================
    // Synthe sinus de test (ETAPE 1 uniquement) : prouve que MIDI in -> audio out.
    struct TestSound final : juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    struct TestVoice final : juce::SynthesiserVoice
    {
        bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<TestSound*> (s) != nullptr; }

        void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
        {
            adsr.setSampleRate (getSampleRate());
            adsr.setParameters ({ 0.005f, 0.15f, 0.6f, 0.25f });
            level = velocity * 0.2f;
            phase = 0.0;
            delta = juce::MathConstants<double>::twoPi
                    * juce::MidiMessage::getMidiNoteInHertz (note) / getSampleRate();
            adsr.noteOn();
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff) adsr.noteOff();
            else { adsr.reset(); clearCurrentNote(); }
        }

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (! isVoiceActive()) return;

            for (int i = 0; i < num; ++i)
            {
                const float env = adsr.getNextSample();
                const float s = (float) std::sin (phase) * level * env;
                phase += delta;
                if (phase > juce::MathConstants<double>::twoPi)
                    phase -= juce::MathConstants<double>::twoPi;

                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addSample (ch, start + i, s);

                if (! adsr.isActive()) { clearCurrentNote(); break; }
            }
        }

        juce::ADSR adsr;
        double phase = 0.0, delta = 0.0;
        float level = 0.0f;
    };
}

//==============================================================================
IDGemsSlicerProcessor::IDGemsSlicerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "IDGemsSlicerState", createParameterLayout())
{
    outputGainDb = apvts.getRawParameterValue ("outputGain");

    for (int i = 0; i < 8; ++i)
        testSynth.addVoice (new TestVoice());
    testSynth.addSound (new TestSound());

    apvts.state.setProperty (IDs::editorWidth,  baseWidth,  nullptr);
    apvts.state.setProperty (IDs::editorHeight, baseHeight, nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout IDGemsSlicerProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "outputGain", 1 }, "Output Gain",
        juce::NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}

//==============================================================================
void IDGemsSlicerProcessor::prepareToPlay (double sampleRate, int)
{
    testSynth.setCurrentPlaybackSampleRate (sampleRate);
    outputGain.reset (sampleRate, 0.05);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load()));
}

bool IDGemsSlicerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void IDGemsSlicerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();

    for (const auto meta : midi)
        if (meta.getMessage().isNoteOn())
            midiActivityCounter.fetch_add (1, std::memory_order_relaxed);

    testSynth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load()));
    outputGain.applyGain (buffer, buffer.getNumSamples());

    for (int ch = 0; ch < 2; ++ch)
    {
        const float peak = ch < buffer.getNumChannels() ? buffer.getMagnitude (ch, 0, buffer.getNumSamples()) : 0.0f;
        const float prev = outputLevel[(size_t) ch].load (std::memory_order_relaxed);
        outputLevel[(size_t) ch].store (juce::jmax (peak, prev * 0.9f), std::memory_order_relaxed);
    }
}

//==============================================================================
juce::AudioProcessorEditor* IDGemsSlicerProcessor::createEditor()
{
    return new IDGemsSlicerEditor (*this);
}

juce::Point<int> IDGemsSlicerProcessor::getSavedEditorSize() const
{
    return { (int) apvts.state.getProperty (IDs::editorWidth,  baseWidth),
             (int) apvts.state.getProperty (IDs::editorHeight, baseHeight) };
}

void IDGemsSlicerProcessor::setSavedEditorSize (int w, int h)
{
    apvts.state.setProperty (IDs::editorWidth,  w, nullptr);
    apvts.state.setProperty (IDs::editorHeight, h, nullptr);
}

//==============================================================================
void IDGemsSlicerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (IDs::stateVersion, currentStateVersion, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void IDGemsSlicerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // Si l'editeur est ouvert, il relit la taille sauvegardee
    if (auto* ed = dynamic_cast<IDGemsSlicerEditor*> (getActiveEditor()))
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<IDGemsSlicerEditor> (ed)]
        {
            if (safe != nullptr) safe->restoreSavedSize();
        });
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IDGemsSlicerProcessor();
}
