#pragma once

// Bounded IMA ADPCM player core: 128 B ring plus decoder/cursor state, no heap.
// The ISR is the only nibble consumer; the foreground is the only byte
// producer. One vacant ring slot keeps empty/full unambiguous.
//
// Extracted from sub-hauler's src/audio/sampled_player.hpp. No license grant
// is implied; see NOTICE.md.

#include <stdint.h>

#include "ima_adpcm.hpp"
#include "uint24.hpp"

#if defined(__AVR__)
#include <avr/interrupt.h>
#define STREAMFX_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define STREAMFX_BARRIER() ((void)0)
#endif

namespace streamfx {

constexpr uint8_t kRingBytes = 128;
constexpr uint8_t kFlagActive = 0x01;
constexpr uint8_t kFlagMuted = 0x02;
constexpr uint8_t kFlagPaused = 0x04;
constexpr uint8_t kIdShift = 4;
constexpr uint8_t kIdMask = 0x07;

class AtomicSection {
#if defined(__AVR__)
  public:
    AtomicSection() : sreg_(SREG) { cli(); }
    ~AtomicSection() { SREG = sreg_; }

  private:
    uint8_t sreg_;
#endif
};

class SampledPlayer {
  public:
    constexpr SampledPlayer()
        : ring_(), predictor_(0), loopPredictor_(0), stepIndex_(0),
          loopIndex_(0), readPos_(0), writePos_(0), samplesLeft_(0),
          loopSamples_(0), fxCursor_(0), fxBytesLeft_(0), fxBase_(0), flags_(0) {}

    // Resets state and selects a stream id without enabling output; enable()
    // arms it after the caller pre-fills the ring. Starting the already-active
    // id is a no-op.
    void start(uint8_t id, uint24_t base, uint24_t samples, int16_t predictor,
               uint8_t index) {
        if ((flags_ & kFlagActive) && ((flags_ >> kIdShift) & kIdMask) == (id & kIdMask))
            return;
        const uint8_t muted = flags_ & kFlagMuted;
        flags_ = static_cast<uint8_t>(muted | ((id & kIdMask) << kIdShift));
        predictor_ = predictor;
        loopPredictor_ = predictor;
        stepIndex_ = index > ima::kMaxIndex ? ima::kMaxIndex : index;
        loopIndex_ = stepIndex_;
        samplesLeft_ = samples;
        loopSamples_ = samples;
        fxBase_ = base;
        fxCursor_ = base;
        fxBytesLeft_ = samples >> 1;
        readPos_ = 0;
        writePos_ = 0;
    }

    void enable() { flags_ |= kFlagActive; }

    void stop() {
        AtomicSection lock;
        (void)lock;
        flags_ &= kFlagMuted;
        readPos_ = 0;
        writePos_ = 0;
        samplesLeft_ = 0;
    }

    void pause() {
        AtomicSection lock;
        (void)lock;
        if (flags_ & kFlagActive)
            flags_ |= kFlagPaused;
    }

    void resume() {
        AtomicSection lock;
        (void)lock;
        flags_ &= static_cast<uint8_t>(~kFlagPaused);
    }

    void setMuted(bool muted) {
        AtomicSection lock;
        (void)lock;
        if (muted)
            flags_ |= kFlagMuted;
        else
            flags_ &= static_cast<uint8_t>(~kFlagMuted);
    }

    bool active() const { return (flags_ & kFlagActive) != 0; }
    bool paused() const { return (flags_ & kFlagPaused) != 0; }
    bool muted() const { return (flags_ & kFlagMuted) != 0; }
    uint8_t streamId() const { return (flags_ >> kIdShift) & kIdMask; }

    uint8_t freeBytes() const {
        const uint8_t readByte = static_cast<uint8_t>(readPos_ >> 1);
        const uint8_t write = writePos_;
        return static_cast<uint8_t>((readByte - write - 1) & 0x7F);
    }

    uint8_t contiguousFree() const {
        const uint8_t free = freeBytes();
        if (free == 0)
            return 0;
        const uint8_t contiguous = static_cast<uint8_t>(kRingBytes - writePos_);
        return free < contiguous ? free : contiguous;
    }

    uint8_t *writePtr() { return &ring_[writePos_]; }
    uint24_t nextAddress() const { return fxCursor_; }
    uint24_t sourceBytesLeft() const { return fxBytesLeft_; }

    void commit(uint8_t count) {
        const uint8_t write = static_cast<uint8_t>((writePos_ + count) & 0x7F);
        STREAMFX_BARRIER();
        writePos_ = write;
    }

    void advance(uint8_t count) {
        fxCursor_ += count;
        fxBytesLeft_ -= count;
    }

    void wrapSource() {
        if (fxBytesLeft_ == 0) {
            fxCursor_ = fxBase_;
            fxBytesLeft_ = loopSamples_ >> 1;
        }
    }

    bool pushByte(uint8_t value) {
        if (freeBytes() == 0)
            return false;
        ring_[writePos_] = value;
        commit(1);
        return true;
    }

    // Timer-consumer step: SRAM only. Pause and mute return 0 without
    // advancing; an empty ring returns silence.
    int16_t tick() {
        const uint8_t flags = flags_;
        if (!(flags & kFlagActive) || (flags & (kFlagPaused | kFlagMuted)))
            return 0;
        STREAMFX_BARRIER();
        const uint8_t write = writePos_;
        STREAMFX_BARRIER();
        if (static_cast<uint8_t>(readPos_ >> 1) == write && (readPos_ & 1) == 0)
            return 0;
        const uint8_t byte = ring_[readPos_ >> 1];
        const uint8_t nibble = (readPos_ & 1) ? static_cast<uint8_t>(byte >> 4)
                                              : static_cast<uint8_t>(byte & 0x0F);
        readPos_ = static_cast<uint8_t>(readPos_ + 1);
        ima::State state{predictor_, stepIndex_};
        const int16_t sample = ima::decode(nibble, state);
        predictor_ = state.predictor;
        stepIndex_ = state.index;
        if (--samplesLeft_ == 0) {
            predictor_ = loopPredictor_;
            stepIndex_ = loopIndex_;
            samplesLeft_ = loopSamples_;
        }
        return sample;
    }

  private:
    uint8_t ring_[kRingBytes];
    int16_t predictor_;
    int16_t loopPredictor_;
    uint8_t stepIndex_;
    uint8_t loopIndex_;
    volatile uint8_t readPos_;
    volatile uint8_t writePos_;
    uint24_t samplesLeft_;
    uint24_t loopSamples_;
    uint24_t fxCursor_;
    uint24_t fxBytesLeft_;
    uint24_t fxBase_;
    volatile uint8_t flags_;
};

#undef STREAMFX_BARRIER

} // namespace streamfx
