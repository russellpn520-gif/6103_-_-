/**
 * @file udp_socket.cpp
 * @brief UDP socket wrapper (BSD sockets on macOS/Linux, Winsock on Windows).
 */

#include "common/udp_socket.h"
#include "common/logger.h"
#include <cstring>

#if defined(_WIN32) && !defined(SIO_UDP_CONNRESET)
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

UdpSocket::UdpSocket() : sockfd_(INVALID_SOCKET_FD), bound_(false) {
    if (!platform::initSockets()) {
        LOG_ERROR("Failed to initialise the socket library");
        return;
    }

    sockfd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ == INVALID_SOCKET_FD) {
        LOG_ERROR("Failed to create socket: " + platform::lastSocketError());
        return;
    }

#ifdef _WIN32
    // Windows reports an ICMP "port unreachable" (e.g. for a reply sent to a
    // client that has already exited) as an error on the NEXT recvfrom() of
    // this socket. UDP is connectionless, so switch that off to behave like
    // Linux/macOS.
    BOOL report_reset = FALSE;
    DWORD bytes_returned = 0;
    WSAIoctl(sockfd_, SIO_UDP_CONNRESET, &report_reset, sizeof(report_reset),
             nullptr, 0, &bytes_returned, nullptr, nullptr);
#endif
}

UdpSocket::~UdpSocket() {
    close();
}

bool UdpSocket::bind(uint16_t port) {
    if (!isValid()) {
        LOG_ERROR("Invalid socket");
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(sockfd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind to port " + std::to_string(port) + ": " + platform::lastSocketError());
        return false;
    }

    bound_ = true;
    return true;
}

ssize_t UdpSocket::sendTo(const void* data, size_t length, const struct sockaddr_in& dest_addr) {
    if (!isValid()) {
        LOG_ERROR("Invalid socket");
        return -1;
    }

#ifdef _WIN32
    ssize_t sent = ::sendto(sockfd_, static_cast<const char*>(data), static_cast<int>(length), 0,
                            reinterpret_cast<const struct sockaddr*>(&dest_addr), sizeof(dest_addr));
#else
    ssize_t sent = ::sendto(sockfd_, data, length, 0,
                            reinterpret_cast<const struct sockaddr*>(&dest_addr), sizeof(dest_addr));
#endif

    if (sent < 0) {
        LOG_ERROR("Failed to send to " + addressToString(dest_addr) + ": " + platform::lastSocketError());
    }

    return sent;
}

ssize_t UdpSocket::recvFrom(void* buffer, size_t buffer_size, struct sockaddr_in& src_addr,
                            int timeout_ms) {
    if (!isValid()) {
        LOG_ERROR("Invalid socket");
        return -1;
    }

    // Wait with select() so the caller can implement timeouts (retransmission,
    // end of the monitor interval) without blocking forever in recvfrom().
    if (timeout_ms >= 0) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd_, &readfds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

#ifdef _WIN32
        int result = ::select(0, &readfds, nullptr, nullptr, &tv);   // first argument ignored on Windows
#else
        int result = ::select(sockfd_ + 1, &readfds, nullptr, nullptr, &tv);
#endif

        if (result == 0) {
            return 0;   // timeout
        }
        if (result < 0) {
            LOG_ERROR("select() failed: " + platform::lastSocketError());
            return -1;
        }
    }

    socklen_t addr_len = sizeof(src_addr);
#ifdef _WIN32
    ssize_t received = ::recvfrom(sockfd_, static_cast<char*>(buffer), static_cast<int>(buffer_size), 0,
                                  reinterpret_cast<struct sockaddr*>(&src_addr), &addr_len);
#else
    ssize_t received = ::recvfrom(sockfd_, buffer, buffer_size, 0,
                                  reinterpret_cast<struct sockaddr*>(&src_addr), &addr_len);
#endif

    if (received < 0) {
        LOG_ERROR("Failed to receive: " + platform::lastSocketError());
    }

    return received;
}

void UdpSocket::close() {
    if (isValid()) {
        platform::closeSocket(sockfd_);
        sockfd_ = INVALID_SOCKET_FD;
        bound_ = false;
    }
}

uint16_t UdpSocket::localPort() const {
    if (!isValid()) {
        return 0;
    }
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (::getsockname(sockfd_, reinterpret_cast<struct sockaddr*>(&addr), &addr_len) < 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

bool UdpSocket::createAddress(const std::string& ip, uint16_t port, struct sockaddr_in& addr) {
    platform::initSockets();

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        LOG_ERROR("Invalid IPv4 address: " + ip);
        return false;
    }

    return true;
}

std::string UdpSocket::addressToString(const struct sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip)) == nullptr) {
        return "?:" + std::to_string(ntohs(addr.sin_port));
    }
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}
