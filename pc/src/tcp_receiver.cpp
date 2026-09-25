#include "tcp_receiver.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifndef SHUT_RDWR
        #define SHUT_RDWR SD_BOTH
    #endif
    using socket_len_t = int;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define SOCKET_ERROR (-1)
    #define closesocket close
    using socket_len_t = socklen_t;
#endif

namespace {

constexpr uint32_t kMaxPayload = 10 * 1024 * 1024;
constexpr int kAcceptTimeoutMs = 500;

}  // namespace

TcpReceiver::TcpReceiver() {}

TcpReceiver::~TcpReceiver() {
    stop();
}

bool TcpReceiver::start(int port) {
    if (running) return true;

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return false;
    }
#endif

    serverSocket = static_cast<int>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (serverSocket == SOCKET_ERROR) {
        std::cerr << "Failed to create socket" << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(serverSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed on port " << port << std::endl;
        closesocket(serverSocket);
        serverSocket = -1;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    if (listen(serverSocket, 1) == SOCKET_ERROR) {
        std::cerr << "Listen failed" << std::endl;
        closesocket(serverSocket);
        serverSocket = -1;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    running = true;
    std::cout << "Listening on port " << port << "..." << std::endl;

    std::thread(&TcpReceiver::acceptLoop, this).detach();
    return true;
}

void TcpReceiver::stop() {
    if (!running && serverSocket < 0 && clientSocket < 0) return;
    running = false;

    // Wake up the receive loop; it owns closing its own socket.
    if (clientSocket >= 0) {
        shutdown(clientSocket, SHUT_RDWR);
    }
    // Wakes up accept() on Windows; the accept loop exits on select timeout
    // or on the resulting error.
    if (serverSocket >= 0) {
        closesocket(serverSocket);
        serverSocket = -1;
    }

    // Give detached threads a moment to observe the shutdown before
    // Winsock state is torn down.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

#ifdef _WIN32
    WSACleanup();
#endif
}

void TcpReceiver::setFrameCallback(FrameCallback cb) {
    frameCallback = std::move(cb);
}

void TcpReceiver::acceptLoop() {
    const int listenFd = serverSocket;

    while (running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listenFd, &fds);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = kAcceptTimeoutMs * 1000;

        int ready = select(listenFd + 1, &fds, nullptr, nullptr, &tv);
        if (ready <= 0) {
            if (ready < 0 && running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            continue;
        }

        sockaddr_in clientAddr{};
        socket_len_t clientLen = sizeof(clientAddr);

        int newClient = static_cast<int>(
            accept(listenFd, (sockaddr*)&clientAddr, &clientLen));
        if (newClient == SOCKET_ERROR) {
            if (running) std::cerr << "Accept failed" << std::endl;
            continue;
        }

        // Retire any previous client without closing its fd here: the old
        // receive loop owns the close, which prevents fd-reuse races.
        int oldClient = clientSocket;
        if (oldClient >= 0 && oldClient != newClient) {
            shutdown(oldClient, SHUT_RDWR);
        }
        clientSocket = newClient;

        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, INET_ADDRSTRLEN);
        std::cout << "Client connected: " << ip << std::endl;

        std::thread(&TcpReceiver::receiveLoop, this, newClient).detach();
    }
}

void TcpReceiver::receiveLoop(int sock) {
    const int HEADER_SIZE = 4;
    uint8_t header[HEADER_SIZE];

    auto fail = [this, sock](const char* reason) {
        std::cout << reason << std::endl;
        if (clientSocket == sock) clientSocket = -1;
        shutdown(sock, SHUT_RDWR);
        closesocket(sock);
    };

    while (running) {
        int totalRead = 0;
        while (totalRead < HEADER_SIZE) {
            int n = recv(sock, (char*)(header + totalRead), HEADER_SIZE - totalRead, 0);
            if (n <= 0) {
                fail("Client disconnected");
                return;
            }
            totalRead += n;
        }

        uint32_t payloadLength = (uint32_t(header[0]) << 24) |
                                 (uint32_t(header[1]) << 16) |
                                 (uint32_t(header[2]) << 8) |
                                 uint32_t(header[3]);

        if (payloadLength == 0 || payloadLength > kMaxPayload) {
            std::cerr << "Invalid payload size: " << payloadLength << std::endl;
            fail("Closing connection");
            return;
        }

        std::vector<uint8_t> jpegData(payloadLength);
        totalRead = 0;
        while (totalRead < static_cast<int>(payloadLength)) {
            int n = recv(sock, (char*)(jpegData.data() + totalRead),
                         static_cast<int>(payloadLength) - totalRead, 0);
            if (n <= 0) {
                fail("Client disconnected");
                return;
            }
            totalRead += n;
        }

        if (frameCallback) {
            frameCallback(jpegData);
        }
    }

    fail("Receive loop stopped");
}
