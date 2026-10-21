#pragma once

#include <atomic>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace QuestConnection {
    inline std::atomic_bool connected{false};
    inline std::atomic_bool listening{false};
    inline int listener_fd = -1;

    inline bool IsConnected() {
        return connected.load(std::memory_order_acquire);
    }

    inline void Disconnect() {
        connected.store(false, std::memory_order_release);

        if (listener_fd >= 0) {
            ::shutdown(listener_fd, SHUT_RDWR);
            ::close(listener_fd);
            listener_fd = -1;
        }
    }

    inline void StopQuestHandshakeListener() {
        listening.store(false, std::memory_order_release);
        Disconnect();
    }

    inline void StartQuestHandshakeListener(std::function<void(bool)> onReady = nullptr) {
        if (listening.load(std::memory_order_acquire))
            return;

        listening.store(true, std::memory_order_release);

        std::thread([onReady = std::move(onReady)]() mutable {
            const int server_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (server_fd < 0) {
                listening.store(false, std::memory_order_release);
                if (onReady)
                    onReady(false);
                return;
            }

            listener_fd = server_fd;

            int enable = 1;
            ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(9000);

            if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
                ::close(server_fd);
                listener_fd = -1;
                listening.store(false, std::memory_order_release);
                if (onReady)
                    onReady(false);
                return;
            }

            if (::listen(server_fd, 1) < 0) {
                ::close(server_fd);
                listener_fd = -1;
                listening.store(false, std::memory_order_release);
                if (onReady)
                    onReady(false);
                return;
            }

            while (listening.load(std::memory_order_acquire)) {
                const int client_fd = ::accept(server_fd, nullptr, nullptr);
                if (client_fd < 0)
                    continue;

                char buffer[256]{};
                const ssize_t received = ::recv(client_fd, buffer, sizeof(buffer) - 1, 0);

                const bool handshakeOk = received > 0 && (
                    std::strstr(buffer, "PC_READY") != nullptr
                );

                ::close(client_fd);

                if (handshakeOk) {
                    connected.store(true, std::memory_order_release);
                    listening.store(false, std::memory_order_release);
                    if (onReady)
                        onReady(true);
                    break;
                }
            }

            ::close(server_fd);
            listener_fd = -1;
            listening.store(false, std::memory_order_release);
        }).detach();
    }
}
