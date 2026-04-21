#include "oram/network/NetIO.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace oram::network {

NetIO::NetIO(const std::string& address, int port, bool is_server, bool quiet)
    : is_server_(is_server) {
    if (port < 0 || port > 65535) {
        throw std::runtime_error("Invalid port number!");
    }

    if (is_server) {
        struct sockaddr_in dest;
        struct sockaddr_in serv;
        socklen_t socksize = sizeof(struct sockaddr_in);
        memset(&serv, 0, sizeof(serv));
        serv.sin_family = AF_INET;
        serv.sin_addr.s_addr = htonl(INADDR_ANY);
        serv.sin_port = htons(port);

        mysocket_ = socket(AF_INET, SOCK_STREAM, 0);
        int reuse = 1;
        setsockopt(mysocket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        if (bind(mysocket_, (struct sockaddr*)&serv, sizeof(struct sockaddr)) < 0) {
            throw std::runtime_error("Failed to bind socket: " + std::string(strerror(errno)));
        }
        if (listen(mysocket_, 1) < 0) {
            throw std::runtime_error("Failed to listen on socket: " + std::string(strerror(errno)));
        }

        consocket_ = accept(mysocket_, (struct sockaddr*)&dest, &socksize);
        close(mysocket_);
    } else {
        struct sockaddr_in dest;
        memset(&dest, 0, sizeof(dest));
        dest.sin_family = AF_INET;
        dest.sin_addr.s_addr = inet_addr(address.c_str());
        dest.sin_port = htons(port);

        while (true) {
            consocket_ = socket(AF_INET, SOCK_STREAM, 0);
            if (connect(consocket_, (struct sockaddr*)&dest, sizeof(struct sockaddr)) == 0) {
                break;
            }
            close(consocket_);
            usleep(1000);  // 1ms retry
        }
    }

    SetNoDelay();
    if (!quiet) {
        std::cout << "NetIO connected\n";
    }
}

NetIO::~NetIO() {
    if (consocket_ >= 0) {
        close(consocket_);
        consocket_ = -1;
    }
}

void NetIO::Sync() {
    int tmp = 0;
    if (is_server_) {
        SendDataInternal(&tmp, 1);
        RecvDataInternal(&tmp, 1);
    } else {
        RecvDataInternal(&tmp, 1);
        SendDataInternal(&tmp, 1);
        Flush();
    }
}

void NetIO::Flush() {}

void NetIO::SetNoDelay() {
    const int one = 1;
    setsockopt(consocket_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

void NetIO::SetDelay() {
    const int zero = 0;
    setsockopt(consocket_, IPPROTO_TCP, TCP_NODELAY, &zero, sizeof(zero));
}

void NetIO::SendVec(const std::vector<uint8_t>& data) {
    const uint64_t size = data.size();
    SendData(&size, sizeof(size));
    if (size > 0) {
        SendData(data.data(), size);
    }
}

void NetIO::RecvVec(std::vector<uint8_t>& data) {
    uint64_t size = 0;
    RecvData(&size, sizeof(size));
    data.resize(size);
    if (size > 0) {
        RecvData(data.data(), size);
    }
}

void NetIO::SendDataInternal(const void* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        const ssize_t res =
            send(consocket_, static_cast<const char*>(data) + sent, len - sent, MSG_NOSIGNAL);
        if (res > 0) {
            sent += static_cast<size_t>(res);
        } else if (res == 0) {
            throw std::runtime_error("NetIO send failed");
        } else if (errno != EINTR) {
            throw std::runtime_error("NetIO send failed: " + std::string(strerror(errno)));
        }
    }
}

void NetIO::RecvDataInternal(void* data, size_t len) {
    size_t received = 0;
    while (received < len) {
        const ssize_t res = recv(consocket_, static_cast<char*>(data) + received, len - received, 0);
        if (res > 0) {
            received += static_cast<size_t>(res);
        } else if (res == 0) {
            throw std::runtime_error("NetIO recv failed");
        } else if (errno != EINTR) {
            throw std::runtime_error("NetIO recv failed: " + std::string(strerror(errno)));
        }
    }
}

}  // namespace oram::network
