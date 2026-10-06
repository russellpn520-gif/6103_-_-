/**
 * @file flight_client.h
 * @brief Client-side stubs: marshal a request, send it with timeout and
 *        retransmission, and unmarshal the reply.
 */

#ifndef FLIGHT_CLIENT_H
#define FLIGHT_CLIENT_H

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <random>
#include "common/message_types.h"
#include "common/udp_socket.h"
#include "common/data_structures.h"

/**
 * @brief One client of the flight server. Every service method returns a
 *        status code: ERR_SUCCESS, an error from the server, or ERR_NO_REPLY /
 *        ERR_MALFORMED_REPLY produced locally.
 */
class FlightClient {
public:
    FlightClient(const std::string& server_ip, uint16_t server_port);
    ~FlightClient();

    /**
     * @brief false if the socket could not be created or the address is invalid.
     */
    bool isValid() const { return valid_; }

    /**
     * @brief Local UDP port of this client (callbacks are sent to it).
     */
    uint16_t localPort() const { return socket_.localPort(); }

    // Service 1: flight ids from source to destination
    int32_t queryFlight(const std::string& source, const std::string& destination,
                        std::vector<int32_t>& flight_ids);

    // Service 2: departure time, airfare and seat availability of a flight
    int32_t getFlightInfo(int32_t flight_id, Flight& flight);

    // Service 3: reserve seats (non-idempotent)
    int32_t reserveSeats(int32_t flight_id, int32_t seat_count, int32_t& remaining_seats);

    // Service 4, step 1: register for callbacks on a flight
    int32_t registerMonitor(int32_t flight_id, int32_t interval_seconds);

    /**
     * @brief Service 4, step 2: block for interval_seconds and pass every
     *        callback received to on_update(flight_id, seats, remaining_seconds).
     */
    void waitForCallbacks(int32_t interval_seconds,
                          const std::function<void(int32_t, int32_t, int32_t)>& on_update);

    // Service 5: seat availability only (idempotent)
    int32_t getSeatCount(int32_t flight_id, int32_t& count);

    // Service 6: give back reserved seats (non-idempotent)
    int32_t cancelReservation(int32_t flight_id, int32_t seat_count, int32_t& new_count);

    /**
     * @brief MAYBE sends each request once; AT_LEAST_ONCE and AT_MOST_ONCE
     *        retransmit after a timeout (the server makes the difference).
     */
    void setSemantics(Semantics semantics);

    /**
     * @brief Probability (0..1) that an outgoing request is dropped, to
     *        simulate request loss.
     */
    void setLossRate(double rate);

    /**
     * @brief How long to wait for a reply before retransmitting.
     */
    void setTimeoutMs(int timeout_ms);

    /**
     * @brief Number of retransmissions after the first attempt.
     */
    void setMaxRetries(int max_retries);

    /**
     * @brief Seed the loss simulation so an experiment can be repeated.
     */
    void setSeed(uint32_t seed);

private:
    /**
     * @brief Send a marshalled request and wait for the matching reply,
     *        retransmitting on timeout unless the semantics is MAYBE.
     *        Messages whose type or request id do not match (e.g. a late
     *        reply to an earlier request) are discarded.
     * @return ERR_SUCCESS with the reply in `reply`, or ERR_NO_REPLY
     */
    int32_t invoke(const std::vector<uint8_t>& request, uint32_t reply_type, std::vector<uint8_t>& reply);

    /**
     * @brief invoke() followed by unmarshal(reply), which returns the status
     *        code from the reply; malformed replies give ERR_MALFORMED_REPLY.
     */
    int32_t call(const std::vector<uint8_t>& request, uint32_t reply_type,
                 const std::function<int32_t(const std::vector<uint8_t>&)>& unmarshal);

    uint32_t generateRequestId();
    bool shouldSimulateLoss();

    UdpSocket socket_;
    struct sockaddr_in server_addr_;
    bool valid_;
    uint32_t request_id_counter_;
    Semantics semantics_;
    double loss_rate_;
    int timeout_ms_;
    int max_retries_;
    std::mt19937 rng_;
};

#endif // FLIGHT_CLIENT_H
