#include "engine/network/server/net_server.h"
#include "engine/core/logging/log.h"

namespace nge::network {

NetServer::~NetServer() {
    Stop();
}

bool NetServer::Start(u16 port) {
    if (m_running) return true;

    if (!m_socket.Open(port)) {
        NGE_LOG_ERROR("Server: failed to open socket on port {}", port);
        return false;
    }

    m_running = true;
    NGE_LOG_INFO("Server started on port {}", m_socket.GetPort());
    return true;
}

void NetServer::Stop() {
    if (!m_running) return;

    // Disconnect all clients
    for (u32 i = 0; i < MAX_CLIENTS; ++i) {
        if (m_clients[i].active) {
            SendPacket(m_clients[i].info.address, PacketType::Disconnect, nullptr, 0);
            m_clients[i].active = false;
        }
    }

    m_socket.Close();
    m_running = false;
    NGE_LOG_INFO("Server stopped");
}

void NetServer::Update(f32 deltaTime) {
    if (!m_running) return;

    // Receive all pending packets
    NetAddress from;
    i32 bytesRead;
    while ((bytesRead = m_socket.Receive(from, m_recvBuffer, MAX_PACKET_SIZE)) > 0) {
        ProcessPacket(from, m_recvBuffer, static_cast<usize>(bytesRead));
    }

    UpdateHeartbeats(deltaTime);
    CheckTimeouts(deltaTime);

    // Retry reliable packets
    for (auto& slot : m_clients) {
        if (!slot.active) continue;
        for (auto& pending : slot.pendingReliables) {
            pending.timeSent += deltaTime;
            if (pending.timeSent > 0.1f && pending.retries < 10) { // 100ms retry
                m_socket.Send(slot.info.address, pending.data.data(), pending.data.size());
                pending.timeSent = 0;
                pending.retries++;
            }
        }
    }
}

void NetServer::ProcessPacket(const NetAddress& from, const u8* data, usize size) {
    if (size < PacketHeader::SIZE) return;

    BitReader reader(data, size);
    u32 protoId = reader.ReadU32();
    if (protoId != PROTOCOL_ID) return;

    u8 version = reader.ReadU8();
    if (version != PROTOCOL_VERSION) return;

    auto type = static_cast<PacketType>(reader.ReadU8());
    u16 sequence = reader.ReadU16();
    u16 ack      = reader.ReadU16();
    u32 ackBits  = reader.ReadU32();
    u16 payloadSize = reader.ReadU16();

    (void)payloadSize;

    i32 clientIdx = FindClientByAddress(from);

    switch (type) {
        case PacketType::ConnectionRequest:
            HandleConnectionRequest(from, reader);
            break;

        case PacketType::Disconnect:
            if (clientIdx >= 0) HandleDisconnect(static_cast<u32>(clientIdx));
            break;

        case PacketType::Heartbeat:
            if (clientIdx >= 0) {
                m_clients[clientIdx].info.timeSinceLastRecv = 0;
                m_clients[clientIdx].info.remoteSequence = sequence;
                m_clients[clientIdx].info.remoteAckBits = ackBits;
            }
            break;

        case PacketType::Reliable:
            if (clientIdx >= 0) {
                m_clients[clientIdx].info.timeSinceLastRecv = 0;
                HandleReliable(static_cast<u32>(clientIdx), reader);
                // Send ACK
                SendPacket(from, PacketType::Ack, &sequence, sizeof(sequence), ack);
            }
            break;

        case PacketType::Unreliable:
            if (clientIdx >= 0) {
                m_clients[clientIdx].info.timeSinceLastRecv = 0;
                HandleUnreliable(static_cast<u32>(clientIdx), reader);
            }
            break;

        case PacketType::Ack:
            if (clientIdx >= 0) {
                HandleAck(static_cast<u32>(clientIdx), reader);
            }
            break;

        default:
            break;
    }
}

void NetServer::HandleConnectionRequest(const NetAddress& from, BitReader& /*reader*/) {
    // Check if already connected
    i32 existing = FindClientByAddress(from);
    if (existing >= 0) {
        // Resend accept
        u32 cid = static_cast<u32>(existing);
        SendPacket(from, PacketType::ConnectionAccept, &cid, sizeof(cid));
        return;
    }

    i32 slot = AllocateClientSlot();
    if (slot < 0) {
        // Server full
        SendPacket(from, PacketType::ConnectionDeny, nullptr, 0);
        NGE_LOG_WARN("Server: connection denied (full) from {}", from.ToString());
        return;
    }

    auto& client = m_clients[slot];
    client.active = true;
    client.info.address = from;
    client.info.state = ConnectionState::Connected;
    client.info.clientId = static_cast<u32>(slot);
    client.info.timeSinceLastRecv = 0;
    client.heartbeatTimer = 0;

    u32 cid = static_cast<u32>(slot);
    SendPacket(from, PacketType::ConnectionAccept, &cid, sizeof(cid));

    NGE_LOG_INFO("Server: client {} connected from {}", cid, from.ToString());
    if (m_onConnect) m_onConnect(cid);
}

void NetServer::HandleDisconnect(u32 clientId) {
    if (clientId >= MAX_CLIENTS || !m_clients[clientId].active) return;

    NGE_LOG_INFO("Server: client {} disconnected", clientId);
    m_clients[clientId].active = false;
    m_clients[clientId].info.state = ConnectionState::Disconnected;
    if (m_onDisconnect) m_onDisconnect(clientId);
}

void NetServer::HandleReliable(u32 clientId, BitReader& reader) {
    ChannelId channel = reader.ReadU16();
    u16 dataSize = reader.ReadU16();
    const u8* payload = reinterpret_cast<const u8*>(reader.GetBytesRead()) + 
                        reinterpret_cast<usize>(nullptr); // Get current read position
    // Safe payload extraction
    std::vector<u8> payloadData(dataSize);
    reader.Read(payloadData.data(), dataSize);

    if (m_onReceive) m_onReceive(clientId, channel, payloadData.data(), dataSize);
}

void NetServer::HandleUnreliable(u32 clientId, BitReader& reader) {
    ChannelId channel = reader.ReadU16();
    u16 dataSize = reader.ReadU16();
    std::vector<u8> payloadData(dataSize);
    reader.Read(payloadData.data(), dataSize);

    if (m_onReceive) m_onReceive(clientId, channel, payloadData.data(), dataSize);
}

void NetServer::HandleAck(u32 clientId, BitReader& reader) {
    u16 ackedSequence = reader.ReadU16();

    // Remove from pending reliables
    auto& pending = m_clients[clientId].pendingReliables;
    pending.erase(
        std::remove_if(pending.begin(), pending.end(),
            [ackedSequence](const ClientSlot::PendingReliable& p) {
                return p.sequence == ackedSequence;
            }),
        pending.end());
}

void NetServer::Send(u32 clientId, ChannelId channel, const void* data, usize size, bool reliable) {
    if (clientId >= MAX_CLIENTS || !m_clients[clientId].active) return;

    u8 payload[MAX_PACKET_SIZE];
    BitWriter writer(payload, MAX_PACKET_SIZE);
    writer.WriteU16(channel);
    writer.WriteU16(static_cast<u16>(size));
    writer.Write(data, size);

    PacketType type = reliable ? PacketType::Reliable : PacketType::Unreliable;
    u16 seq = m_clients[clientId].info.localSequence++;

    SendPacket(m_clients[clientId].info.address, type, payload, writer.GetSize(), seq);

    if (reliable) {
        // Store for retry
        ClientSlot::PendingReliable pr;
        pr.sequence = seq;
        pr.timeSent = 0;
        pr.retries = 0;
        // Store full packet for resend
        // TODO: store the complete encoded packet
        m_clients[clientId].pendingReliables.push_back(std::move(pr));
    }
}

void NetServer::Broadcast(ChannelId channel, const void* data, usize size, bool reliable) {
    for (u32 i = 0; i < MAX_CLIENTS; ++i) {
        if (m_clients[i].active) {
            Send(i, channel, data, size, reliable);
        }
    }
}

void NetServer::Disconnect(u32 clientId) {
    if (clientId >= MAX_CLIENTS || !m_clients[clientId].active) return;
    SendPacket(m_clients[clientId].info.address, PacketType::Disconnect, nullptr, 0);
    HandleDisconnect(clientId);
}

void NetServer::SendPacket(const NetAddress& to, PacketType type, const void* payload, usize payloadSize, u16 sequence) {
    u8 buffer[MAX_PACKET_SIZE];
    BitWriter writer(buffer, MAX_PACKET_SIZE);

    writer.WriteU32(PROTOCOL_ID);
    writer.WriteU8(PROTOCOL_VERSION);
    writer.WriteU8(static_cast<u8>(type));
    writer.WriteU16(sequence);
    writer.WriteU16(0); // ack
    writer.WriteU32(0); // ack bitfield
    writer.WriteU16(static_cast<u16>(payloadSize));

    if (payload && payloadSize > 0) {
        writer.Write(payload, payloadSize);
    }

    m_socket.Send(to, buffer, writer.GetSize());
}

void NetServer::UpdateHeartbeats(f32 deltaTime) {
    for (auto& slot : m_clients) {
        if (!slot.active) continue;
        slot.heartbeatTimer += deltaTime;
        if (slot.heartbeatTimer >= HEARTBEAT_INTERVAL) {
            slot.heartbeatTimer = 0;
            SendPacket(slot.info.address, PacketType::Heartbeat, nullptr, 0, slot.info.localSequence++);
        }
    }
}

void NetServer::CheckTimeouts(f32 deltaTime) {
    for (u32 i = 0; i < MAX_CLIENTS; ++i) {
        if (!m_clients[i].active) continue;
        m_clients[i].info.timeSinceLastRecv += deltaTime;
        if (m_clients[i].info.timeSinceLastRecv > CONNECTION_TIMEOUT) {
            NGE_LOG_WARN("Server: client {} timed out", i);
            m_clients[i].info.state = ConnectionState::TimedOut;
            HandleDisconnect(i);
        }
    }
}

i32 NetServer::FindClientByAddress(const NetAddress& addr) const {
    for (u32 i = 0; i < MAX_CLIENTS; ++i) {
        if (m_clients[i].active && m_clients[i].info.address == addr) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

i32 NetServer::AllocateClientSlot() {
    for (u32 i = 0; i < MAX_CLIENTS; ++i) {
        if (!m_clients[i].active) return static_cast<i32>(i);
    }
    return -1;
}

u32 NetServer::GetConnectedClientCount() const {
    u32 count = 0;
    for (const auto& slot : m_clients) {
        if (slot.active) count++;
    }
    return count;
}

const ConnectionInfo* NetServer::GetClientInfo(u32 clientId) const {
    if (clientId >= MAX_CLIENTS || !m_clients[clientId].active) return nullptr;
    return &m_clients[clientId].info;
}

} // namespace nge::network
