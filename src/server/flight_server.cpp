/**
 * @file flight_server.cpp
 * @brief UDP request loop, duplicate handling (invocation semantics) and the
 *        six services.
 */

#include "server/flight_server.h"
#include "common/message_serializer.h"
#include "common/logger.h"
#include <cstdio>
#include <ctime>
#include <stdexcept>

namespace {

void logRequest(uint32_t request_id, const std::string& client, const std::string& text) {
    LOG_INFO("Request #" + std::to_string(request_id) + " from " + client + ": " + text);
}

void logReply(uint32_t request_id, const std::string& client, int32_t status, const std::string& details = "") {
    std::string text = "Reply   #" + std::to_string(request_id) + " to   " + client + ": " + getErrorMessage(status);
    if (!details.empty()) {
        text += ", " + details;
    }
    LOG_INFO(text);
}

std::string joinIds(const std::vector<int32_t>& ids) {
    std::string text;
    for (size_t i = 0; i < ids.size(); i++) {
        if (i > 0) {
            text += ", ";
        }
        text += std::to_string(ids[i]);
    }
    return text;
}

std::string formatFare(float fare) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.2f", fare);
    return buffer;
}

} // namespace

FlightServer::FlightServer(uint16_t port)
    : port_(port), running_(false), semantics_(Semantics::AT_MOST_ONCE), loss_rate_(0.0),
      rng_(static_cast<uint32_t>(time(nullptr))) {
}

FlightServer::~FlightServer() {
    stop();
}

bool FlightServer::start() {
    if (!socket_.bind(port_)) {
        return false;
    }

    running_ = true;
    LOG_INFO("Server listening on UDP port " + std::to_string(port_));

    flight_manager_.initTestData();

    requestLoop();
    return true;
}

void FlightServer::stop() {
    running_ = false;
    socket_.close();
}

void FlightServer::addFlight(const Flight& flight) {
    flight_manager_.addFlight(flight);
}

void FlightServer::setSemantics(Semantics semantics) {
    semantics_ = semantics;
}

void FlightServer::setLossRate(double rate) {
    loss_rate_ = rate;
}

void FlightServer::setSeed(uint32_t seed) {
    rng_.seed(seed);
}

void FlightServer::requestLoop() {
    uint8_t buffer[65536];
    struct sockaddr_in client_addr;

    while (running_) {
        // Wake up at least once per second so expired monitors are removed on
        // time even when no requests arrive.
        ssize_t received = socket_.recvFrom(buffer, sizeof(buffer), client_addr, 1000);

        if (received > 0) {
            std::vector<uint8_t> request(buffer, buffer + received);
            handleRequest(request, client_addr);
        }

        monitor_manager_.cleanupExpired();
        history_.cleanup();
    }
}

void FlightServer::handleRequest(const std::vector<uint8_t>& request, const struct sockaddr_in& client_addr) {
    const std::string client = UdpSocket::addressToString(client_addr);

    if (request.size() < MessageHeader::SIZE) {
        LOG_WARNING("Dropped " + std::to_string(request.size()) + "-byte datagram from " + client +
                    ": shorter than a message header");
        return;
    }

    uint32_t message_type, request_id, message_length;
    MessageSerializer::readHeader(request, message_type, request_id, message_length);

    if (message_length != request.size()) {
        LOG_WARNING("Dropped malformed request from " + client + ": length field " +
                    std::to_string(message_length) + " but " + std::to_string(request.size()) + " bytes received");
        return;
    }

    // ---- Duplicate detection --------------------------------------------------
    // A retransmitted request carries the same request id from the same client
    // address as the original. Under at-most-once it is answered from the reply
    // history and NOT executed again. Under at-least-once there is no duplicate
    // filtering: the history is only consulted to point out the re-execution in
    // the log, and the request is executed like any other.
    std::vector<uint8_t> stored_reply;
    if (history_.findEntry(client_addr, request_id, stored_reply)) {
        if (semantics_ == Semantics::AT_MOST_ONCE) {
            LOG_INFO("Duplicate " + std::string(getMessageTypeName(message_type)) + " #" +
                     std::to_string(request_id) + " from " + client +
                     ": already executed, re-sending the stored reply (at-most-once: NOT re-executed)");
            sendReply(stored_reply, request_id, client_addr, client);
            return;
        }
        LOG_WARNING("Duplicate " + std::string(getMessageTypeName(message_type)) + " #" +
                    std::to_string(request_id) + " from " + client +
                    ": was already executed, executing AGAIN (at-least-once: no duplicate filtering)");
    }

    // ---- Execution ----------------------------------------------------------
    std::vector<uint8_t> reply;
    try {
        if (!execute(message_type, request, client_addr, client, reply)) {
            return;
        }
    } catch (const std::exception& e) {
        LOG_WARNING("Dropped malformed request #" + std::to_string(request_id) + " from " + client + ": " + e.what());
        return;
    }

    history_.addEntry(client_addr, request_id, reply);
    sendReply(reply, request_id, client_addr, client);
}

bool FlightServer::execute(uint32_t message_type, const std::vector<uint8_t>& request,
                           const struct sockaddr_in& client_addr, const std::string& client,
                           std::vector<uint8_t>& reply) {
    uint32_t request_id = 0;

    switch (message_type) {
        // Service 1: flight ids for a source/destination pair (idempotent)
        case MSG_QUERY_FLIGHT_REQUEST: {
            std::string source, destination;
            MessageSerializer::unmarshalQueryFlightRequest(request, request_id, source, destination);
            logRequest(request_id, client, "QUERY_FLIGHT source=\"" + source + "\" destination=\"" + destination + "\"");

            std::vector<int32_t> flight_ids;
            int32_t status = ERR_INVALID_INPUT;
            if (!source.empty() && !destination.empty()) {
                flight_ids = flight_manager_.queryFlight(source, destination);
                status = flight_ids.empty() ? ERR_NO_FLIGHTS_FOUND : ERR_SUCCESS;
            }

            MessageSerializer::marshalQueryFlightResponse(request_id, status, flight_ids, reply);
            logReply(request_id, client, status, status == ERR_SUCCESS ? "flights [" + joinIds(flight_ids) + "]" : "");
            return true;
        }

        // Service 2: departure time, airfare and seat availability (idempotent)
        case MSG_GET_FLIGHT_INFO_REQUEST: {
            int32_t flight_id;
            MessageSerializer::unmarshalGetFlightInfoRequest(request, request_id, flight_id);
            logRequest(request_id, client, "GET_FLIGHT_INFO flight=" + std::to_string(flight_id));

            Flight flight;
            int32_t status = flight_manager_.getFlightInfo(flight_id, flight) ? ERR_SUCCESS : ERR_FLIGHT_NOT_FOUND;

            MessageSerializer::marshalGetFlightInfoResponse(request_id, status, flight, reply);
            logReply(request_id, client, status, status != ERR_SUCCESS ? "" :
                     flight.source + " -> " + flight.destination + ", departs " + flight.departure_time.toString() +
                     ", airfare " + formatFare(flight.airfare) + ", seats " + std::to_string(flight.seat_availability));
            return true;
        }

        // Service 3: reserve seats (NON-idempotent: each execution removes seats)
        case MSG_RESERVE_SEATS_REQUEST: {
            int32_t flight_id, seat_count;
            MessageSerializer::unmarshalReserveSeatsRequest(request, request_id, flight_id, seat_count);
            logRequest(request_id, client, "RESERVE_SEATS flight=" + std::to_string(flight_id) +
                       " seats=" + std::to_string(seat_count));

            int32_t remaining = 0;
            int32_t status = ERR_SUCCESS;
            bool reserved = flight_manager_.reserveSeats(flight_id, seat_count, remaining, &status);

            MessageSerializer::marshalReserveSeatsResponse(request_id, status, remaining, reply);
            logReply(request_id, client, status,
                     status == ERR_FLIGHT_NOT_FOUND ? "" : "seats left " + std::to_string(remaining));

            if (reserved) {
                monitor_manager_.notifyMonitors(socket_, flight_id, remaining);
            }
            return true;
        }

        // Service 4: register for seat-availability callbacks
        case MSG_REGISTER_MONITOR_REQUEST: {
            int32_t flight_id, interval_seconds;
            MessageSerializer::unmarshalRegisterMonitorRequest(request, request_id, flight_id, interval_seconds);
            logRequest(request_id, client, "REGISTER_MONITOR flight=" + std::to_string(flight_id) +
                       " interval=" + std::to_string(interval_seconds) + "s");

            Flight flight;
            int32_t status = ERR_SUCCESS;
            if (!flight_manager_.getFlightInfo(flight_id, flight)) {
                status = ERR_FLIGHT_NOT_FOUND;
            } else if (interval_seconds <= 0 || interval_seconds > MAX_MONITOR_INTERVAL_SECONDS) {
                status = ERR_INVALID_MONITOR_INTERVAL;
            }

            if (status == ERR_SUCCESS) {
                // The address the request came from is where callbacks will go.
                monitor_manager_.registerMonitor(flight_id, interval_seconds, client_addr);
            }

            MessageSerializer::marshalRegisterMonitorResponse(request_id, status, reply);
            logReply(request_id, client, status);
            return true;
        }

        // Service 5: seat availability only (idempotent)
        case MSG_GET_SEAT_COUNT_REQUEST: {
            int32_t flight_id;
            MessageSerializer::unmarshalGetSeatCountRequest(request, request_id, flight_id);
            logRequest(request_id, client, "GET_SEAT_COUNT flight=" + std::to_string(flight_id));

            int32_t seat_count = 0;
            int32_t status = flight_manager_.getSeatCount(flight_id, seat_count) ? ERR_SUCCESS : ERR_FLIGHT_NOT_FOUND;

            MessageSerializer::marshalGetSeatCountResponse(request_id, status, seat_count, reply);
            logReply(request_id, client, status,
                     status == ERR_SUCCESS ? "seat count " + std::to_string(seat_count) : "");
            return true;
        }

        // Service 6: give back reserved seats (NON-idempotent: each execution adds seats)
        case MSG_CANCEL_RESERVATION_REQUEST: {
            int32_t flight_id, seat_count;
            MessageSerializer::unmarshalCancelReservationRequest(request, request_id, flight_id, seat_count);
            logRequest(request_id, client, "CANCEL_RESERVATION flight=" + std::to_string(flight_id) +
                       " seats=" + std::to_string(seat_count));

            int32_t new_count = 0;
            int32_t status = ERR_SUCCESS;
            bool cancelled = flight_manager_.cancelReservation(flight_id, seat_count, new_count, &status);

            MessageSerializer::marshalCancelReservationResponse(request_id, status, new_count, reply);
            logReply(request_id, client, status,
                     status == ERR_FLIGHT_NOT_FOUND ? "" : "seats available " + std::to_string(new_count));

            if (cancelled) {
                monitor_manager_.notifyMonitors(socket_, flight_id, new_count);
            }
            return true;
        }

        default:
            LOG_WARNING("Ignored message from " + client + ": unknown message type " + std::to_string(message_type));
            return false;
    }
}

void FlightServer::sendReply(const std::vector<uint8_t>& reply, uint32_t request_id,
                             const struct sockaddr_in& client_addr, const std::string& client) {
    if (shouldSimulateLoss()) {
        LOG_WARNING("[Simulated loss] reply #" + std::to_string(request_id) + " to " + client + " dropped");
        return;
    }

    socket_.sendTo(reply.data(), reply.size(), client_addr);
}

bool FlightServer::shouldSimulateLoss() {
    if (loss_rate_ <= 0.0) return false;
    if (loss_rate_ >= 1.0) return true;

    // mt19937 produces the same sequence on every platform for a given seed,
    // so a seeded experiment is repeatable.
    double random = static_cast<double>(rng_()) / 4294967296.0;
    return random < loss_rate_;
}
