/**
 * @file client_ui.h
 * @brief Text menu of the client.
 */

#ifndef CLIENT_UI_H
#define CLIENT_UI_H

#include "client/flight_client.h"
#include <memory>
#include <string>

/**
 * @brief Repeatedly asks the user for a request, invokes it through the
 *        FlightClient and prints the reply or error. Ends on option 0 or
 *        at the end of standard input.
 */
class ClientUI {
public:
    explicit ClientUI(std::shared_ptr<FlightClient> client);
    ~ClientUI() = default;

    void run();

private:
    void showMenu();
    void handleQueryFlight();
    void handleGetFlightInfo();
    void handleReserveSeats();
    void handleGetSeatCount();
    void handleCancelReservation();
    void handleRegisterMonitor();

    /**
     * @brief Print prompt and read one whole line (so names may contain
     *        spaces). @return false at end of input
     */
    bool readLine(const std::string& prompt, std::string& line);

    /**
     * @brief Like readLine, but asks again until the line is not empty.
     */
    bool readNonEmpty(const std::string& prompt, std::string& line);

    /**
     * @brief Like readLine, but asks again until the line is a whole number.
     */
    bool readInt(const std::string& prompt, int32_t& value);

    std::shared_ptr<FlightClient> client_;
    bool input_closed_;
};

#endif // CLIENT_UI_H
