/**
 * @file request_history.h
 * @brief Reply history used to recognise duplicate (retransmitted) requests.
 */

#ifndef REQUEST_HISTORY_H
#define REQUEST_HISTORY_H

#include <map>
#include <tuple>
#include <vector>
#include <cstdint>
#include "common/data_structures.h"

/**
 * @brief Remembers the reply sent for each executed request.
 *
 * A request is identified by (client IP, client port, request id). Request ids
 * are only unique within one client (every client counts from 1), so the
 * client's address must be part of the key; otherwise request #1 of one client
 * would be mistaken for a duplicate of request #1 of another client.
 *
 * Entries are kept for EXPIRE_SECONDS, which must be longer than a client's
 * whole retransmission window (timeout x number of attempts).
 */
class RequestHistory {
public:
    static constexpr time_t EXPIRE_SECONDS = 60;

    RequestHistory() = default;
    ~RequestHistory() = default;

    /**
     * @brief Store the reply that was produced for a request.
     */
    void addEntry(const struct sockaddr_in& client_addr, uint32_t request_id, const std::vector<uint8_t>& response);

    /**
     * @brief Look up the stored reply of a request.
     * @return true if this request was executed before
     */
    bool findEntry(const struct sockaddr_in& client_addr, uint32_t request_id, std::vector<uint8_t>& response) const;

    /**
     * @brief Drop entries older than EXPIRE_SECONDS.
     */
    void cleanup();

private:
    // (IPv4 address, port, request id); address and port in network byte order
    typedef std::tuple<uint32_t, uint16_t, uint32_t> Key;

    static Key makeKey(const struct sockaddr_in& client_addr, uint32_t request_id);

    std::map<Key, HistoryEntry> history_;
};

#endif // REQUEST_HISTORY_H
