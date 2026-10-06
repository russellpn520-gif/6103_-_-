/**
 * @file monitor_manager.h
 * @brief Registry of clients monitoring seat availability (service 4).
 */

#ifndef MONITOR_MANAGER_H
#define MONITOR_MANAGER_H

#include <vector>
#include <cstdint>
#include "common/data_structures.h"
#include "common/udp_socket.h"

/**
 * @brief Keeps the (client address, flight, expiry time) records of all
 *        monitoring clients and sends them callbacks.
 */
class MonitorManager {
public:
    MonitorManager() = default;
    ~MonitorManager() = default;

    /**
     * @brief Record that client_addr wants updates of flight_id for the next
     *        interval_seconds. A repeated registration from the same address
     *        for the same flight renews the existing record instead of adding
     *        a second one, so the client never gets duplicate callbacks.
     */
    void registerMonitor(int32_t flight_id, int32_t interval_seconds, const struct sockaddr_in& client_addr);

    /**
     * @brief Send the new seat availability of flight_id to every client whose
     *        monitor interval has not expired. Callbacks are sent from the
     *        server's own socket, i.e. from the address the client talks to.
     */
    void notifyMonitors(UdpSocket& socket, int32_t flight_id, int32_t new_seat_count);

    /**
     * @brief Remove the records whose monitor interval has expired.
     */
    void cleanupExpired();

private:
    std::vector<MonitorEntry> monitors_;
};

#endif // MONITOR_MANAGER_H
