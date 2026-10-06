/**
 * @file flight_manager.h
 * @brief In-memory flight database and the operations on it.
 */

#ifndef FLIGHT_MANAGER_H
#define FLIGHT_MANAGER_H

#include <map>
#include <vector>
#include <string>
#include <mutex>
#include "common/data_structures.h"

/**
 * @brief Stores all flights in memory and implements the flight operations.
 *        Knows nothing about networking.
 */
class FlightManager {
public:
    FlightManager() = default;
    ~FlightManager() = default;

    /**
     * @brief Ids of all flights from source to destination (exact match).
     */
    std::vector<int32_t> queryFlight(const std::string& source, const std::string& destination);

    /**
     * @brief Copy of the flight with the given id.
     * @return false if there is no such flight
     */
    bool getFlightInfo(int32_t flight_id, Flight& flight);

    /**
     * @brief Reserve seats on a flight. Non-idempotent: every execution
     *        removes seat_count more seats.
     * @param remaining  seats left afterwards (current seats if rejected)
     * @param error      optional; receives ERR_SUCCESS, ERR_FLIGHT_NOT_FOUND,
     *                   ERR_INVALID_SEAT_COUNT or ERR_INSUFFICIENT_SEATS
     * @return true if the seats were reserved
     */
    bool reserveSeats(int32_t flight_id, int32_t seat_count, int32_t& remaining, int32_t* error = nullptr);

    /**
     * @brief Current seat availability of a flight.
     * @return false if there is no such flight
     */
    bool getSeatCount(int32_t flight_id, int32_t& count);

    /**
     * @brief Give back previously reserved seats. Non-idempotent: every
     *        execution adds seat_count more seats. At most the number of
     *        currently reserved seats (capacity - available) can be given back.
     * @param new_count  seat availability afterwards (current seats if rejected)
     * @param error      optional; receives ERR_SUCCESS, ERR_FLIGHT_NOT_FOUND,
     *                   ERR_INVALID_SEAT_COUNT or ERR_CANCEL_EXCEEDS_RESERVED
     * @return true if the seats were given back
     */
    bool cancelReservation(int32_t flight_id, int32_t seat_count, int32_t& new_count, int32_t* error = nullptr);

    /**
     * @brief Add (or replace) a flight. Its seat availability at this point
     *        is taken as the flight's capacity.
     */
    void addFlight(const Flight& flight);

    /**
     * @brief Load the sample flights used for testing and the demo.
     */
    void initTestData();

private:
    std::map<int32_t, Flight> flights_;     // flight id -> flight
    std::map<int32_t, int32_t> capacity_;   // flight id -> total number of seats
    std::mutex mutex_;
};

#endif // FLIGHT_MANAGER_H
