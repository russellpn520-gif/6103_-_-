/**
 * @file message_types.h
 * @brief Protocol constants: message types, error codes, header layout and
 *        invocation semantics.
 */

#ifndef MESSAGE_TYPES_H
#define MESSAGE_TYPES_H

#include <cstdint>
#include <cstddef>
#include <string>

/**
 * @brief Message type, the first field of every message header.
 */
enum MessageType : uint32_t {
    // Service 1: query flight ids by source and destination
    MSG_QUERY_FLIGHT_REQUEST = 1,
    MSG_QUERY_FLIGHT_RESPONSE = 2,

    // Service 2: query departure time, airfare and seat availability
    MSG_GET_FLIGHT_INFO_REQUEST = 3,
    MSG_GET_FLIGHT_INFO_RESPONSE = 4,

    // Service 3: reserve seats (non-idempotent)
    MSG_RESERVE_SEATS_REQUEST = 5,
    MSG_RESERVE_SEATS_RESPONSE = 6,

    // Service 4: monitor seat availability through callbacks
    MSG_REGISTER_MONITOR_REQUEST = 7,
    MSG_REGISTER_MONITOR_RESPONSE = 8,
    MSG_CALLBACK_UPDATE = 9,              // server -> client, not a reply to any request

    // Service 5 (additional, idempotent): query the seat availability only
    MSG_GET_SEAT_COUNT_REQUEST = 10,
    MSG_GET_SEAT_COUNT_RESPONSE = 11,

    // Service 6 (additional, non-idempotent): cancel (give back) reserved seats
    MSG_CANCEL_RESERVATION_REQUEST = 12,
    MSG_CANCEL_RESERVATION_RESPONSE = 13,
};

/**
 * @brief Status codes. Codes below 100 are sent by the server in replies;
 *        codes from 100 are produced locally by the client and never sent.
 */
enum ErrorCode : int32_t {
    ERR_SUCCESS = 0,
    ERR_INVALID_INPUT = 1,              // e.g. empty source/destination
    ERR_FLIGHT_NOT_FOUND = 2,           // no flight with this identifier
    ERR_NO_FLIGHTS_FOUND = 3,           // no flight between source and destination
    ERR_INSUFFICIENT_SEATS = 4,         // fewer seats available than requested
    ERR_INVALID_SEAT_COUNT = 5,         // seat count must be positive
    ERR_INVALID_MONITOR_INTERVAL = 6,   // interval outside 1..MAX_MONITOR_INTERVAL_SECONDS
    ERR_CANCEL_EXCEEDS_RESERVED = 7,    // cancelling more seats than are reserved
    ERR_SERVER_ERROR = 99,

    // Client-side only
    ERR_NO_REPLY = 100,                 // no reply after all (re)transmissions
    ERR_MALFORMED_REPLY = 101,          // reply could not be unmarshalled
};

/** Longest monitor interval the server accepts. */
constexpr int32_t MAX_MONITOR_INTERVAL_SECONDS = 3600;

/**
 * @brief Header that starts every message (3 x uint32, big-endian = 12 bytes).
 */
struct MessageHeader {
    uint32_t message_type;      // MessageType
    uint32_t request_id;        // chosen by the client, unique per client; 0 for callbacks
    uint32_t message_length;    // total datagram length in bytes, header included

    static constexpr size_t SIZE = 12;
};

/**
 * @brief Text for a status code (printed by the client).
 */
inline const char* getErrorMessage(int32_t error_code) {
    switch (error_code) {
        case ERR_SUCCESS: return "Success";
        case ERR_INVALID_INPUT: return "Invalid input";
        case ERR_FLIGHT_NOT_FOUND: return "Flight not found";
        case ERR_NO_FLIGHTS_FOUND: return "No flights found";
        case ERR_INSUFFICIENT_SEATS: return "Insufficient seats";
        case ERR_INVALID_SEAT_COUNT: return "Invalid seat count (must be a positive number)";
        case ERR_INVALID_MONITOR_INTERVAL: return "Invalid monitor interval (must be 1-3600 seconds)";
        case ERR_CANCEL_EXCEEDS_RESERVED: return "Cannot cancel more seats than are currently reserved";
        case ERR_SERVER_ERROR: return "Server error";
        case ERR_NO_REPLY: return "No reply from server (request/reply lost or server unreachable)";
        case ERR_MALFORMED_REPLY: return "Malformed reply from server";
        default: return "Unknown error";
    }
}

/**
 * @brief Short name of a message type, for log output.
 */
inline const char* getMessageTypeName(uint32_t message_type) {
    switch (message_type) {
        case MSG_QUERY_FLIGHT_REQUEST: return "QUERY_FLIGHT_REQUEST";
        case MSG_QUERY_FLIGHT_RESPONSE: return "QUERY_FLIGHT_RESPONSE";
        case MSG_GET_FLIGHT_INFO_REQUEST: return "GET_FLIGHT_INFO_REQUEST";
        case MSG_GET_FLIGHT_INFO_RESPONSE: return "GET_FLIGHT_INFO_RESPONSE";
        case MSG_RESERVE_SEATS_REQUEST: return "RESERVE_SEATS_REQUEST";
        case MSG_RESERVE_SEATS_RESPONSE: return "RESERVE_SEATS_RESPONSE";
        case MSG_REGISTER_MONITOR_REQUEST: return "REGISTER_MONITOR_REQUEST";
        case MSG_REGISTER_MONITOR_RESPONSE: return "REGISTER_MONITOR_RESPONSE";
        case MSG_CALLBACK_UPDATE: return "CALLBACK_UPDATE";
        case MSG_GET_SEAT_COUNT_REQUEST: return "GET_SEAT_COUNT_REQUEST";
        case MSG_GET_SEAT_COUNT_RESPONSE: return "GET_SEAT_COUNT_RESPONSE";
        case MSG_CANCEL_RESERVATION_REQUEST: return "CANCEL_RESERVATION_REQUEST";
        case MSG_CANCEL_RESERVATION_RESPONSE: return "CANCEL_RESERVATION_RESPONSE";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Invocation semantics, as defined in the lecture on remote invocation.
 *
 *  MAYBE          The client sends each request once and never retransmits.
 *                 (Baseline only; offered by the client for comparison.)
 *  AT_LEAST_ONCE  The client retransmits the request after a timeout. The
 *                 server does NOT filter duplicates: it executes every request
 *                 it receives, so a retransmission after a lost reply executes
 *                 the operation again. Safe only for idempotent operations.
 *  AT_MOST_ONCE   The client retransmits the request after a timeout. The
 *                 server filters duplicates by (client address, request id) and
 *                 re-sends the reply stored in its history instead of executing
 *                 the operation again. Correct for all operations.
 *
 * The client retransmits the same way under AT_LEAST_ONCE and AT_MOST_ONCE;
 * the difference between them is made by the server.
 */
enum class Semantics {
    MAYBE,
    AT_LEAST_ONCE,
    AT_MOST_ONCE,
};

/**
 * @brief Command-line spelling of a semantics value.
 */
inline const char* getSemanticsName(Semantics semantics) {
    switch (semantics) {
        case Semantics::MAYBE: return "maybe";
        case Semantics::AT_LEAST_ONCE: return "at-least-once";
        case Semantics::AT_MOST_ONCE: return "at-most-once";
    }
    return "unknown";
}

/**
 * @brief Parse "maybe", "at-least-once" or "at-most-once".
 * @return false if the text is not one of these
 */
inline bool parseSemantics(const std::string& text, Semantics& semantics) {
    if (text == "maybe") {
        semantics = Semantics::MAYBE;
    } else if (text == "at-least-once") {
        semantics = Semantics::AT_LEAST_ONCE;
    } else if (text == "at-most-once") {
        semantics = Semantics::AT_MOST_ONCE;
    } else {
        return false;
    }
    return true;
}

#endif // MESSAGE_TYPES_H
