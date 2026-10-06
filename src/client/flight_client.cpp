/**
 * @file flight_client.cpp
 * @brief Client-side request/reply protocol with timeout and retransmission.
 */

#include "client/flight_client.h"
#include "common/message_serializer.h"
#include "common/logger.h"
#include <chrono>
#include <cstring>
#include <ctime>
#include <stdexcept>

namespace {

typedef std::chrono::steady_clock Clock;

/** Milliseconds left until deadline (0 if it has passed). */
int millisecondsUntil(Clock::time_point deadline) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left > 0 ? static_cast<int>(left) : 0;
}

} // namespace

FlightClient::FlightClient(const std::string& server_ip, uint16_t server_port)
    : valid_(false), request_id_counter_(0), semantics_(Semantics::AT_MOST_ONCE), loss_rate_(0.0),
      timeout_ms_(2000), max_retries_(5), rng_(static_cast<uint32_t>(time(nullptr))) {

    memset(&server_addr_, 0, sizeof(server_addr_));

    // Bind to an OS-chosen port right away: replies and callbacks are sent to
    // this port, and on Windows receiving on a never-bound socket fails.
    valid_ = socket_.isValid() && socket_.bind(0) &&
             UdpSocket::createAddress(server_ip, server_port, server_addr_);
}

FlightClient::~FlightClient() {
    socket_.close();
}

// ============================================================
// Request/reply with timeout and retransmission
// ============================================================

int32_t FlightClient::invoke(const std::vector<uint8_t>& request, uint32_t reply_type,
                             std::vector<uint8_t>& reply) {
    uint32_t request_type, request_id, request_length;
    MessageSerializer::readHeader(request, request_type, request_id, request_length);
    const std::string tag = std::string(getMessageTypeName(request_type)) + " #" + std::to_string(request_id);

    // Under maybe semantics the request is sent once. Otherwise it is resent,
    // unchanged and with the same request id, each time the timeout expires;
    // the same id is what lets an at-most-once server recognise the duplicate.
    const int attempts = (semantics_ == Semantics::MAYBE) ? 1 : 1 + max_retries_;
    uint8_t buffer[65536];

    for (int attempt = 1; attempt <= attempts; attempt++) {
        if (attempt > 1) {
            LOG_WARNING("Timeout: no reply to " + tag + " within " + std::to_string(timeout_ms_) +
                        " ms, retransmitting (attempt " + std::to_string(attempt) + "/" +
                        std::to_string(attempts) + ")");
        }

        if (shouldSimulateLoss()) {
            LOG_WARNING("[Simulated loss] request " + tag + " dropped before sending");
        } else {
            socket_.sendTo(request.data(), request.size(), server_addr_);
        }

        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeout_ms_);
        int wait_ms;
        while ((wait_ms = millisecondsUntil(deadline)) > 0) {
            struct sockaddr_in src_addr;
            ssize_t received = socket_.recvFrom(buffer, sizeof(buffer), src_addr, wait_ms);
            if (received <= 0) {
                break;   // timeout (0) or socket error (-1): next attempt
            }

            std::vector<uint8_t> message(buffer, buffer + received);
            if (message.size() < MessageHeader::SIZE) {
                continue;
            }

            uint32_t type, id, length;
            MessageSerializer::readHeader(message, type, id, length);
            if (type == reply_type && id == request_id) {
                reply.swap(message);
                return ERR_SUCCESS;
            }

            // Not the reply we are waiting for, e.g. a late reply to an earlier
            // request: drop it and keep waiting.
            LOG_INFO("Discarded unexpected " + std::string(getMessageTypeName(type)) + " #" +
                     std::to_string(id) + " while waiting for the reply to " + tag);
        }
    }

    if (semantics_ == Semantics::MAYBE) {
        LOG_ERROR("No reply to " + tag + " (maybe semantics: the request is not retransmitted)");
    } else {
        LOG_ERROR("No reply to " + tag + " after " + std::to_string(attempts) + " attempt(s), giving up");
    }
    return ERR_NO_REPLY;
}

int32_t FlightClient::call(const std::vector<uint8_t>& request, uint32_t reply_type,
                           const std::function<int32_t(const std::vector<uint8_t>&)>& unmarshal) {
    std::vector<uint8_t> reply;
    int32_t result = invoke(request, reply_type, reply);
    if (result != ERR_SUCCESS) {
        return result;
    }

    try {
        return unmarshal(reply);
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("Malformed reply: ") + e.what());
        return ERR_MALFORMED_REPLY;
    }
}

// ============================================================
// Services
// ============================================================

int32_t FlightClient::queryFlight(const std::string& source, const std::string& destination,
                                  std::vector<int32_t>& flight_ids) {
    std::vector<uint8_t> request;
    MessageSerializer::marshalQueryFlightRequest(generateRequestId(), source, destination, request);

    return call(request, MSG_QUERY_FLIGHT_RESPONSE, [&](const std::vector<uint8_t>& reply) {
        uint32_t reply_id;
        int32_t status;
        MessageSerializer::unmarshalQueryFlightResponse(reply, reply_id, status, flight_ids);
        return status;
    });
}

int32_t FlightClient::getFlightInfo(int32_t flight_id, Flight& flight) {
    std::vector<uint8_t> request;
    MessageSerializer::marshalGetFlightInfoRequest(generateRequestId(), flight_id, request);

    return call(request, MSG_GET_FLIGHT_INFO_RESPONSE, [&](const std::vector<uint8_t>& reply) {
        uint32_t reply_id;
        int32_t status;
        MessageSerializer::unmarshalGetFlightInfoResponse(reply, reply_id, status, flight);
        return status;
    });
}

int32_t FlightClient::reserveSeats(int32_t flight_id, int32_t seat_count, int32_t& remaining_seats) {
    std::vector<uint8_t> request;
    MessageSerializer::marshalReserveSeatsRequest(generateRequestId(), flight_id, seat_count, request);

    return call(request, MSG_RESERVE_SEATS_RESPONSE, [&](const std::vector<uint8_t>& reply) {
        uint32_t reply_id;
        int32_t status;
        MessageSerializer::unmarshalReserveSeatsResponse(reply, reply_id, status, remaining_seats);
        return status;
    });
}

int32_t FlightClient::registerMonitor(int32_t flight_id, int32_t interval_seconds) {
    std::vector<uint8_t> request;
    MessageSerializer::marshalRegisterMonitorRequest(generateRequestId(), flight_id, interval_seconds, request);

    return call(request, MSG_REGISTER_MONITOR_RESPONSE, [&](const std::vector<uint8_t>& reply) {
        uint32_t reply_id;
        int32_t status;
        MessageSerializer::unmarshalRegisterMonitorResponse(reply, reply_id, status);
        return status;
    });
}

void FlightClient::waitForCallbacks(int32_t interval_seconds,
                                    const std::function<void(int32_t, int32_t, int32_t)>& on_update) {
    // The user is blocked for the whole monitor interval (as allowed by the
    // project description), so no extra thread is needed.
    const Clock::time_point deadline = Clock::now() + std::chrono::seconds(interval_seconds);
    uint8_t buffer[65536];

    int wait_ms;
    while ((wait_ms = millisecondsUntil(deadline)) > 0) {
        struct sockaddr_in src_addr;
        ssize_t received = socket_.recvFrom(buffer, sizeof(buffer), src_addr, wait_ms);
        if (received == 0) {
            break;   // interval over
        }
        if (received < 0) {
            continue;
        }

        std::vector<uint8_t> message(buffer, buffer + received);
        try {
            int32_t flight_id, seat_count, remaining_time;
            MessageSerializer::unmarshalCallbackUpdate(message, flight_id, seat_count, remaining_time);
            on_update(flight_id, seat_count, remaining_time);
        } catch (const std::exception&) {
            // e.g. a duplicate reply to the register request
            LOG_INFO("Discarded a non-callback message while monitoring");
        }
    }
}

int32_t FlightClient::getSeatCount(int32_t flight_id, int32_t& count) {
    std::vector<uint8_t> request;
    MessageSerializer::marshalGetSeatCountRequest(generateRequestId(), flight_id, request);

    return call(request, MSG_GET_SEAT_COUNT_RESPONSE, [&](const std::vector<uint8_t>& reply) {
        uint32_t reply_id;
        int32_t status;
        MessageSerializer::unmarshalGetSeatCountResponse(reply, reply_id, status, count);
        return status;
    });
}

int32_t FlightClient::cancelReservation(int32_t flight_id, int32_t seat_count, int32_t& new_count) {
    std::vector<uint8_t> request;
    MessageSerializer::marshalCancelReservationRequest(generateRequestId(), flight_id, seat_count, request);

    return call(request, MSG_CANCEL_RESERVATION_RESPONSE, [&](const std::vector<uint8_t>& reply) {
        uint32_t reply_id;
        int32_t status;
        MessageSerializer::unmarshalCancelReservationResponse(reply, reply_id, status, new_count);
        return status;
    });
}

// ============================================================
// Settings
// ============================================================

void FlightClient::setSemantics(Semantics semantics) {
    semantics_ = semantics;
}

void FlightClient::setLossRate(double rate) {
    loss_rate_ = rate;
}

void FlightClient::setTimeoutMs(int timeout_ms) {
    timeout_ms_ = timeout_ms;
}

void FlightClient::setMaxRetries(int max_retries) {
    max_retries_ = max_retries;
}

void FlightClient::setSeed(uint32_t seed) {
    rng_.seed(seed);
}

uint32_t FlightClient::generateRequestId() {
    // Unique within this client; the server combines it with the client's
    // address to identify a request.
    return ++request_id_counter_;
}

bool FlightClient::shouldSimulateLoss() {
    if (loss_rate_ <= 0.0) return false;
    if (loss_rate_ >= 1.0) return true;

    double random = static_cast<double>(rng_()) / 4294967296.0;
    return random < loss_rate_;
}
