#pragma once

#include "engine/network/core/network_types.h"
#include "engine/network/core/net_socket.h"
#include <vector>

namespace nge::network {

// ─── Network Client ──────────────────────────────────────────────────────
// Connects to a NetServer over UDP. Handles connection handshake,
// heartbeat, reliable delivery, and state replication.

class NetClient {
public:
    NetClient() = default;
    ~NetClient();

    // Connect to server
    bool Connect(const NetAddress& serverAddress);
    void Disconnect();
    bool IsConnected() const { return m_state == ConnectionState::Connected; }
    ConnectionState GetState() const { return m_state; }

    // Call once per tick
    void Update(f32 deltaTime);

    // Send data to server
    void Send(ChannelId channel, const void* data, usize size, bool reliable = false);

    // Callbacks
    void SetOnConnect(OnConnectCallback cb)       { m_onConnect = std::move(cb); }
    void SetOnDisconnect(OnDisconnectCallback cb)  { m_onDisconnect = std::move(cb); }
    void SetOnReceive(OnReceiveCallback cb)        { m_onReceive = std::move(cb); }

    // Info
    u32 GetClientId() const { return m_clientId; }
    f32 GetRTT() const { return m_rtt; }
    f32 GetPacketLoss() const { return m_packetLoss; }
    const NetAddress& GetServerAddress() const { return m_serverAddress; }

private:
    void ProcessPacket(const u8* data, usize size);
    void SendPacket(PacketType type, const void* payload, usize payloadSize);
    void UpdateHeartbeat(f32 deltaTime);

    UDPSocket       m_socket;
    NetAddress      m_serverAddress;
    ConnectionState m_state = ConnectionState::Disconnected;
    u32             m_clientId = UINT32_MAX;

    u16 m_localSequence  = 0;
    u16 m_remoteSequence = 0;
    u32 m_remoteAckBits  = 0;

    f32 m_rtt            = 0;
    f32 m_packetLoss     = 0;
    f32 m_timeSinceLastRecv = 0;
    f32 m_heartbeatTimer = 0;
    f32 m_connectTimer   = 0;
    u32 m_connectRetries = 0;

    // Reliable delivery
    struct PendingReliable {
        u16             sequence;
        std::vector<u8> packet;
        f32             timeSent;
        u32             retries;
    };
    std::vector<PendingReliable> m_pendingReliables;

    OnConnectCallback    m_onConnect;
    OnDisconnectCallback m_onDisconnect;
    OnReceiveCallback    m_onReceive;

    u8 m_recvBuffer[MAX_PACKET_SIZE]{};
};

} // namespace nge::network
