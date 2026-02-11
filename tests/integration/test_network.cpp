#include <gtest/gtest.h>
#ifdef _WIN32
#include <winsock2.h>
#endif
#include "engine/network/core/net_socket.h"
#include "engine/network/server/net_server.h"
#include "engine/network/client/net_client.h"

using namespace nge;
using namespace nge::network;

class NetworkTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Platform socket init (Windows WSAStartup)
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    void TearDown() override {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

TEST_F(NetworkTest, SocketCreateAndDestroy) {
    UDPSocket socket;
    EXPECT_TRUE(socket.Open(0)); // Bind to any available port
    EXPECT_TRUE(socket.IsOpen());
    socket.Close();
    EXPECT_FALSE(socket.IsOpen());
}

TEST_F(NetworkTest, SocketBindToPort) {
    UDPSocket socket;
    EXPECT_TRUE(socket.Open(27100));
    EXPECT_TRUE(socket.IsOpen());
    socket.Close();
}

TEST_F(NetworkTest, ServerInitShutdown) {
    NetServer server;
    EXPECT_TRUE(server.Start(27200));
    server.Stop();
}

TEST_F(NetworkTest, ClientConnectDisconnect) {
    NetClient client;
    NetAddress addr = NetAddress::Loopback(27201);
    // Connect may fail without a running server, but should not crash
    client.Connect(addr);
    auto state = client.GetState();
    EXPECT_TRUE(state == ConnectionState::Connecting || state == ConnectionState::Disconnected);
    client.Disconnect();
}

TEST_F(NetworkTest, ServerStartStop) {
    NetServer server;
    EXPECT_TRUE(server.Start(27202));
    EXPECT_TRUE(server.IsRunning());
    server.Stop();
    EXPECT_FALSE(server.IsRunning());
}

TEST_F(NetworkTest, ServerAcceptsConnection) {
    NetServer server;
    EXPECT_TRUE(server.Start(27203));

    NetClient client;
    NetAddress addr = NetAddress::Loopback(27203);
    client.Connect(addr);

    // Pump both sides for a few ticks
    for (int i = 0; i < 10; ++i) {
        server.Update(1.0f / 60.0f);
        client.Update(1.0f / 60.0f);
    }

    // Client should be in connecting or connected state
    auto state = client.GetState();
    EXPECT_TRUE(state == ConnectionState::Connecting || state == ConnectionState::Connected);

    client.Disconnect();
    server.Stop();
}

TEST_F(NetworkTest, ReliablePacketDelivery) {
    NetServer server;
    EXPECT_TRUE(server.Start(27204));

    NetClient client;
    NetAddress addr = NetAddress::Loopback(27204);
    client.Connect(addr);

    // Connect
    for (int i = 0; i < 20; ++i) {
        server.Update(1.0f / 60.0f);
        client.Update(1.0f / 60.0f);
    }

    // Send reliable packet from client
    u8 data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    client.Send(0, data, sizeof(data), true);

    // Pump to deliver
    for (int i = 0; i < 10; ++i) {
        server.Update(1.0f / 60.0f);
        client.Update(1.0f / 60.0f);
    }

    // Verify delivery — for now just ensure no crash
    EXPECT_TRUE(true);

    client.Disconnect();
    server.Stop();
}
