#include "Exporter.h"
#include "PluginProcessor.h"

namespace Exporter
{
juce::File uniqueFile (const juce::File& folder, const juce::String& prefix, const juce::String& ext)
{
    folder.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
    auto f = folder.getChildFile (prefix + "_" + stamp + ext);
    int n = 2;
    while (f.exists())
        f = folder.getChildFile (prefix + "_" + stamp + "_" + juce::String (n++) + ext);
    return f;
}

bool writeWav (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                           .withNumChannels (audio.getNumChannels())
                                                                           .withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

namespace
{
    void finish (Done done, juce::Array<juce::File> files, juce::String msg)
    {
        juce::MessageManager::callAsync ([done, files, msg] { if (done) done (files, msg); });
    }

    juce::String cleanName (juce::String s)
    {
        s = s.replaceCharacters (" /\\\\:*?\"<>|", "___________");
        return s.isEmpty() ? juce::String ("sample") : s;
    }
}

//==============================================================================
void renderPattern (IDGemsSlicerProcessor& src, int bars, bool audio, bool midi, Done done)
{
    // Copie du plugin preparee sur le thread message, rendu sur un thread separe
    std::shared_ptr<IDGemsSlicerProcessor> off (new IDGemsSlicerProcessor(),
        [] (IDGemsSlicerProcessor* p)
        {
            // destruction sur le thread message (minuteries)
            juce::MessageManager::callAsync ([p] { delete p; });
        });
    off->prepareOfflineCopy (src);

    const double sr = src.getCurrentSampleRate() > 0 ? src.getCurrentSampleRate() : 48000.0;
    const double bpm = src.getHostBpm() > 20.0 ? src.getHostBpm() : 120.0;
    const int note = (int) src.getAPVTS().getRawParameterValue ("rootNote")->load();
    const auto folder = src.getExportFolder();
    bars = juce::jlimit (1, 128, bars);

    juce::Thread::launch ([off, sr, bpm, note, folder, bars, audio, midi, done]
    {
        const int block = 512;
        const auto total = (juce::int64) std::llround (bars * 4.0 * 60.0 / bpm * sr);
        off->setPlayConfigDetails (0, 2, sr, block);
        off->prepareToPlay (sr, block);
        off->setOfflineBpm (bpm);
        off->enableTriggerLog (16384);

        juce::AudioBuffer<float> out (2, (int) total);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer on, none;
        on.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);

        for (juce::int64 pos = 0; pos < total; pos += block)
        {
            const int n = (int) juce::jmin ((juce::int64) block, total - pos);
            juce::AudioBuffer<float> part (buf.getArrayOfWritePointers(), 2, 0, n);
            off->processBlock (part, pos == 0 ? on : none);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, (int) pos, part, ch, 0, n);
        }

        juce::Array<juce::File> files;
        juce::String msg;

        if (audio)
        {
            auto f = uniqueFile (folder, "IDGems_Pattern_" + juce::String (bars) + "mes", ".wav");
            if (writeWav (out, sr, f)) files.add (f);
            else msg << "Ecriture WAV impossible. ";
        }

        if (midi)
        {
            // Une note par slice jouee : note = racine + numero de slice (Mode Clavier)
            constexpr int ppq = 960;
            const double ticksPerSample = bpm / 60.0 / sr * ppq;
            juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm)), 0.0);
            seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
            seq.addEvent (juce::MidiMessage::textMetaEvent (3, "IDGems Slicer"), 0.0);
            for (auto& t : off->getTriggerLog())
            {
                if (t.time >= total) continue;
                const int nn = juce::jlimit (0, 127, note + t.slice);
                const auto vel = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (t.velocity * 127.0f));
                const double start = (double) t.time * ticksPerSample;
                const double end = (double) juce::jmin (total, t.time + t.lengthSamples) * ticksPerSample;
                seq.addEvent (juce::MidiMessage::noteOn (1, nn, vel), start);
                seq.addEvent (juce::MidiMessage::noteOff (1, nn), juce::jmax (start + 1.0, end));
            }
            seq.updateMatchedPairs();
            juce::MidiFile mf;
            mf.setTicksPerQuarterNote (ppq);
            mf.addTrack (seq);
            auto f = uniqueFile (folder, "IDGems_Pattern_" + juce::String (bars) + "mes", ".mid");
            juce::FileOutputStream fos (f);
            if (fos.openedOk() && mf.writeTo (fos)) files.add (f);
            else msg << "Ecriture MIDI impossible. ";
        }

        if (msg.isEmpty())
            msg = juce::String (bars) + " mesure(s) a " + juce::String (bpm, 1) + " BPM exportee(s) ("
                + juce::String ((int) off->getTriggerLog().size()) + " slices jouees)";
        finish (done, files, msg);
    });
}

//==============================================================================
void exportSlices (IDGemsSlicerProcessor& src, Done done)
{
    std::array<idg::SampleData::Ptr, idg::numSlots> datas;
    for (int i = 0; i < idg::numSlots; ++i) datas[(size_t) i] = src.getSampleManager().getSlot (i).data;
    const auto folder = src.getExportFolder().getChildFile ("Slices");
    const float fadeMs = src.getAPVTS().getRawParameterValue ("fadeMs")->load();
    const bool normalise = src.getAPVTS().getRawParameterValue ("normalize")->load() > 0.5f;

    juce::Thread::launch ([datas, folder, fadeMs, normalise, done]
    {
        juce::Array<juce::File> files;
        int count = 0;
        for (int s = 0; s < idg::numSlots; ++s)
        {
            const auto& d = datas[(size_t) s];
            if (d == nullptr) continue;
            const auto name = cleanName (d->audio->name);
            const auto dir = uniqueFile (folder, "S" + juce::String (s + 1) + "_" + name, "");
            dir.createDirectory();
            files.add (dir);

            for (int i = 0; i < d->numSlices(); ++i)
            {
                const auto& sl = d->slices[(size_t) i];
                const int len = sl.end - sl.start;
                if (len <= 0) continue;
                juce::AudioBuffer<float> b (2, len);
                for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, 0, d->audio->buffer, ch, sl.start, len);
                if (normalise) b.applyGain (sl.normGain);
                const int fade = juce::jmin (len / 2, (int) (fadeMs * 0.001 * d->audio->sampleRate));
                if (fade > 1)
                {
                    b.applyGainRamp (0, fade, 0.0f, 1.0f);
                    b.applyGainRamp (len - fade, fade, 1.0f, 0.0f);
                }
                const auto f = dir.getChildFile (name + "_" + juce::String (i + 1).paddedLeft ('0', 3) + ".wav");
                if (writeWav (b, d->audio->sampleRate, f)) ++count;
            }
        }
        finish (done, files, count > 0 ? juce::String (count) + " slices exportees en one-shots"
                                       : juce::String ("Aucun sample charge"));
    });
}

//==============================================================================
void saveRecording (juce::AudioBuffer<float> audio, double sr, juce::File folder, Done done)
{
    juce::Thread::launch ([audio = std::move (audio), sr, folder, done]
    {
        auto f = uniqueFile (folder.getChildFile ("Resamples"), "IDGems_Resample", ".wav");
        juce::Array<juce::File> files;
        if (writeWav (audio, sr, f)) files.add (f);
        finish (done, files, files.isEmpty() ? juce::String ("Ecriture impossible : ") + f.getFullPathName()
                                             : juce::String (audio.getNumSamples() / sr, 1) + " s enregistrees");
    });
}
}
