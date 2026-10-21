#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace SMB {
    inline std::atomic_bool connected{false};
    inline int socket_handle = -1;

    enum class Result {
        Success,
        InvalidPort,
        SocketCreateFailed,
        SocketConnectFailed,
        NegotiateFailed,
        InvalidCredentials,
    };

    inline void Disconnect();

    inline bool IsConnected() {
        return connected.load(std::memory_order_acquire);
    }

    struct __attribute__((packed)) Smb2Header {
        std::array<uint8_t, 4> protocolId{};
        uint16_t structureSize = 64;
        uint16_t creditCharge = 1;
        uint32_t status = 0;
        uint16_t command = 0;
        uint16_t creditsRequested = 1;
        uint32_t flags = 0;
        uint32_t nextCommand = 0;
        uint64_t messageId = 0;
        uint32_t reserved = 0;
        uint32_t treeId = 0;
        uint64_t sessionId = 0;
        std::array<uint8_t, 16> signature{};
    };

    struct __attribute__((packed)) Smb2NegotiateRequest {
        uint16_t structureSize = 36;
        uint16_t securityMode = 0x01;
        uint16_t dialectCount = 2;
        uint16_t reserved = 0;
        uint32_t negotiateContextOffset = 0;
        uint16_t negotiateContextCount = 0;
        uint16_t reserved2 = 0;
        std::array<uint8_t, 16> clientGuid{};
        uint32_t capabilities = 0;
        uint32_t clientSecurityBlobSize = 0;
        uint16_t reserved3 = 0;
        uint16_t reserved4 = 0;
        std::array<uint16_t, 2> dialects{{0x0202, 0x0300}};
    };

    inline bool SendSmb2NegotiateRequest(int sock) {
        Smb2Header header{};
        header.protocolId = {0xFE, 'S', 'M', 'B'};
        header.command = 0x0000; // SMB2 NEGOTIATE

        Smb2NegotiateRequest request{};
        std::array<uint8_t, sizeof(Smb2Header) + sizeof(Smb2NegotiateRequest)> packet{};

        std::memcpy(packet.data(), &header, sizeof(header));
        std::memcpy(packet.data() + sizeof(header), &request, sizeof(request));

        const ssize_t sent = ::send(sock, packet.data(), packet.size(), 0);
        if (sent < 0)
            return false;

        char response[512]{};
        const ssize_t received = ::recv(sock, response, sizeof(response), 0);
        if (received <= 0)
            return false;

        return true;
    }

    inline bool Connect(std::string_view ip, int port, std::string_view user, std::string_view pass) {
        (void)user;
        (void)pass;

        if (port <= 0 || port > 65535)
            return false;

        if (IsConnected())
            Disconnect();

        const int sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0)
            return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));

        std::string ipString(ip);
        if (::inet_pton(AF_INET, ipString.c_str(), &addr.sin_addr) != 1) {
            ::close(sock);
            return false;
        }

        if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(sock);
            return false;
        }

        if (!SendSmb2NegotiateRequest(sock)) {
            ::close(sock);
            return false;
        }

        socket_handle = sock;
        connected.store(true, std::memory_order_release);
        return true;
    }

    inline void Disconnect() {
        if (socket_handle >= 0) {
            ::shutdown(socket_handle, SHUT_RDWR);
            ::close(socket_handle);
            socket_handle = -1;
        }

        connected.store(false, std::memory_order_release);
    }
}
