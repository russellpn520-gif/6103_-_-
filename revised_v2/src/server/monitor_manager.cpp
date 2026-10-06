/**
 * @file monitor_manager.cpp
 * @brief Registry of monitoring clients and callback delivery.
 */

#include "server/monitor_manager.h"
#include "common/message_serializer.h"
#include "common/logger.h"
#include <ctime>

namespace {

bool sameAddress(const struct sockaddr_in& a, const struct sockaddr_in& b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

} // namespace

void MonitorManager::registerMonitor(int32_t flight_id, int32_t interval_seconds, const struct sockaddr_in& client_addr) {
    const time_t expire_time = time(nullptr) + interval_seconds;
    const std::string client = UdpSocket::addressToString(client_addr);

    for (auto& monitor : monitors_) {
        if (monitor.flight_id == flight_id && sameAddress(monitor.client_addr, client_addr)) {
            monitor.expire_time = expire_time;
            monitor.active = true;
            LOG_INFO("Monitor renewed: " + client + " watches flight " + std::to_string(flight_id) +
                     " for " + std::to_string(interval_seconds) + " s");
            return;
        }
    }

    MonitorEntry entry;
    entry.flight_id = flight_id;
    entry.client_addr = client_addr;
    entry.expire_time = expire_time;
    entry.active = true;
    monitors_.push_back(entry);

    LOG_INFO("Monitor registered: " + client + " watches flight " + std::to_string(flight_id) +
             " for " + std::to_string(interval_seconds) + " s (" + std::to_string(monitors_.size()) +
             " active monitor(s))");
}

void MonitorManager::notifyMonitors(UdpSocket& socket, int32_t flight_id, int32_t new_seat_count) {
    const time_t now = time(nullptr);

    for (const auto& monitor : monitors_) {
        if (!monitor.active || monitor.flight_id != flight_id || now >= monitor.expire_time) {
            continue;
        }

        int32_t remaining_time = static_cast<int32_t>(monitor.expire_time - now);
        std::vector<uint8_t> buffer;
        MessageSerializer::marshalCallbackUpdate(flight_id, new_seat_count, remaining_time, buffer);
        socket.sendTo(buffer.data(), buffer.size(), monitor.client_addr);

        LOG_INFO("Callback -> " + UdpSocket::addressToString(monitor.client_addr) + ": flight " +
                 std::to_string(flight_id) + " now has " + std::to_string(new_seat_count) +
                 " seats (monitor expires in " + std::to_string(remaining_time) + " s)");
    }
}

void MonitorManager::cleanupExpired() {
    const time_t now = time(nullptr);

    auto it = monitors_.begin();
    while (it != monitors_.end()) {
        if (now >= it->expire_time) {
            LOG_INFO("Monitor expired: " + UdpSocket::addressToString(it->client_addr) +
                     " no longer receives updates of flight " + std::to_string(it->flight_id));
            it = monitors_.erase(it);
        } else {
            ++it;
        }
    }
}
