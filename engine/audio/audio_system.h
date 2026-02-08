#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace nge::audio {

// ─── Audio Handles ───────────────────────────────────────────────────────

using SoundId  = u32;
using SourceId = u32;
using BusId    = u8;

inline constexpr SoundId  INVALID_SOUND  = UINT32_MAX;
inline constexpr SourceId INVALID_SOURCE = UINT32_MAX;

// ─── Audio Format ────────────────────────────────────────────────────────

enum class AudioFormat : u8 {
    Mono8,
    Mono16,
    MonoF32,
    Stereo8,
    Stereo16,
    StereoF32,
};

// ─── Sound Data ──────────────────────────────────────────────────────────

struct SoundDesc {
    std::string    name;
    AudioFormat    format     = AudioFormat::Mono16;
    u32            sampleRate = 44100;
    u32            channels   = 1;
    const void*    data       = nullptr;
    usize          dataSize   = 0;
    bool           streaming  = false; // Stream from disk instead of loading fully
};

// ─── Source (playing instance) ───────────────────────────────────────────

struct SourceDesc {
    SoundId    sound     = INVALID_SOUND;
    math::Vec3 position  = {0, 0, 0};
    f32        volume    = 1.0f;
    f32        pitch     = 1.0f;
    f32        minDistance = 1.0f;   // Distance at which attenuation starts
    f32        maxDistance = 100.0f; // Distance at which sound is inaudible
    bool       looping   = false;
    bool       spatial   = true;    // 3D spatialized
    BusId      bus       = 0;       // Mixer bus (0 = master)
};

enum class SourceState : u8 {
    Stopped,
    Playing,
    Paused,
};

// ─── Mixer Bus ───────────────────────────────────────────────────────────

struct MixerBus {
    static constexpr u8 MAX_BUSES = 16;

    std::string name = "Master";
    f32         volume = 1.0f;
    bool        muted  = false;
    BusId       parent = 0; // Parent bus (0 = root)
};

// ─── Listener (camera/player position for 3D audio) ─────────────────────

struct AudioListener {
    math::Vec3 position  = {0, 0, 0};
    math::Vec3 forward   = {0, 0, -1};
    math::Vec3 up        = {0, 1, 0};
    math::Vec3 velocity  = {0, 0, 0};
};

// ─── Audio System ────────────────────────────────────────────────────────
// Wraps a low-level audio backend (miniaudio, FMOD, or custom).
// Provides 3D spatial audio, mixer buses, streaming, and DSP.
//
// When miniaudio is integrated via vcpkg, this implementation wraps ma_engine.

struct AudioConfig {
    u32  sampleRate     = 48000;
    u32  channels       = 2;       // Output channels
    u32  maxSources     = 256;     // Max simultaneous playing sources
    u32  maxSounds      = 1024;    // Max loaded sounds
    bool enableReverb   = true;
    bool enableHRTF     = false;   // Head-related transfer function for headphones
};

class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem();

    bool Init(const AudioConfig& config = {});
    void Shutdown();

    // Update (call once per frame — updates 3D positions, streams, etc.)
    void Update(f32 deltaTime);

    // Listener
    void SetListener(const AudioListener& listener);
    const AudioListener& GetListener() const { return m_listener; }

    // Sound loading
    SoundId LoadSound(const SoundDesc& desc);
    SoundId LoadSoundFromFile(const std::string& path, bool streaming = false);
    void    UnloadSound(SoundId id);

    // Source playback
    SourceId Play(const SourceDesc& desc);
    SourceId PlayOneShot(SoundId sound, const math::Vec3& position = {0,0,0}, f32 volume = 1.0f);
    void     Stop(SourceId id);
    void     Pause(SourceId id);
    void     Resume(SourceId id);
    void     StopAll();

    // Source properties
    void         SetSourcePosition(SourceId id, const math::Vec3& pos);
    void         SetSourceVolume(SourceId id, f32 volume);
    void         SetSourcePitch(SourceId id, f32 pitch);
    SourceState  GetSourceState(SourceId id) const;

    // Mixer
    void SetBusVolume(BusId bus, f32 volume);
    void SetBusMuted(BusId bus, bool muted);
    f32  GetBusVolume(BusId bus) const;

    // Global
    void SetMasterVolume(f32 volume);
    f32  GetMasterVolume() const { return m_masterVolume; }

    // Stats
    u32 GetActiveSourceCount() const;
    u32 GetLoadedSoundCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    AudioConfig   m_config;
    AudioListener m_listener;
    f32           m_masterVolume = 1.0f;
    MixerBus      m_buses[MixerBus::MAX_BUSES];
};

} // namespace nge::audio
