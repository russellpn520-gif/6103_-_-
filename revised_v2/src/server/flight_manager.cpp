/**
 * @file flight_manager.cpp
 * @brief In-memory flight database.
 */

#include "server/flight_manager.h"
#include "common/message_types.h"
#include "common/logger.h"

namespace {

void setError(int32_t* error, int32_t code) {
    if (error != nullptr) {
        *error = code;
    }
}

} // namespace

std::vector<int32_t> FlightManager::queryFlight(const std::string& source, const std::string& destination) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<int32_t> result;

    for (const auto& pair : flights_) {
        const Flight& flight = pair.second;
        if (flight.source == source && flight.destination == destination) {
            result.push_back(flight.flight_id);
        }
    }

    return result;
}

bool FlightManager::getFlightInfo(int32_t flight_id, Flight& flight) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = flights_.find(flight_id);
    if (it == flights_.end()) {
        return false;
    }

    flight = it->second;
    return true;
}

bool FlightManager::reserveSeats(int32_t flight_id, int32_t seat_count, int32_t& remaining, int32_t* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = flights_.find(flight_id);
    if (it == flights_.end()) {
        remaining = 0;
        setError(error, ERR_FLIGHT_NOT_FOUND);
        return false;
    }

    Flight& flight = it->second;
    remaining = flight.seat_availability;

    if (seat_count <= 0) {
        setError(error, ERR_INVALID_SEAT_COUNT);
        return false;
    }

    if (flight.seat_availability < seat_count) {
        setError(error, ERR_INSUFFICIENT_SEATS);
        return false;
    }

    flight.seat_availability -= seat_count;
    remaining = flight.seat_availability;
    setError(error, ERR_SUCCESS);
    return true;
}

bool FlightManager::getSeatCount(int32_t flight_id, int32_t& count) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = flights_.find(flight_id);
    if (it == flights_.end()) {
        return false;
    }

    count = it->second.seat_availability;
    return true;
}

bool FlightManager::cancelReservation(int32_t flight_id, int32_t seat_count, int32_t& new_count, int32_t* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = flights_.find(flight_id);
    if (it == flights_.end()) {
        new_count = 0;
        setError(error, ERR_FLIGHT_NOT_FOUND);
        return false;
    }

    Flight& flight = it->second;
    new_count = flight.seat_availability;

    if (seat_count <= 0) {
        setError(error, ERR_INVALID_SEAT_COUNT);
        return false;
    }

    // Seats can only be given back if they were reserved before; without this
    // check a cancellation could push the availability above the capacity.
    int32_t reserved = capacity_[flight_id] - flight.seat_availability;
    if (seat_count > reserved) {
        setError(error, ERR_CANCEL_EXCEEDS_RESERVED);
        return false;
    }

    flight.seat_availability += seat_count;
    new_count = flight.seat_availability;
    setError(error, ERR_SUCCESS);
    return true;
}

void FlightManager::addFlight(const Flight& flight) {
    std::lock_guard<std::mutex> lock(mutex_);
    flights_[flight.flight_id] = flight;
    capacity_[flight.flight_id] = flight.seat_availability;

    char fare[32];
    snprintf(fare, sizeof(fare), "%.2f", flight.airfare);
    LOG_INFO("  Flight " + std::to_string(flight.flight_id) + ": " + flight.source + " -> " + flight.destination +
             ", departs " + flight.departure_time.toString() + ", airfare " + fare +
             ", " + std::to_string(flight.seat_availability) + " seats");
}

void FlightManager::initTestData() {
    LOG_INFO("Loading test flights:");
    addFlight(Flight(1001, "Beijing", "Shanghai", Time(2026, 10, 20, 8, 30), 800.50f, 120));
    addFlight(Flight(1002, "Beijing", "Shanghai", Time(2026, 10, 20, 10, 0), 850.00f, 100));
    addFlight(Flight(1003, "Beijing", "Shanghai", Time(2026, 10, 20, 14, 30), 900.00f, 80));
    addFlight(Flight(2001, "Shanghai", "Beijing", Time(2026, 10, 21, 9, 0), 820.00f, 110));
    addFlight(Flight(2002, "Shanghai", "Beijing", Time(2026, 10, 21, 15, 0), 870.50f, 90));
    addFlight(Flight(3001, "Guangzhou", "Chengdu", Time(2026, 10, 22, 7, 30), 1200.00f, 150));
    addFlight(Flight(3002, "Guangzhou", "Chengdu", Time(2026, 10, 22, 13, 0), 1250.00f, 130));
    addFlight(Flight(4001, "Shenzhen", "Xi'an", Time(2026, 10, 23, 11, 0), 1100.00f, 100));
    addFlight(Flight(5001, "Hangzhou", "Wuhan", Time(2026, 10, 24, 16, 0), 700.00f, 140));
    addFlight(Flight(6001, "Nanjing", "Kunming", Time(2026, 10, 25, 12, 30), 1500.00f, 120));

    LOG_INFO("Initialized " + std::to_string(flights_.size()) + " test flights");
}
