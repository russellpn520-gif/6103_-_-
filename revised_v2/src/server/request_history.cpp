/**
 * @file request_history.cpp
 * @brief Reply history keyed by (client address, request id).
 */

#include "server/request_history.h"
#include "common/logger.h"
#include <ctime>

RequestHistory::Key RequestHistory::makeKey(const struct sockaddr_in& client_addr, uint32_t request_id) {
    return Key(static_cast<uint32_t>(client_addr.sin_addr.s_addr),
               static_cast<uint16_t>(client_addr.sin_port),
               request_id);
}

void RequestHistory::addEntry(const struct sockaddr_in& client_addr, uint32_t request_id,
                              const std::vector<uint8_t>& response) {
    HistoryEntry entry;
    entry.request_id = request_id;
    entry.response_data = response;
    entry.timestamp = time(nullptr);

    history_[makeKey(client_addr, request_id)] = entry;
}

bool RequestHistory::findEntry(const struct sockaddr_in& client_addr, uint32_t request_id,
                               std::vector<uint8_t>& response) const {
    auto it = history_.find(makeKey(client_addr, request_id));
    if (it == history_.end()) {
        return false;
    }

    response = it->second.response_data;
    return true;
}

void RequestHistory::cleanup() {
    const time_t now = time(nullptr);

    auto it = history_.begin();
    while (it != history_.end()) {
        if (now - it->second.timestamp > EXPIRE_SECONDS) {
            it = history_.erase(it);
        } else {
            ++it;
        }
    }
}
