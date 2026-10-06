/**
 * @file flight_server.h
 * @brief UDP flight server: request loop, invocation semantics, services.
 */

#ifndef FLIGHT_SERVER_H
#define FLIGHT_SERVER_H

#include <cstdint>
#include <atomic>
#include <random>
#include <string>
#include "common/message_types.h"
#include "common/udp_socket.h"
#include "server/flight_manager.h"
#include "server/monitor_manager.h"
#include "server/request_history.h"

/**
 * @brief Single-threaded UDP server. Receives one request at a time, executes
 *        it (or, under at-most-once, answers a duplicate from the reply
 *        history) and sends the reply back to the sender's address.
 */
class FlightServer {
public:
    explicit FlightServer(uint16_t port);
    ~FlightServer();

    /**
     * @brief Bind the port, load the test flights and serve requests until
     *        stop() is called.
     * @return false if the port could not be bound
     */
    bool start();

    void stop();

    void addFlight(const Flight& flight);

    /**
     * @brief AT_MOST_ONCE filters duplicate requests; AT_LEAST_ONCE executes
     *        every request it receives.
     */
    void setSemantics(Semantics semantics);

    /**
     * @brief Probability (0..1) that an outgoing reply is dropped, to simulate
     *        reply loss.
     */
    void setLossRate(double rate);

    /**
     * @brief Seed the loss simulation so an experiment can be repeated.
     */
    void setSeed(uint32_t seed);

private:
    void requestLoop();
    void handleRequest(const std::vector<uint8_t>& request, const struct sockaddr_in& client_addr);

    /**
     * @brief Unmarshal and execute one request and marshal its reply.
     * @return false if the message type is unknown (nothing to reply)
     * @throws std::runtime_error if the request is malformed
     */
    bool execute(uint32_t message_type, const std::vector<uint8_t>& request,
                 const struct sockaddr_in& client_addr, const std::string& client,
                 std::vector<uint8_t>& reply);

    void sendReply(const std::vector<uint8_t>& reply, uint32_t request_id,
                   const struct sockaddr_in& client_addr, const std::string& client);
    bool shouldSimulateLoss();

    uint16_t port_;
    UdpSocket socket_;
    FlightManager flight_manager_;
    MonitorManager monitor_manager_;
    RequestHistory history_;
    std::atomic<bool> running_;
    Semantics semantics_;
    double loss_rate_;
    std::mt19937 rng_;
};

#endif // FLIGHT_SERVER_H
