#pragma once

#include "PolySynth/Core.h"
#include "PolySynth/MidiEvent.h"
#include "PolySynth/Patch.h"

#include <algorithm>
#include <memory>
#include <span>

namespace polysynth
{
class InstrumentVoice
{
  public:
    struct InstrumentVoiceContext
    {
        float tempo;
        float pitchbendRangeSemitones = 2.0f;
        std::optional<AudioBufferRef> sidechainBuffer = std::nullopt;

        bool operator==(const InstrumentVoiceContext& rhs) const { return tempo == rhs.tempo; }
        bool operator!=(const InstrumentVoiceContext& rhs) const { return !(rhs == *this); }
    };
    virtual ~InstrumentVoice() = default;

    virtual void onMidiEvent(const InstrumentVoiceContext& ctx, const MidiEvent& event) = 0;
    virtual void onParamChange(int paramId, Patch::optional_paramvalue_t newValue) = 0;

    /**
     * Render audio of a single voice.
     */
    virtual void renderVoiceAudio(const InstrumentVoiceContext& /*ctx*/,
                                  std::optional<AudioBufferStereoRef> /*inputBuffer*/,
                                  AudioBufferStereoRef /*outputBuffer*/)
    {
    }

    virtual void renderVoiceAudio(const InstrumentVoiceContext& /*ctx*/,
                                  std::optional<AudioBufferStereoRef> /*inputBuffer*/,
                                  AudioBufferStereoRef /*outputBuffer*/,
                                  std::span<AudioBufferRef> /*auxOutBuffer*/)
    {
    }

    /**
     * Apply post processing / fx on final audio - mixdown of all voices.
     * Called on one voice only, specifically on the first allocated voice of the instrument.
     */
    virtual void renderPostProcessAudio(const InstrumentVoiceContext& /*ctx*/,
                                        AudioBufferStereoRef /*outputBuffer*/){};

    virtual bool hasPostProcess() const { return false; }

    /**
     * Optional lightweight advance for idle voices (when Voice Sleep is Off and !isActive()),
     * allowing free-running modulation/LFO state to stay in sync without rendering audio.
     */
    virtual void tickIdle(const InstrumentVoiceContext& /*ctx*/) {}

    virtual bool hasTickIdle() const { return false; }

    virtual void renderGraph(std::span<float> points) { std::ranges::fill(points, 0); };

    /**
     * Does the voice produce sound of any note, including after releasing it.
     */
    virtual bool isActive() = 0;

    /**
     * Does the voice handle note, i.e. either it's playing it or it's held.
     * (held note means a note that has not been released while another was triggered).
     */
    virtual bool handlesNote(note_t note) = 0;

    /**
     * Does the voice play (triggered and not released) a note.
     */
    virtual bool handlesAnyNote() = 0;

    /**
     * Get note value of currently or previously played note.
     */
    virtual tiny_optional<note_t> getLastNote() = 0;

    /**
     * Get velocity of the note currently being played.
     */
    virtual tiny_optional<float> getCurrentNoteVelocity() = 0;

    /**
     * Remove any history of previously played notes (other than current/last played note),
     * effectively causing legato to not retrigger note on next note off.
     */
    virtual void clearPreviousNotes(){};

    virtual void reset(){};

    [[nodiscard]] Patch* getPatch() const { return patch.get(); };
    virtual void setPatch(std::shared_ptr<Patch> patch) { this->patch = std::move(patch); };

  private:
    std::shared_ptr<Patch> patch = nullptr;
};

}
