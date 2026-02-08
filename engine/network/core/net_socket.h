#pragma once

#include "engine/network/core/network_types.h"

namespace nge::network {

// ─── UDP Socket Wrapper ──────────────────────────────────────────────────
// Platform-abstracted non-blocking UDP socket for game networking.

class UDPSocket {
public:
    UDPSocket() = default;
    ~UDPSocket();

    UDPSocket(const UDPSocket&) = delete;
    UDPSocket& operator=(const UDPSocket&) = delete;

    bool Open(u16 port = 0);
    void Close();
    bool IsOpen() const { return m_socket != INVALID_SOCKET_VAL; }

    // Send data to address. Returns bytes sent or -1 on error.
    i32 Send(const NetAddress& to, const void* data, usize size);

    // Receive data. Returns bytes received, 0 if no data, -1 on error.
    i32 Receive(NetAddress& from, void* buffer, usize bufferSize);

    u16 GetPort() const { return m_port; }

private:
    static constexpr u64 INVALID_SOCKET_VAL = ~0ULL;

    u64 m_socket = INVALID_SOCKET_VAL;
    u16 m_port   = 0;

    static bool s_initialized;
    static bool InitSockets();
    static void ShutdownSockets();
};

} // namespace nge::network
