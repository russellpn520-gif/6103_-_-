/**
 * @file data_structures.h
 * @brief Core data structures shared by the client and the server.
 */

#ifndef DATA_STRUCTURES_H
#define DATA_STRUCTURES_H

#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>
#include "common/platform.h"

/**
 * @brief Departure time with minute precision (our own time representation).
 *
 * On the wire a Time is five consecutive 32-bit big-endian integers
 * (year, month, day, hour, minute), i.e. WIRE_SIZE = 20 bytes.
 */
struct Time {
    int32_t year;
    int32_t month;    // 1-12
    int32_t day;      // 1-31
    int32_t hour;     // 0-23
    int32_t minute;   // 0-59

    static constexpr size_t WIRE_SIZE = 20;

    Time() : year(0), month(0), day(0), hour(0), minute(0) {}

    Time(int32_t y, int32_t m, int32_t d, int32_t h, int32_t min)
        : year(y), month(m), day(d), hour(h), minute(min) {}

    /**
     * @brief Format as "YYYY-MM-DD HH:MM".
     */
    std::string toString() const {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d",
                 year, month, day, hour, minute);
        return std::string(buffer);
    }

    /**
     * @brief Write the 20-byte network representation into buffer
     *        (buffer must have room for WIRE_SIZE bytes).
     */
    void serialize(uint8_t* buffer) const;

    /**
     * @brief Read the 20-byte network representation from buffer.
     */
    void deserialize(const uint8_t* buffer);
};

/**
 * @brief One flight as stored at the server.
 */
struct Flight {
    int32_t flight_id;           // flight identifier
    std::string source;          // departure place (variable length)
    std::string destination;     // arrival place (variable length)
    Time departure_time;         // departure time
    float airfare;               // airfare (IEEE-754 single precision on the wire)
    int32_t seat_availability;   // number of seats still available

    Flight() : flight_id(0), airfare(0.0f), seat_availability(0) {}

    Flight(int32_t id, const std::string& src, const std::string& dest,
           const Time& time, float fare, int32_t seats)
        : flight_id(id), source(src), destination(dest),
          departure_time(time), airfare(fare), seat_availability(seats) {}
};

/**
 * @brief A client registered for seat-availability callbacks on one flight.
 */
struct MonitorEntry {
    int32_t flight_id;                    // flight being monitored
    struct sockaddr_in client_addr;       // where callbacks are sent (taken from the register request)
    time_t expire_time;                   // end of the monitor interval (Unix time)
    bool active;                          // false once the entry should no longer receive callbacks

    MonitorEntry() : flight_id(0), expire_time(0), active(false) {
        memset(&client_addr, 0, sizeof(client_addr));
    }
};

/**
 * @brief Reply history entry used for duplicate filtering (at-most-once).
 */
struct HistoryEntry {
    uint32_t request_id;                  // request id chosen by the client
    std::vector<uint8_t> response_data;   // marshalled reply that was sent for this request
    time_t timestamp;                     // when the request was executed (for expiry)

    HistoryEntry() : request_id(0), timestamp(0) {}
};

#endif // DATA_STRUCTURES_H
