#include "engine/network/core/net_socket.h"
#include "engine/core/logging/log.h"

#if defined(NGE_PLATFORM_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib") // MSVC only; other toolchains link ws2_32 via CMake
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define closesocket close
#endif

// Address-length type differs: int on Winsock, socklen_t on POSIX.
#if defined(NGE_PLATFORM_WINDOWS)
using AddrLen = int;
#else
using AddrLen = socklen_t;
#endif

namespace nge::network {

bool UDPSocket::s_initialized = false;

bool UDPSocket::InitSockets() {
    if (s_initialized) return true;
#if defined(NGE_PLATFORM_WINDOWS)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        NGE_LOG_ERROR("WSAStartup failed");
        return false;
    }
#endif
    s_initialized = true;
    return true;
}

void UDPSocket::ShutdownSockets() {
#if defined(NGE_PLATFORM_WINDOWS)
    WSACleanup();
#endif
    s_initialized = false;
}

UDPSocket::~UDPSocket() {
    Close();
}

bool UDPSocket::Open(u16 port) {
    if (!InitSockets()) return false;

    auto sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#if defined(NGE_PLATFORM_WINDOWS)
    if (sock == INVALID_SOCKET) {
#else
    if (sock < 0) {
#endif
        NGE_LOG_ERROR("Failed to create UDP socket");
        return false;
    }

    // Bind
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        NGE_LOG_ERROR("Failed to bind UDP socket on port {}", port);
        closesocket(sock);
        return false;
    }

    // Get actual bound port
    sockaddr_in boundAddr{};
    AddrLen addrLen = sizeof(boundAddr);
    getsockname(sock, reinterpret_cast<sockaddr*>(&boundAddr), &addrLen);
    m_port = ntohs(boundAddr.sin_port);

    // Set non-blocking
#if defined(NGE_PLATFORM_WINDOWS)
    u_long nonBlocking = 1;
    ioctlsocket(sock, FIONBIO, &nonBlocking);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

    // Increase receive buffer
    int bufSize = 256 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));

    m_socket = static_cast<u64>(sock);
    NGE_LOG_INFO("UDP socket opened on port {}", m_port);
    return true;
}

void UDPSocket::Close() {
    if (m_socket != INVALID_SOCKET_VAL) {
        closesocket(static_cast<SOCKET>(m_socket));
        m_socket = INVALID_SOCKET_VAL;
        m_port = 0;
    }
}

i32 UDPSocket::Send(const NetAddress& to, const void* data, usize size) {
    if (m_socket == INVALID_SOCKET_VAL) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(to.ip);
    addr.sin_port = htons(to.port);

    auto result = sendto(static_cast<SOCKET>(m_socket),
                          static_cast<const char*>(data),
                          static_cast<int>(size), 0,
                          reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return static_cast<i32>(result);
}

i32 UDPSocket::Receive(NetAddress& from, void* buffer, usize bufferSize) {
    if (m_socket == INVALID_SOCKET_VAL) return -1;

    sockaddr_in addr{};
    AddrLen addrLen = sizeof(addr);

    auto result = recvfrom(static_cast<SOCKET>(m_socket),
                            static_cast<char*>(buffer),
                            static_cast<int>(bufferSize), 0,
                            reinterpret_cast<sockaddr*>(&addr), &addrLen);

#if defined(NGE_PLATFORM_WINDOWS)
    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return 0;
        return -1;
    }
#else
    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
#endif

    from.ip   = ntohl(addr.sin_addr.s_addr);
    from.port = ntohs(addr.sin_port);
    return static_cast<i32>(result);
}

} // namespace nge::network
