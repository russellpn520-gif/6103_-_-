/**
 * @file client_ui.cpp
 * @brief Text menu of the client.
 */

#include "client/client_ui.h"
#include "common/cli_options.h"
#include "common/message_types.h"
#include <cstdio>
#include <iostream>

namespace {

void printError(int32_t status) {
    std::cout << "Error: " << getErrorMessage(status) << "\n";
}

std::string formatFare(float fare) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.2f", fare);
    return buffer;
}

std::string trim(const std::string& text) {
    const char* whitespace = " \t\r\n";
    size_t begin = text.find_first_not_of(whitespace);
    if (begin == std::string::npos) {
        return "";
    }
    size_t end = text.find_last_not_of(whitespace);
    return text.substr(begin, end - begin + 1);
}

} // namespace

ClientUI::ClientUI(std::shared_ptr<FlightClient> client) : client_(client), input_closed_(false) {}

void ClientUI::run() {
    while (!input_closed_) {
        showMenu();

        int32_t choice;
        if (!readInt("Enter your choice (0-6): ", choice)) {
            break;
        }

        switch (choice) {
            case 0:
                std::cout << "Exiting...\n";
                return;
            case 1:
                handleQueryFlight();
                break;
            case 2:
                handleGetFlightInfo();
                break;
            case 3:
                handleReserveSeats();
                break;
            case 4:
                handleGetSeatCount();
                break;
            case 5:
                handleCancelReservation();
                break;
            case 6:
                handleRegisterMonitor();
                break;
            default:
                std::cout << "Invalid choice!\n";
        }

        std::cout << "\n";
    }
}

void ClientUI::showMenu() {
    std::cout << "\n========== Flight Information System ==========\n"
              << "1. Query flights by source and destination\n"
              << "2. Get flight information\n"
              << "3. Reserve seats\n"
              << "4. Get seat count\n"
              << "5. Cancel reservation\n"
              << "6. Monitor seat availability\n"
              << "0. Exit\n"
              << "===============================================\n";
}

void ClientUI::handleQueryFlight() {
    std::string source, destination;
    if (!readNonEmpty("Enter source: ", source) || !readNonEmpty("Enter destination: ", destination)) {
        return;
    }

    std::vector<int32_t> flight_ids;
    int32_t status = client_->queryFlight(source, destination, flight_ids);

    if (status == ERR_SUCCESS) {
        std::cout << "Found " << flight_ids.size() << " flights:\n";
        for (int32_t id : flight_ids) {
            std::cout << "  Flight " << id << "\n";
        }
    } else {
        printError(status);
    }
}

void ClientUI::handleGetFlightInfo() {
    int32_t flight_id;
    if (!readInt("Enter flight ID: ", flight_id)) {
        return;
    }

    Flight flight;
    int32_t status = client_->getFlightInfo(flight_id, flight);

    if (status == ERR_SUCCESS) {
        std::cout << "Flight Information:\n"
                  << "  Flight ID: " << flight.flight_id << "\n"
                  << "  Source: " << flight.source << "\n"
                  << "  Destination: " << flight.destination << "\n"
                  << "  Departure: " << flight.departure_time.toString() << "\n"
                  << "  Airfare: " << formatFare(flight.airfare) << "\n"
                  << "  Available Seats: " << flight.seat_availability << "\n";
    } else {
        printError(status);
    }
}

void ClientUI::handleReserveSeats() {
    int32_t flight_id, seat_count;
    if (!readInt("Enter flight ID: ", flight_id) || !readInt("Enter number of seats: ", seat_count)) {
        return;
    }

    int32_t remaining = 0;
    int32_t status = client_->reserveSeats(flight_id, seat_count, remaining);

    if (status == ERR_SUCCESS) {
        std::cout << "Reservation successful! Remaining seats: " << remaining << "\n";
    } else if (status == ERR_INSUFFICIENT_SEATS) {
        std::cout << "Error: " << getErrorMessage(status) << " (only " << remaining << " seat(s) available)\n";
    } else {
        printError(status);
    }
}

void ClientUI::handleGetSeatCount() {
    int32_t flight_id;
    if (!readInt("Enter flight ID: ", flight_id)) {
        return;
    }

    int32_t count = 0;
    int32_t status = client_->getSeatCount(flight_id, count);

    if (status == ERR_SUCCESS) {
        std::cout << "Available seats: " << count << "\n";
    } else {
        printError(status);
    }
}

void ClientUI::handleCancelReservation() {
    int32_t flight_id, seat_count;
    if (!readInt("Enter flight ID: ", flight_id) || !readInt("Enter number of seats to cancel: ", seat_count)) {
        return;
    }

    int32_t new_count = 0;
    int32_t status = client_->cancelReservation(flight_id, seat_count, new_count);

    if (status == ERR_SUCCESS) {
        std::cout << "Cancellation successful! Available seats: " << new_count << "\n";
    } else {
        printError(status);
    }
}

void ClientUI::handleRegisterMonitor() {
    int32_t flight_id, interval;
    if (!readInt("Enter flight ID: ", flight_id) || !readInt("Enter monitor interval (seconds): ", interval)) {
        return;
    }

    int32_t status = client_->registerMonitor(flight_id, interval);
    if (status != ERR_SUCCESS) {
        printError(status);
        return;
    }

    std::cout << "Monitor registered for flight " << flight_id << " for " << interval << " s.\n"
              << "Waiting for updates (input is blocked until the monitor interval expires)...\n"
              << std::flush;

    client_->waitForCallbacks(interval, [](int32_t id, int32_t seat_count, int32_t remaining_time) {
        std::cout << "[Callback] Flight " << id << " seat availability is now " << seat_count
                  << " (monitor expires in " << remaining_time << " s)\n" << std::flush;
    });

    std::cout << "Monitor interval expired, stopped monitoring flight " << flight_id << ".\n";
}

bool ClientUI::readLine(const std::string& prompt, std::string& line) {
    std::cout << prompt << std::flush;
    if (!std::getline(std::cin, line)) {
        input_closed_ = true;
        std::cout << "\n";
        return false;
    }
    line = trim(line);
    return true;
}

bool ClientUI::readNonEmpty(const std::string& prompt, std::string& line) {
    while (readLine(prompt, line)) {
        if (!line.empty()) {
            return true;
        }
        std::cout << "Please enter a value.\n";
    }
    return false;
}

bool ClientUI::readInt(const std::string& prompt, int32_t& value) {
    std::string line;
    while (readLine(prompt, line)) {
        long long number;
        if (cli::parseInteger(line, INT32_MIN, INT32_MAX, number)) {
            value = static_cast<int32_t>(number);
            return true;
        }
        std::cout << "Please enter a whole number.\n";
    }
    return false;
}
