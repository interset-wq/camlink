#pragma once

#include <cstdint>
#include <vector>
#include <functional>

class TcpReceiver {
public:
    using FrameCallback = std::function<void(const std::vector<uint8_t>& jpegData)>;

    TcpReceiver();
    ~TcpReceiver();

    bool start(int port = 5555);
    void stop();
    void setFrameCallback(FrameCallback cb);

private:
    void acceptLoop();
    void receiveLoop(int clientSocket);

    int serverSocket = -1;
    int clientSocket = -1;
    bool running = false;
    FrameCallback frameCallback;
};
