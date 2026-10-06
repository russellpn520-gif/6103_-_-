/**
 * @file udp_socket.h
 * @brief Thin RAII wrapper around one UDP socket.
 */

#ifndef UDP_SOCKET_H
#define UDP_SOCKET_H

#include <string>
#include <cstdint>
#include "common/platform.h"

/**
 * @brief Owns one UDP (SOCK_DGRAM) socket and exposes the system calls the
 *        project needs: bind(), sendto(), and recvfrom() with a select()-based
 *        timeout.
 */
class UdpSocket {
public:
    /**
     * @brief Create the socket (check isValid() afterwards).
     */
    UdpSocket();

    /**
     * @brief Close the socket.
     */
    ~UdpSocket();

    /**
     * @brief Bind to a local port on all interfaces. Port 0 lets the OS pick
     *        a free port (used by the client).
     * @return true on success
     */
    bool bind(uint16_t port);

    /**
     * @brief Send one datagram.
     * @return number of bytes sent, or -1 on error
     */
    ssize_t sendTo(const void* data, size_t length, const struct sockaddr_in& dest_addr);

    /**
     * @brief Receive one datagram.
     * @param buffer       destination buffer
     * @param buffer_size  size of buffer
     * @param src_addr     filled with the sender's address
     * @param timeout_ms   negative: wait until a datagram arrives;
     *                     otherwise wait at most this many milliseconds
     * @return number of bytes received, 0 on timeout, -1 on error
     */
    ssize_t recvFrom(void* buffer, size_t buffer_size, struct sockaddr_in& src_addr,
                     int timeout_ms = -1);

    /**
     * @brief Close the socket (also done by the destructor).
     */
    void close();

    /**
     * @brief Whether the socket was created successfully and is still open.
     */
    bool isValid() const { return sockfd_ != INVALID_SOCKET_FD; }

    /**
     * @brief Whether bind() succeeded.
     */
    bool isBound() const { return bound_; }

    /**
     * @brief Local port the socket is bound to (0 if unknown).
     */
    uint16_t localPort() const;

    /**
     * @brief Build an IPv4 address from a dotted-quad string and a port.
     * @return false if ip is not a valid IPv4 address
     */
    static bool createAddress(const std::string& ip, uint16_t port, struct sockaddr_in& addr);

    /**
     * @brief "a.b.c.d:port" form of an address, for logging.
     */
    static std::string addressToString(const struct sockaddr_in& addr);

private:
    socket_t sockfd_;
    bool bound_;

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
};

#endif // UDP_SOCKET_H
