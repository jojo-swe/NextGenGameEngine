#include "engine/network/client/net_client.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::network {

NetClient::~NetClient() {
    Disconnect();
}

bool NetClient::Connect(const NetAddress& serverAddress) {
    if (m_state != ConnectionState::Disconnected) {
        Disconnect();
    }

    if (!m_socket.Open(0)) {
        NGE_LOG_ERROR("Client: failed to open socket");
        return false;
    }

    m_serverAddress = serverAddress;
    m_state = ConnectionState::Connecting;
    m_connectTimer = 0;
    m_connectRetries = 0;
    m_timeSinceLastRecv = 0;

    // Send connection request
    SendPacket(PacketType::ConnectionRequest, nullptr, 0);

    NGE_LOG_INFO("Client: connecting to {}", serverAddress.ToString());
    return true;
}

void NetClient::Disconnect() {
    if (m_state == ConnectionState::Disconnected) return;

    if (m_state == ConnectionState::Connected) {
        SendPacket(PacketType::Disconnect, nullptr, 0);
    }

    m_socket.Close();
    m_state = ConnectionState::Disconnected;
    m_clientId = UINT32_MAX;
    m_pendingReliables.clear();

    NGE_LOG_INFO("Client: disconnected");
    if (m_onDisconnect) m_onDisconnect(m_clientId);
}

void NetClient::Update(f32 deltaTime) {
    if (m_state == ConnectionState::Disconnected) return;

    // Receive packets
    NetAddress from;
    i32 bytesRead;
    while ((bytesRead = m_socket.Receive(from, m_recvBuffer, MAX_PACKET_SIZE)) > 0) {
        if (from == m_serverAddress) {
            ProcessPacket(m_recvBuffer, static_cast<usize>(bytesRead));
        }
    }

    m_timeSinceLastRecv += deltaTime;

    // Connection timeout
    if (m_timeSinceLastRecv > CONNECTION_TIMEOUT) {
        NGE_LOG_WARN("Client: connection timed out");
        m_state = ConnectionState::TimedOut;
        Disconnect();
        return;
    }

    // Retry connection requests
    if (m_state == ConnectionState::Connecting) {
        m_connectTimer += deltaTime;
        if (m_connectTimer > 1.0f) {
            m_connectTimer = 0;
            m_connectRetries++;
            if (m_connectRetries > 10) {
                NGE_LOG_ERROR("Client: connection failed after {} retries", m_connectRetries);
                Disconnect();
                return;
            }
            SendPacket(PacketType::ConnectionRequest, nullptr, 0);
        }
    }

    // Heartbeat
    if (m_state == ConnectionState::Connected) {
        UpdateHeartbeat(deltaTime);
    }

    // Retry reliable packets
    for (auto& pending : m_pendingReliables) {
        pending.timeSent += deltaTime;
        if (pending.timeSent > 0.1f && pending.retries < 10) {
            m_socket.Send(m_serverAddress, pending.packet.data(), pending.packet.size());
            pending.timeSent = 0;
            pending.retries++;
        }
    }
}

void NetClient::ProcessPacket(const u8* data, usize size) {
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
    (void)ack;
    (void)ackBits;

    m_timeSinceLastRecv = 0;
    m_remoteSequence = sequence;

    switch (type) {
        case PacketType::ConnectionAccept: {
            u32 clientId = reader.ReadU32();
            m_clientId = clientId;
            m_state = ConnectionState::Connected;
            NGE_LOG_INFO("Client: connected as client {}", clientId);
            if (m_onConnect) m_onConnect(clientId);
            break;
        }

        case PacketType::ConnectionDeny:
            NGE_LOG_WARN("Client: connection denied by server");
            Disconnect();
            break;

        case PacketType::Disconnect:
            NGE_LOG_INFO("Client: server disconnected us");
            m_state = ConnectionState::Disconnected;
            m_socket.Close();
            if (m_onDisconnect) m_onDisconnect(m_clientId);
            break;

        case PacketType::Heartbeat:
            // Just reset timeout (done above)
            break;

        case PacketType::Reliable: {
            ChannelId channel = reader.ReadU16();
            u16 dataSize = reader.ReadU16();
            std::vector<u8> payload(dataSize);
            reader.Read(payload.data(), dataSize);

            // Send ACK
            u8 ackPayload[2];
            BitWriter ackWriter(ackPayload, 2);
            ackWriter.WriteU16(sequence);
            SendPacket(PacketType::Ack, ackPayload, 2);

            if (m_onReceive) m_onReceive(m_clientId, channel, payload.data(), dataSize);
            break;
        }

        case PacketType::Unreliable: {
            ChannelId channel = reader.ReadU16();
            u16 dataSize = reader.ReadU16();
            std::vector<u8> payload(dataSize);
            reader.Read(payload.data(), dataSize);

            if (m_onReceive) m_onReceive(m_clientId, channel, payload.data(), dataSize);
            break;
        }

        case PacketType::Ack: {
            u16 ackedSeq = reader.ReadU16();
            m_pendingReliables.erase(
                std::remove_if(m_pendingReliables.begin(), m_pendingReliables.end(),
                    [ackedSeq](const PendingReliable& p) { return p.sequence == ackedSeq; }),
                m_pendingReliables.end());
            break;
        }

        default:
            break;
    }
}

void NetClient::Send(ChannelId channel, const void* data, usize size, bool reliable) {
    if (m_state != ConnectionState::Connected) return;

    u8 payload[MAX_PACKET_SIZE];
    BitWriter writer(payload, MAX_PACKET_SIZE);
    writer.WriteU16(channel);
    writer.WriteU16(static_cast<u16>(size));
    writer.Write(data, size);

    PacketType type = reliable ? PacketType::Reliable : PacketType::Unreliable;
    u16 seq = m_localSequence++;

    // Build full packet for potential resend
    u8 fullPacket[MAX_PACKET_SIZE];
    BitWriter pktWriter(fullPacket, MAX_PACKET_SIZE);
    pktWriter.WriteU32(PROTOCOL_ID);
    pktWriter.WriteU8(PROTOCOL_VERSION);
    pktWriter.WriteU8(static_cast<u8>(type));
    pktWriter.WriteU16(seq);
    pktWriter.WriteU16(m_remoteSequence);
    pktWriter.WriteU32(m_remoteAckBits);
    pktWriter.WriteU16(static_cast<u16>(writer.GetSize()));
    pktWriter.Write(payload, writer.GetSize());

    m_socket.Send(m_serverAddress, fullPacket, pktWriter.GetSize());

    if (reliable) {
        PendingReliable pr;
        pr.sequence = seq;
        pr.packet.assign(fullPacket, fullPacket + pktWriter.GetSize());
        pr.timeSent = 0;
        pr.retries = 0;
        m_pendingReliables.push_back(std::move(pr));
    }
}

void NetClient::SendPacket(PacketType type, const void* payload, usize payloadSize) {
    u8 buffer[MAX_PACKET_SIZE];
    BitWriter writer(buffer, MAX_PACKET_SIZE);

    writer.WriteU32(PROTOCOL_ID);
    writer.WriteU8(PROTOCOL_VERSION);
    writer.WriteU8(static_cast<u8>(type));
    writer.WriteU16(m_localSequence++);
    writer.WriteU16(m_remoteSequence);
    writer.WriteU32(m_remoteAckBits);
    writer.WriteU16(static_cast<u16>(payloadSize));

    if (payload && payloadSize > 0) {
        writer.Write(payload, payloadSize);
    }

    m_socket.Send(m_serverAddress, buffer, writer.GetSize());
}

void NetClient::UpdateHeartbeat(f32 deltaTime) {
    m_heartbeatTimer += deltaTime;
    if (m_heartbeatTimer >= HEARTBEAT_INTERVAL) {
        m_heartbeatTimer = 0;
        SendPacket(PacketType::Heartbeat, nullptr, 0);
    }
}

} // namespace nge::network
