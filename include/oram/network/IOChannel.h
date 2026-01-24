#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace oram::network {

template <typename T>
class IOChannel {
   public:
    void SendData(const void* data, size_t nbyte) { derived().SendDataInternal(data, nbyte); }

    void RecvData(void* data, size_t nbyte) { derived().RecvDataInternal(data, nbyte); }

    // Helper for C++ containers
    template <typename V>
    void Send(const std::vector<V>& vec) {
        size_t size = vec.size();
        SendData(&size, sizeof(size_t));
        if (size > 0) {
            SendData(vec.data(), size * sizeof(V));
        }
    }

    template <typename V>
    void Recv(std::vector<V>& vec) {
        size_t size;
        RecvData(&size, sizeof(size_t));
        vec.resize(size);
        if (size > 0) {
            RecvData(vec.data(), size * sizeof(V));
        }
    }

   private:
    T& derived() { return *static_cast<T*>(this); }
};

}  // namespace oram::network
