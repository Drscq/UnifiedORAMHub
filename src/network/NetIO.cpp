#include "oram/network/NetIO.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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
    stream_ = fdopen(consocket_, "wb+");
    buffer_ = new char[kNetworkBufferSize];
    memset(buffer_, 0, kNetworkBufferSize);
    setvbuf(stream_, buffer_, _IOFBF, kNetworkBufferSize);  // Full buffering using our buffer

    if (!quiet) {
        std::cout << "NetIO connected\n";
    }
}

NetIO::~NetIO() {
    Flush();
    if (stream_) fclose(stream_);  // fclose closes the file descriptor (consocket_) too
    delete[] buffer_;
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

void NetIO::Flush() { fflush(stream_); }

void NetIO::SetNoDelay() {
    const int one = 1;
    setsockopt(consocket_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

void NetIO::SetDelay() {
    const int zero = 0;
    setsockopt(consocket_, IPPROTO_TCP, TCP_NODELAY, &zero, sizeof(zero));
}

void NetIO::SendDataInternal(const void* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        size_t res = fwrite((char*)data + sent, 1, len - sent, stream_);
        if (res > 0) {
            sent += res;
        } else {
            // Error handling could be improved
            throw std::runtime_error("NetIO send failed");
        }
    }
    has_sent_ = true;
}

void NetIO::RecvDataInternal(void* data, size_t len) {
    if (has_sent_) {
        Flush();
    }
    has_sent_ = false;

    size_t received = 0;
    while (received < len) {
        size_t res = fread((char*)data + received, 1, len - received, stream_);
        if (res > 0) {
            received += res;
        } else {
            // Error handling
            throw std::runtime_error("NetIO recv failed");
        }
    }
}

}  // namespace oram::network
