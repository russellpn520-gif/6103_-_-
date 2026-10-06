/**
 * @file platform.h
 * @brief Small portability layer over the socket APIs.
 *
 * macOS/Linux use BSD sockets and Windows uses Winsock2. The two APIs are
 * almost identical; the few differences (headers, socket handle type, how a
 * socket is closed, how errors are reported, and the one-time WSAStartup()
 * call on Windows) are hidden here so the rest of the code is written once
 * against the BSD-style API.
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <string>
#include <cstring>
#include <cerrno>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600   // Windows Vista or later: needed for inet_pton()/inet_ntop()
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifdef _MSC_VER
        #include <BaseTsd.h>
        typedef SSIZE_T ssize_t;
        #pragma comment(lib, "ws2_32.lib")
    #endif

    typedef SOCKET socket_t;
    constexpr socket_t INVALID_SOCKET_FD = INVALID_SOCKET;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <sys/time.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>

    typedef int socket_t;
    constexpr socket_t INVALID_SOCKET_FD = -1;
#endif

namespace platform {

/**
 * @brief Initialise the socket library once per process.
 *        Calls WSAStartup() on Windows; there is nothing to do on POSIX systems.
 * @return true if sockets can be used
 */
inline bool initSockets() {
#ifdef _WIN32
    static const bool initialised = [] {
        WSADATA wsa_data;
        return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    }();
    return initialised;
#else
    return true;
#endif
}

/**
 * @brief Close a socket handle (closesocket() on Windows, close() elsewhere).
 */
inline void closeSocket(socket_t fd) {
#ifdef _WIN32
    ::closesocket(fd);
#else
    ::close(fd);
#endif
}

/**
 * @brief Text describing the most recent socket error.
 */
inline std::string lastSocketError() {
#ifdef _WIN32
    return "Winsock error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

} // namespace platform

#endif // PLATFORM_H
