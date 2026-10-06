/**
 * @file message_serializer.h
 * @brief Marshalling and unmarshalling of request/reply messages.
 *
 * All messages are plain byte arrays built by hand (no RMI/RPC/object
 * serialisation library is used).
 *
 * Field encodings (every integer is 32 bits in network byte order, htonl/ntohl):
 *   int32 / uint32   4 bytes
 *   float            IEEE-754 single-precision bit pattern, sent as a uint32
 *   string           uint32 length, followed by `length` raw bytes (no '\0')
 *   Time             5 x int32: year, month, day, hour, minute (20 bytes)
 *
 * Header (12 bytes, every message):
 *   uint32 message_type | uint32 request_id | uint32 message_length
 *   message_length is the total datagram size, header included.
 *
 * Bodies:
 *   QUERY_FLIGHT_REQUEST         string source, string destination
 *   QUERY_FLIGHT_RESPONSE        int32 status [, uint32 count, count x int32 flight_id]
 *   GET_FLIGHT_INFO_REQUEST      int32 flight_id
 *   GET_FLIGHT_INFO_RESPONSE     int32 status [, int32 flight_id, string source, string destination,
 *                                              Time departure, float airfare, int32 seats]
 *   RESERVE_SEATS_REQUEST        int32 flight_id, int32 seat_count
 *   RESERVE_SEATS_RESPONSE       int32 status, int32 remaining_seats
 *   REGISTER_MONITOR_REQUEST     int32 flight_id, int32 interval_seconds
 *   REGISTER_MONITOR_RESPONSE    int32 status
 *   CALLBACK_UPDATE              int32 flight_id, int32 seat_availability, int32 remaining_seconds
 *                                (request_id is 0: a callback answers no request)
 *   GET_SEAT_COUNT_REQUEST       int32 flight_id
 *   GET_SEAT_COUNT_RESPONSE      int32 status, int32 seat_count
 *   CANCEL_RESERVATION_REQUEST   int32 flight_id, int32 seat_count
 *   CANCEL_RESERVATION_RESPONSE  int32 status, int32 seat_availability
 * The bracketed parts are present only when status == ERR_SUCCESS.
 *
 * Every unmarshal function checks the message type and that message_length
 * equals the datagram size, and throws std::runtime_error on malformed input.
 */

#ifndef MESSAGE_SERIALIZER_H
#define MESSAGE_SERIALIZER_H

#include <vector>
#include <string>
#include <cstdint>
#include "common/data_structures.h"
#include "common/message_types.h"

/**
 * @brief Static functions that convert messages to and from byte arrays.
 */
class MessageSerializer {
public:
    // ============================================================
    // Primitive types
    // ============================================================

    /**
     * @brief Append a header (type, request id, length).
     */
    static void writeHeader(std::vector<uint8_t>& buffer,
                           uint32_t message_type,
                           uint32_t request_id,
                           uint32_t message_length);

    /**
     * @brief Read the header fields at the start of buffer (no validation).
     */
    static void readHeader(const std::vector<uint8_t>& buffer,
                          uint32_t& message_type,
                          uint32_t& request_id,
                          uint32_t& message_length);

    /**
     * @brief Append a 32-bit unsigned integer in network byte order.
     */
    static void writeUint32(std::vector<uint8_t>& buffer, uint32_t value);

    /**
     * @brief Read a 32-bit unsigned integer at offset and advance offset.
     */
    static uint32_t readUint32(const std::vector<uint8_t>& buffer, size_t& offset);

    /**
     * @brief Append a length-prefixed string.
     */
    static void writeString(std::vector<uint8_t>& buffer, const std::string& str);

    /**
     * @brief Read a length-prefixed string at offset and advance offset.
     */
    static std::string readString(const std::vector<uint8_t>& buffer, size_t& offset);

    /**
     * @brief Append a float as its IEEE-754 bit pattern in network byte order.
     */
    static void writeFloat(std::vector<uint8_t>& buffer, float value);

    /**
     * @brief Read a float written by writeFloat() and advance offset.
     */
    static float readFloat(const std::vector<uint8_t>& buffer, size_t& offset);

    // ============================================================
    // Service 1: query flights by source and destination
    // ============================================================

    static void marshalQueryFlightRequest(
        uint32_t request_id,
        const std::string& source,
        const std::string& destination,
        std::vector<uint8_t>& buffer
    );

    static void unmarshalQueryFlightRequest(
        const std::vector<uint8_t>& buffer,
        uint32_t& request_id,
        std::string& source,
        std::string& destination
    );

    static void marshalQueryFlightResponse(
        uint32_t request_id,
        int32_t status_code,
        const std::vector<int32_t>& flight_ids,
        std::vector<uint8_t>& buffer
    );

    static void unmarshalQueryFlightResponse(
        const std::vector<uint8_t>& buffer,
        uint32_t& request_id,
        int32_t& status_code,
        std::vector<int32_t>& flight_ids
    );

    // ============================================================
    // Service 2: query flight details
    // ============================================================

    static void marshalGetFlightInfoRequest(
        uint32_t request_id,
        int32_t flight_id,
        std::vector<uint8_t>& buffer
    );

    static void unmarshalGetFlightInfoRequest(
        const std::vector<uint8_t>& buffer,
        uint32_t& request_id,
        int32_t& flight_id
    );

    static void marshalGetFlightInfoResponse(
        uint32_t request_id,
        int32_t status_code,
        const Flight& flight,
        std::vector<uint8_t>& buffer
    );

    static void unmarshalGetFlightInfoResponse(
        const std::vector<uint8_t>& buffer,
        uint32_t& request_id,
        int32_t& status_code,
        Flight& flight
    );

    // ============================================================
    // Service 3: reserve seats
    // ============================================================

    static void marshalReserveSeatsRequest(
        uint32_t request_id,
        int32_t flight_id,
        int32_t seat_count,
        std::vector<uint8_t>& buffer
    );

    static void unmarshalReserveSeatsRequest(
        const std::vector<uint8_t>& buffer,
        uint32_t& request_id,
        int32_t& flight_id,
        int32_t& seat_count
    );

    static void marshalReserveSeatsResponse(
        uint32_t request_id,
        int32_t status_code,
        int32_t remaining_seats,
        std::vector<uint8_t>& buffer
    );

    static void unmarshalReserveSeatsResponse(
        const std::vector<uint8_t>& buffer,
        uint32_t& request_id,
        int32_t& status_code,
        int32_t& remaining_seats
    );

    // ============================================================
    // Service 4: monitor seat availability (callback)
    // ============================================================

    static void marshalRegisterMonitorRequest(
        uint32_t request_id,
        int32_t flight_id,
        int32_t interval_seconds,
        std::vector<uint8_t>& buffer
    );

    static void unmarshalRegisterMonitorRequest(
        const std::vector<uint8_t>& buffer,
        uint32_t& request_id,
        int32_t& flight_id,
        int32_t& interval_seconds
    );

    static void marshalRegisterMonitorResponse(
        uint32_t request_id,
        int32_t status_code,
        std::vector<uint8_t>& buffer
    );

    static void unmarshalRegisterMonitorResponse(
        const std::vector<uint8_t>& buffer,
        uint32_t& request_id,
        int32_t& status_code
    );

    static void marshalCallbackUpdate(
        int32_t flight_id,
        int32_t new_seat_count,
        int32_t remaining_time,
        std::vector<uint8_t>& buffer
    );

    static void unmarshalCallbackUpdate(
        const std::vector<uint8_t>& buffer,
        int32_t& flight_id,
        int32_t& new_seat_count,
        int32_t& remaining_time
    );

    // ============================================================
    // Service 5: query seat count (idempotent)
    // ============================================================

    static void marshalGetSeatCountRequest(
        uint32_t request_id,
        int32_t flight_id,
        std::vector<uint8_t>& buffer
    );

    static void unmarshalGetSeatCountRequest(
        const std::vector<uint8_t>& buffer,
        uint32_t& request_id,
        int32_t& flight_id
    );

    static void marshalGetSeatCountResponse(
        uint32_t request_id,
        int32_t status_code,
        int32_t seat_count,
        std::vector<uint8_t>& buffer
    );

    static void unmarshalGetSeatCountResponse(
        const std::vector<uint8_t>& buffer,
        uint32_t& request_id,
        int32_t& status_code,
        int32_t& seat_count
    );

    // ============================================================
    // Service 6: cancel reservation (non-idempotent)
    // ============================================================

    static void marshalCancelReservationRequest(
        uint32_t request_id,
        int32_t flight_id,
        int32_t seat_count,
        std::vector<uint8_t>& buffer
    );

    static void unmarshalCancelReservationRequest(
        const std::vector<uint8_t>& buffer,
        uint32_t& request_id,
        int32_t& flight_id,
        int32_t& seat_count
    );

    static void marshalCancelReservationResponse(
        uint32_t request_id,
        int32_t status_code,
        int32_t new_seat_count,
        std::vector<uint8_t>& buffer
    );

    static void unmarshalCancelReservationResponse(
        const std::vector<uint8_t>& buffer,
        uint32_t& request_id,
        int32_t& status_code,
        int32_t& new_seat_count
    );
};

#endif // MESSAGE_SERIALIZER_H
