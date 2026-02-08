#include "engine/audio/audio_system.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <cmath>
#include <algorithm>

// miniaudio integration — when available via vcpkg:
// #define MA_IMPLEMENTATION
// #include <miniaudio.h>

// Stub implementation until miniaudio is available.
// Tracks source state and positions for testing without actual audio output.

namespace nge::audio {

struct AudioSystem::Impl {
    struct SoundEntry {
        bool        loaded = false;
        std::string name;
        AudioFormat format;
        u32         sampleRate;
        u32         channels;
        usize       dataSize;
        // In production: miniaudio decoder or buffer handle
    };

    struct SourceEntry {
        bool        active = false;
        SoundId     sound  = INVALID_SOUND;
        SourceState state  = SourceState::Stopped;
        math::Vec3  position;
        f32         volume = 1.0f;
        f32         pitch  = 1.0f;
        f32         minDist = 1.0f;
        f32         maxDist = 100.0f;
        bool        looping = false;
        bool        spatial = true;
        BusId       bus = 0;
        f32         playbackTime = 0; // seconds elapsed
    };

    std::vector<SoundEntry>  sounds;
    std::vector<SourceEntry> sources;
    std::vector<u32>         freeSoundSlots;
    std::vector<u32>         freeSourceSlots;
};

AudioSystem::~AudioSystem() {
    Shutdown();
}

bool AudioSystem::Init(const AudioConfig& config) {
    m_config = config;
    m_impl = std::make_unique<Impl>();
    m_impl->sounds.reserve(config.maxSounds);
    m_impl->sources.reserve(config.maxSources);

    // Initialize mixer buses
    m_buses[0].name = "Master";
    m_buses[0].volume = 1.0f;
    m_buses[1].name = "SFX";
    m_buses[1].parent = 0;
    m_buses[2].name = "Music";
    m_buses[2].parent = 0;
    m_buses[3].name = "Voice";
    m_buses[3].parent = 0;
    m_buses[4].name = "Ambient";
    m_buses[4].parent = 0;

    NGE_LOG_INFO("Audio system initialized (stub): {}Hz, {} channels, max {} sources",
                 config.sampleRate, config.channels, config.maxSources);
    return true;
}

void AudioSystem::Shutdown() {
    if (m_impl) {
        StopAll();
        m_impl.reset();
    }
}

void AudioSystem::Update(f32 deltaTime) {
    if (!m_impl) return;

    for (auto& src : m_impl->sources) {
        if (!src.active || src.state != SourceState::Playing) continue;
        src.playbackTime += deltaTime;

        // In production: update 3D spatialization, check if playback complete
        // For stub: just track time. Non-looping sounds "stop" after a fixed duration.
        if (!src.looping && src.playbackTime > 5.0f) {
            src.state = SourceState::Stopped;
            src.active = false;
            m_impl->freeSourceSlots.push_back(
                static_cast<u32>(&src - m_impl->sources.data()));
        }
    }
}

void AudioSystem::SetListener(const AudioListener& listener) {
    m_listener = listener;
}

SoundId AudioSystem::LoadSound(const SoundDesc& desc) {
    if (!m_impl) return INVALID_SOUND;

    SoundId id;
    if (!m_impl->freeSoundSlots.empty()) {
        id = m_impl->freeSoundSlots.back();
        m_impl->freeSoundSlots.pop_back();
    } else {
        id = static_cast<SoundId>(m_impl->sounds.size());
        m_impl->sounds.emplace_back();
    }

    auto& entry = m_impl->sounds[id];
    entry.loaded     = true;
    entry.name       = desc.name;
    entry.format     = desc.format;
    entry.sampleRate = desc.sampleRate;
    entry.channels   = desc.channels;
    entry.dataSize   = desc.dataSize;

    NGE_LOG_DEBUG("Loaded sound {}: '{}' ({}B)", id, desc.name, desc.dataSize);
    return id;
}

SoundId AudioSystem::LoadSoundFromFile(const std::string& path, bool /*streaming*/) {
    NGE_LOG_INFO("Loading sound from file: {}", path);
    // TODO: Use miniaudio decoder to load WAV/OGG/MP3/FLAC
    SoundDesc desc;
    desc.name = path;
    desc.sampleRate = 44100;
    desc.channels = 1;
    desc.dataSize = 0;
    return LoadSound(desc);
}

void AudioSystem::UnloadSound(SoundId id) {
    if (!m_impl || id >= m_impl->sounds.size() || !m_impl->sounds[id].loaded) return;
    m_impl->sounds[id].loaded = false;
    m_impl->freeSoundSlots.push_back(id);
}

SourceId AudioSystem::Play(const SourceDesc& desc) {
    if (!m_impl) return INVALID_SOURCE;
    if (desc.sound >= m_impl->sounds.size() || !m_impl->sounds[desc.sound].loaded) {
        NGE_LOG_WARN("Audio: tried to play invalid sound {}", desc.sound);
        return INVALID_SOURCE;
    }

    SourceId id;
    if (!m_impl->freeSourceSlots.empty()) {
        id = m_impl->freeSourceSlots.back();
        m_impl->freeSourceSlots.pop_back();
    } else {
        if (m_impl->sources.size() >= m_config.maxSources) {
            NGE_LOG_WARN("Audio: max sources reached ({})", m_config.maxSources);
            return INVALID_SOURCE;
        }
        id = static_cast<SourceId>(m_impl->sources.size());
        m_impl->sources.emplace_back();
    }

    auto& src = m_impl->sources[id];
    src.active       = true;
    src.sound        = desc.sound;
    src.state        = SourceState::Playing;
    src.position     = desc.position;
    src.volume       = desc.volume;
    src.pitch        = desc.pitch;
    src.minDist      = desc.minDistance;
    src.maxDist      = desc.maxDistance;
    src.looping      = desc.looping;
    src.spatial       = desc.spatial;
    src.bus          = desc.bus;
    src.playbackTime = 0;

    return id;
}

SourceId AudioSystem::PlayOneShot(SoundId sound, const math::Vec3& position, f32 volume) {
    SourceDesc desc;
    desc.sound = sound;
    desc.position = position;
    desc.volume = volume;
    desc.looping = false;
    return Play(desc);
}

void AudioSystem::Stop(SourceId id) {
    if (!m_impl || id >= m_impl->sources.size() || !m_impl->sources[id].active) return;
    m_impl->sources[id].state = SourceState::Stopped;
    m_impl->sources[id].active = false;
    m_impl->freeSourceSlots.push_back(id);
}

void AudioSystem::Pause(SourceId id) {
    if (!m_impl || id >= m_impl->sources.size() || !m_impl->sources[id].active) return;
    m_impl->sources[id].state = SourceState::Paused;
}

void AudioSystem::Resume(SourceId id) {
    if (!m_impl || id >= m_impl->sources.size() || !m_impl->sources[id].active) return;
    if (m_impl->sources[id].state == SourceState::Paused) {
        m_impl->sources[id].state = SourceState::Playing;
    }
}

void AudioSystem::StopAll() {
    if (!m_impl) return;
    for (u32 i = 0; i < static_cast<u32>(m_impl->sources.size()); ++i) {
        if (m_impl->sources[i].active) Stop(i);
    }
}

void AudioSystem::SetSourcePosition(SourceId id, const math::Vec3& pos) {
    if (!m_impl || id >= m_impl->sources.size() || !m_impl->sources[id].active) return;
    m_impl->sources[id].position = pos;
}

void AudioSystem::SetSourceVolume(SourceId id, f32 volume) {
    if (!m_impl || id >= m_impl->sources.size() || !m_impl->sources[id].active) return;
    m_impl->sources[id].volume = volume;
}

void AudioSystem::SetSourcePitch(SourceId id, f32 pitch) {
    if (!m_impl || id >= m_impl->sources.size() || !m_impl->sources[id].active) return;
    m_impl->sources[id].pitch = pitch;
}

SourceState AudioSystem::GetSourceState(SourceId id) const {
    if (!m_impl || id >= m_impl->sources.size() || !m_impl->sources[id].active)
        return SourceState::Stopped;
    return m_impl->sources[id].state;
}

void AudioSystem::SetBusVolume(BusId bus, f32 volume) {
    if (bus < MixerBus::MAX_BUSES) m_buses[bus].volume = volume;
}

void AudioSystem::SetBusMuted(BusId bus, bool muted) {
    if (bus < MixerBus::MAX_BUSES) m_buses[bus].muted = muted;
}

f32 AudioSystem::GetBusVolume(BusId bus) const {
    if (bus < MixerBus::MAX_BUSES) return m_buses[bus].volume;
    return 0;
}

void AudioSystem::SetMasterVolume(f32 volume) {
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
}

u32 AudioSystem::GetActiveSourceCount() const {
    if (!m_impl) return 0;
    u32 count = 0;
    for (const auto& src : m_impl->sources) {
        if (src.active && src.state == SourceState::Playing) count++;
    }
    return count;
}

u32 AudioSystem::GetLoadedSoundCount() const {
    if (!m_impl) return 0;
    u32 count = 0;
    for (const auto& snd : m_impl->sounds) {
        if (snd.loaded) count++;
    }
    return count;
}

} // namespace nge::audio
