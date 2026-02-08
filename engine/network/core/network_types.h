#pragma once

#include "engine/core/types.h"
#include <string>
#include <vector>
#include <functional>
#include <cstring>

namespace nge::network {

// ─── Network Constants ───────────────────────────────────────────────────
inline constexpr u32 MAX_PACKET_SIZE    = 1200;  // MTU-safe
inline constexpr u32 MAX_CLIENTS        = 64;
inline constexpr u32 PROTOCOL_ID        = 0x4E474500; // "NGE\0"
inline constexpr u32 PROTOCOL_VERSION   = 1;
inline constexpr u16 DEFAULT_PORT       = 27015;
inline constexpr f32 CONNECTION_TIMEOUT = 10.0f;
inline constexpr f32 HEARTBEAT_INTERVAL = 1.0f;

// ─── Network Address ─────────────────────────────────────────────────────
struct NetAddress {
    u32  ip   = 0;         // IPv4 in host byte order
    u16  port = 0;

    bool operator==(const NetAddress& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const NetAddress& o) const { return !(*this == o); }

    std::string ToString() const {
        return std::to_string((ip >> 24) & 0xFF) + "." +
               std::to_string((ip >> 16) & 0xFF) + "." +
               std::to_string((ip >> 8) & 0xFF) + "." +
               std::to_string(ip & 0xFF) + ":" +
               std::to_string(port);
    }

    static NetAddress FromString(const std::string& str);
    static NetAddress Loopback(u16 port = DEFAULT_PORT) { return {0x7F000001, port}; }
};

// ─── Packet Types ────────────────────────────────────────────────────────
enum class PacketType : u8 {
    ConnectionRequest   = 0,
    ConnectionAccept    = 1,
    ConnectionDeny      = 2,
    Disconnect          = 3,
    Heartbeat           = 4,
    Reliable            = 5,  // Reliable ordered
    Unreliable          = 6,  // Unreliable (state updates)
    Fragment            = 7,  // Large packet fragment
    Ack                 = 8,  // Acknowledgement
};

// ─── Packet Header ───────────────────────────────────────────────────────
struct PacketHeader {
    u32        protocolId  = PROTOCOL_ID;
    u8         version     = PROTOCOL_VERSION;
    PacketType type        = PacketType::Unreliable;
    u16        sequence    = 0;
    u16        ack         = 0;        // Last received sequence from peer
    u32        ackBitfield = 0;        // Bitfield for 32 packets before ack
    u16        payloadSize = 0;

    static constexpr usize SIZE = 16;
};

// ─── Connection State ────────────────────────────────────────────────────
enum class ConnectionState : u8 {
    Disconnected,
    Connecting,
    Connected,
    TimedOut,
};

// ─── Connection Info ─────────────────────────────────────────────────────
struct ConnectionInfo {
    NetAddress      address;
    ConnectionState state = ConnectionState::Disconnected;
    u32             clientId = UINT32_MAX;
    f32             roundTripTime  = 0;
    f32             packetLoss     = 0;
    f32             timeSinceLastRecv = 0;
    u16             localSequence  = 0;
    u16             remoteSequence = 0;
    u32             remoteAckBits  = 0;
};

// ─── Serialization Buffer ────────────────────────────────────────────────
class BitWriter {
public:
    BitWriter(u8* buffer, usize capacity)
        : m_buffer(buffer), m_capacity(capacity) {}

    void WriteU8(u8 val)   { Write(&val, 1); }
    void WriteU16(u16 val) { Write(&val, 2); }
    void WriteU32(u32 val) { Write(&val, 4); }
    void WriteU64(u64 val) { Write(&val, 8); }
    void WriteF32(f32 val) { Write(&val, 4); }
    void WriteString(const std::string& s) {
        u16 len = static_cast<u16>(s.size());
        WriteU16(len);
        Write(s.data(), len);
    }
    void Write(const void* data, usize bytes) {
        if (m_offset + bytes <= m_capacity) {
            std::memcpy(m_buffer + m_offset, data, bytes);
            m_offset += bytes;
        }
    }

    usize GetSize() const { return m_offset; }
    const u8* GetData() const { return m_buffer; }

private:
    u8*   m_buffer;
    usize m_capacity;
    usize m_offset = 0;
};

class BitReader {
public:
    BitReader(const u8* buffer, usize size)
        : m_buffer(buffer), m_size(size) {}

    u8  ReadU8()  { u8  v = 0; Read(&v, 1); return v; }
    u16 ReadU16() { u16 v = 0; Read(&v, 2); return v; }
    u32 ReadU32() { u32 v = 0; Read(&v, 4); return v; }
    u64 ReadU64() { u64 v = 0; Read(&v, 8); return v; }
    f32 ReadF32() { f32 v = 0; Read(&v, 4); return v; }
    std::string ReadString() {
        u16 len = ReadU16();
        if (m_offset + len > m_size) return "";
        std::string s(reinterpret_cast<const char*>(m_buffer + m_offset), len);
        m_offset += len;
        return s;
    }
    void Read(void* out, usize bytes) {
        if (m_offset + bytes <= m_size) {
            std::memcpy(out, m_buffer + m_offset, bytes);
            m_offset += bytes;
        }
    }

    bool IsValid() const { return m_offset <= m_size; }
    usize GetBytesRead() const { return m_offset; }
    usize GetBytesRemaining() const { return m_size > m_offset ? m_size - m_offset : 0; }

private:
    const u8* m_buffer;
    usize     m_size;
    usize     m_offset = 0;
};

// ─── Replication Channel ID ──────────────────────────────────────────────
using ChannelId = u16;

// ─── Network Event Callback ──────────────────────────────────────────────
using OnConnectCallback    = std::function<void(u32 clientId)>;
using OnDisconnectCallback = std::function<void(u32 clientId)>;
using OnReceiveCallback    = std::function<void(u32 clientId, ChannelId channel, const u8* data, usize size)>;

} // namespace nge::network
