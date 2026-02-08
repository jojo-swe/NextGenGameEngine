#pragma once

#include "engine/network/core/network_types.h"
#include "engine/network/core/net_socket.h"
#include <array>
#include <mutex>

namespace nge::network {

// ─── Network Server ──────────────────────────────────────────────────────
// Authoritative game server. Manages client connections over UDP.
// Handles connection handshake, heartbeat, reliable delivery, and replication.

class NetServer {
public:
    NetServer() = default;
    ~NetServer();

    bool Start(u16 port = DEFAULT_PORT);
    void Stop();
    bool IsRunning() const { return m_running; }

    // Call once per tick (e.g., 60 Hz)
    void Update(f32 deltaTime);

    // Send data to a specific client
    void Send(u32 clientId, ChannelId channel, const void* data, usize size, bool reliable = false);

    // Broadcast to all connected clients
    void Broadcast(ChannelId channel, const void* data, usize size, bool reliable = false);

    // Kick a client
    void Disconnect(u32 clientId);

    // Callbacks
    void SetOnConnect(OnConnectCallback cb)       { m_onConnect = std::move(cb); }
    void SetOnDisconnect(OnDisconnectCallback cb)  { m_onDisconnect = std::move(cb); }
    void SetOnReceive(OnReceiveCallback cb)        { m_onReceive = std::move(cb); }

    // Info
    u32 GetConnectedClientCount() const;
    const ConnectionInfo* GetClientInfo(u32 clientId) const;

private:
    void ProcessPacket(const NetAddress& from, const u8* data, usize size);
    void HandleConnectionRequest(const NetAddress& from, BitReader& reader);
    void HandleDisconnect(u32 clientId);
    void HandleReliable(u32 clientId, BitReader& reader);
    void HandleUnreliable(u32 clientId, BitReader& reader);
    void HandleAck(u32 clientId, BitReader& reader);
    void SendPacket(const NetAddress& to, PacketType type, const void* payload, usize payloadSize, u16 sequence = 0);
    void UpdateHeartbeats(f32 deltaTime);
    void CheckTimeouts(f32 deltaTime);

    i32 FindClientByAddress(const NetAddress& addr) const;
    i32 AllocateClientSlot();

    UDPSocket m_socket;
    bool      m_running = false;

    struct ClientSlot {
        ConnectionInfo info;
        bool           active = false;
        f32            heartbeatTimer = 0;

        // Reliable delivery
        struct PendingReliable {
            u16                sequence;
            std::vector<u8>    data;
            f32                timeSent;
            u32                retries;
        };
        std::vector<PendingReliable> pendingReliables;
    };

    std::array<ClientSlot, MAX_CLIENTS> m_clients{};
    std::mutex m_mutex;

    OnConnectCallback    m_onConnect;
    OnDisconnectCallback m_onDisconnect;
    OnReceiveCallback    m_onReceive;

    u8 m_recvBuffer[MAX_PACKET_SIZE]{};
};

} // namespace nge::network
