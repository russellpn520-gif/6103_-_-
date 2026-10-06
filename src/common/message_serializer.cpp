/**
 * @file message_serializer.cpp
 * @brief Marshalling/unmarshalling of all messages (format: see message_serializer.h).
 *
 * Every multi-byte value goes through htonl()/ntohl(), so the byte stream is
 * the same whatever the byte order of the host that produced it.
 */

#include "common/message_serializer.h"
#include <cstring>
#include <stdexcept>

// ============================================================
// Time
// ============================================================

void Time::serialize(uint8_t* buffer) const {
    const int32_t fields[5] = {year, month, day, hour, minute};
    for (int i = 0; i < 5; i++) {
        uint32_t network_value = htonl(static_cast<uint32_t>(fields[i]));
        memcpy(buffer + 4 * i, &network_value, sizeof(network_value));  // buffer may be unaligned
    }
}

void Time::deserialize(const uint8_t* buffer) {
    int32_t* fields[5] = {&year, &month, &day, &hour, &minute};
    for (int i = 0; i < 5; i++) {
        uint32_t network_value;
        memcpy(&network_value, buffer + 4 * i, sizeof(network_value));
        *fields[i] = static_cast<int32_t>(ntohl(network_value));
    }
}

// ============================================================
// Internal helpers
// ============================================================

namespace {

/**
 * Start a message: write the header with a placeholder length.
 * Returns the offset of the length field, to be filled in by finishMessage().
 */
size_t beginMessage(std::vector<uint8_t>& buffer, uint32_t message_type, uint32_t request_id) {
    buffer.clear();
    MessageSerializer::writeUint32(buffer, message_type);
    MessageSerializer::writeUint32(buffer, request_id);
    size_t length_pos = buffer.size();
    MessageSerializer::writeUint32(buffer, 0);
    return length_pos;
}

/**
 * Finish a message: store the total length (header + body) in the header.
 */
void finishMessage(std::vector<uint8_t>& buffer, size_t length_pos) {
    uint32_t network_length = htonl(static_cast<uint32_t>(buffer.size()));
    memcpy(&buffer[length_pos], &network_length, sizeof(network_length));
}

/**
 * Read and validate a header. The type must be expected_type and the length
 * field must equal the datagram size. Returns the request id and leaves
 * offset at the first byte of the body.
 */
uint32_t readCheckedHeader(const std::vector<uint8_t>& buffer, uint32_t expected_type, size_t& offset) {
    offset = 0;
    uint32_t message_type = MessageSerializer::readUint32(buffer, offset);
    if (message_type != expected_type) {
        throw std::runtime_error(std::string("Unexpected message type ") + getMessageTypeName(message_type) +
                                 " (expected " + getMessageTypeName(expected_type) + ")");
    }
    uint32_t request_id = MessageSerializer::readUint32(buffer, offset);
    uint32_t message_length = MessageSerializer::readUint32(buffer, offset);
    if (message_length != buffer.size()) {
        throw std::runtime_error("Length field " + std::to_string(message_length) +
                                 " does not match datagram size " + std::to_string(buffer.size()));
    }
    return request_id;
}

void writeInt32(std::vector<uint8_t>& buffer, int32_t value) {
    MessageSerializer::writeUint32(buffer, static_cast<uint32_t>(value));
}

int32_t readInt32(const std::vector<uint8_t>& buffer, size_t& offset) {
    return static_cast<int32_t>(MessageSerializer::readUint32(buffer, offset));
}

} // namespace

// ============================================================
// Primitive types
// ============================================================

void MessageSerializer::writeHeader(std::vector<uint8_t>& buffer,
                                    uint32_t message_type,
                                    uint32_t request_id,
                                    uint32_t message_length) {
    writeUint32(buffer, message_type);
    writeUint32(buffer, request_id);
    writeUint32(buffer, message_length);
}

void MessageSerializer::readHeader(const std::vector<uint8_t>& buffer,
                                   uint32_t& message_type,
                                   uint32_t& request_id,
                                   uint32_t& message_length) {
    size_t offset = 0;
    message_type = readUint32(buffer, offset);
    request_id = readUint32(buffer, offset);
    message_length = readUint32(buffer, offset);
}

void MessageSerializer::writeUint32(std::vector<uint8_t>& buffer, uint32_t value) {
    uint32_t network_value = htonl(value);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&network_value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(uint32_t));
}

uint32_t MessageSerializer::readUint32(const std::vector<uint8_t>& buffer, size_t& offset) {
    if (offset + sizeof(uint32_t) > buffer.size()) {
        throw std::runtime_error("Buffer underflow in readUint32");
    }

    uint32_t network_value;
    memcpy(&network_value, &buffer[offset], sizeof(uint32_t));
    offset += sizeof(uint32_t);

    return ntohl(network_value);
}

void MessageSerializer::writeString(std::vector<uint8_t>& buffer, const std::string& str) {
    writeUint32(buffer, static_cast<uint32_t>(str.length()));
    buffer.insert(buffer.end(), str.begin(), str.end());
}

std::string MessageSerializer::readString(const std::vector<uint8_t>& buffer, size_t& offset) {
    uint32_t length = readUint32(buffer, offset);

    if (length > buffer.size() - offset) {
        throw std::runtime_error("Buffer underflow in readString");
    }

    std::string str(buffer.begin() + offset, buffer.begin() + offset + length);
    offset += length;

    return str;
}

void MessageSerializer::writeFloat(std::vector<uint8_t>& buffer, float value) {
    // Send the IEEE-754 bit pattern as an ordinary 32-bit integer.
    uint32_t int_value;
    memcpy(&int_value, &value, sizeof(float));
    writeUint32(buffer, int_value);
}

float MessageSerializer::readFloat(const std::vector<uint8_t>& buffer, size_t& offset) {
    uint32_t int_value = readUint32(buffer, offset);
    float value;
    memcpy(&value, &int_value, sizeof(float));
    return value;
}

// ============================================================
// Service 1: query flights by source and destination
// ============================================================

void MessageSerializer::marshalQueryFlightRequest(
    uint32_t request_id,
    const std::string& source,
    const std::string& destination,
    std::vector<uint8_t>& buffer) {

    size_t length_pos = beginMessage(buffer, MSG_QUERY_FLIGHT_REQUEST, request_id);
    writeString(buffer, source);
    writeString(buffer, destination);
    finishMessage(buffer, length_pos);
}

void MessageSerializer::unmarshalQueryFlightRequest(
    const std::vector<uint8_t>& buffer,
    uint32_t& request_id,
    std::string& source,
    std::string& destination) {

    size_t offset;
    request_id = readCheckedHeader(buffer, MSG_QUERY_FLIGHT_REQUEST, offset);
    source = readString(buffer, offset);
    destination = readString(buffer, offset);
}

void MessageSerializer::marshalQueryFlightResponse(
    uint32_t request_id,
    int32_t status_code,
    const std::vector<int32_t>& flight_ids,
    std::vector<uint8_t>& buffer) {

    size_t length_pos = beginMessage(buffer, MSG_QUERY_FLIGHT_RESPONSE, request_id);
    writeInt32(buffer, status_code);

    // The id list is only present on success: count followed by the ids.
    if (status_code == ERR_SUCCESS) {
        writeUint32(buffer, static_cast<uint32_t>(flight_ids.size()));
        for (int32_t id : flight_ids) {
            writeInt32(buffer, id);
        }
    }

    finishMessage(buffer, length_pos);
}

void MessageSerializer::unmarshalQueryFlightResponse(
    const std::vector<uint8_t>& buffer,
    uint32_t& request_id,
    int32_t& status_code,
    std::vector<int32_t>& flight_ids) {

    size_t offset;
    request_id = readCheckedHeader(buffer, MSG_QUERY_FLIGHT_RESPONSE, offset);
    status_code = readInt32(buffer, offset);

    flight_ids.clear();
    if (status_code == ERR_SUCCESS) {
        uint32_t count = readUint32(buffer, offset);
        if (count > (buffer.size() - offset) / sizeof(uint32_t)) {
            throw std::runtime_error("Flight id count exceeds message size");
        }
        for (uint32_t i = 0; i < count; i++) {
            flight_ids.push_back(readInt32(buffer, offset));
        }
    }
}

// ============================================================
// Service 2: query flight details
// ============================================================

void MessageSerializer::marshalGetFlightInfoRequest(
    uint32_t request_id,
    int32_t flight_id,
    std::vector<uint8_t>& buffer) {

    size_t length_pos = beginMessage(buffer, MSG_GET_FLIGHT_INFO_REQUEST, request_id);
    writeInt32(buffer, flight_id);
    finishMessage(buffer, length_pos);
}

void MessageSerializer::unmarshalGetFlightInfoRequest(
    const std::vector<uint8_t>& buffer,
    uint32_t& request_id,
    int32_t& flight_id) {

    size_t offset;
    request_id = readCheckedHeader(buffer, MSG_GET_FLIGHT_INFO_REQUEST, offset);
    flight_id = readInt32(buffer, offset);
}

void MessageSerializer::marshalGetFlightInfoResponse(
    uint32_t request_id,
    int32_t status_code,
    const Flight& flight,
    std::vector<uint8_t>& buffer) {

    size_t length_pos = beginMessage(buffer, MSG_GET_FLIGHT_INFO_RESPONSE, request_id);
    writeInt32(buffer, status_code);

    if (status_code == ERR_SUCCESS) {
        writeInt32(buffer, flight.flight_id);
        writeString(buffer, flight.source);
        writeString(buffer, flight.destination);

        uint8_t time_buffer[Time::WIRE_SIZE];
        flight.departure_time.serialize(time_buffer);
        buffer.insert(buffer.end(), time_buffer, time_buffer + Time::WIRE_SIZE);

        writeFloat(buffer, flight.airfare);
        writeInt32(buffer, flight.seat_availability);
    }

    finishMessage(buffer, length_pos);
}

void MessageSerializer::unmarshalGetFlightInfoResponse(
    const std::vector<uint8_t>& buffer,
    uint32_t& request_id,
    int32_t& status_code,
    Flight& flight) {

    size_t offset;
    request_id = readCheckedHeader(buffer, MSG_GET_FLIGHT_INFO_RESPONSE, offset);
    status_code = readInt32(buffer, offset);

    if (status_code == ERR_SUCCESS) {
        flight.flight_id = readInt32(buffer, offset);
        flight.source = readString(buffer, offset);
        flight.destination = readString(buffer, offset);

        if (offset + Time::WIRE_SIZE > buffer.size()) {
            throw std::runtime_error("Buffer underflow in readTime");
        }
        flight.departure_time.deserialize(&buffer[offset]);
        offset += Time::WIRE_SIZE;

        flight.airfare = readFloat(buffer, offset);
        flight.seat_availability = readInt32(buffer, offset);
    }
}

// ============================================================
// Service 3: reserve seats
// ============================================================

void MessageSerializer::marshalReserveSeatsRequest(
    uint32_t request_id,
    int32_t flight_id,
    int32_t seat_count,
    std::vector<uint8_t>& buffer) {

    size_t length_pos = beginMessage(buffer, MSG_RESERVE_SEATS_REQUEST, request_id);
    writeInt32(buffer, flight_id);
    writeInt32(buffer, seat_count);
    finishMessage(buffer, length_pos);
}

void MessageSerializer::unmarshalReserveSeatsRequest(
    const std::vector<uint8_t>& buffer,
    uint32_t& request_id,
    int32_t& flight_id,
    int32_t& seat_count) {

    size_t offset;
    request_id = readCheckedHeader(buffer, MSG_RESERVE_SEATS_REQUEST, offset);
    flight_id = readInt32(buffer, offset);
    seat_count = readInt32(buffer, offset);
}

void MessageSerializer::marshalReserveSeatsResponse(
    uint32_t request_id,
    int32_t status_code,
    int32_t remaining_seats,
    std::vector<uint8_t>& buffer) {

    size_t length_pos = beginMessage(buffer, MSG_RESERVE_SEATS_RESPONSE, request_id);
    writeInt32(buffer, status_code);
    writeInt32(buffer, remaining_seats);
    finishMessage(buffer, length_pos);
}

void MessageSerializer::unmarshalReserveSeatsResponse(
    const std::vector<uint8_t>& buffer,
    uint32_t& request_id,
    int32_t& status_code,
    int32_t& remaining_seats) {

    size_t offset;
    request_id = readCheckedHeader(buffer, MSG_RESERVE_SEATS_RESPONSE, offset);
    status_code = readInt32(buffer, offset);
    remaining_seats = readInt32(buffer, offset);
}

// ============================================================
// Service 4: monitor seat availability (callback)
// ============================================================

void MessageSerializer::marshalRegisterMonitorRequest(
    uint32_t request_id,
    int32_t flight_id,
    int32_t interval_seconds,
    std::vector<uint8_t>& buffer) {

    size_t length_pos = beginMessage(buffer, MSG_REGISTER_MONITOR_REQUEST, request_id);
    writeInt32(buffer, flight_id);
    writeInt32(buffer, interval_seconds);
    finishMessage(buffer, length_pos);
}

void MessageSerializer::unmarshalRegisterMonitorRequest(
    const std::vector<uint8_t>& buffer,
    uint32_t& request_id,
    int32_t& flight_id,
    int32_t& interval_seconds) {

    size_t offset;
    request_id = readCheckedHeader(buffer, MSG_REGISTER_MONITOR_REQUEST, offset);
    flight_id = readInt32(buffer, offset);
    interval_seconds = readInt32(buffer, offset);
}

void MessageSerializer::marshalRegisterMonitorResponse(
    uint32_t request_id,
    int32_t status_code,
    std::vector<uint8_t>& buffer) {

    size_t length_pos = beginMessage(buffer, MSG_REGISTER_MONITOR_RESPONSE, request_id);
    writeInt32(buffer, status_code);
    finishMessage(buffer, length_pos);
}

void MessageSerializer::unmarshalRegisterMonitorResponse(
    const std::vector<uint8_t>& buffer,
    uint32_t& request_id,
    int32_t& status_code) {

    size_t offset;
    request_id = readCheckedHeader(buffer, MSG_REGISTER_MONITOR_RESPONSE, offset);
    status_code = readInt32(buffer, offset);
}

void MessageSerializer::marshalCallbackUpdate(
    int32_t flight_id,
    int32_t new_seat_count,
    int32_t remaining_time,
    std::vector<uint8_t>& buffer) {

    // A callback is initiated by the server, so it carries no request id (0).
    size_t length_pos = beginMessage(buffer, MSG_CALLBACK_UPDATE, 0);
    writeInt32(buffer, flight_id);
    writeInt32(buffer, new_seat_count);
    writeInt32(buffer, remaining_time);
    finishMessage(buffer, length_pos);
}

void MessageSerializer::unmarshalCallbackUpdate(
    const std::vector<uint8_t>& buffer,
    int32_t& flight_id,
    int32_t& new_seat_count,
    int32_t& remaining_time) {

    size_t offset;
    readCheckedHeader(buffer, MSG_CALLBACK_UPDATE, offset);
    flight_id = readInt32(buffer, offset);
    new_seat_count = readInt32(buffer, offset);
    remaining_time = readInt32(buffer, offset);
}

// ============================================================
// Service 5: query seat count (idempotent)
// ============================================================

void MessageSerializer::marshalGetSeatCountRequest(
    uint32_t request_id,
    int32_t flight_id,
    std::vector<uint8_t>& buffer) {

    size_t length_pos = beginMessage(buffer, MSG_GET_SEAT_COUNT_REQUEST, request_id);
    writeInt32(buffer, flight_id);
    finishMessage(buffer, length_pos);
}

void MessageSerializer::unmarshalGetSeatCountRequest(
    const std::vector<uint8_t>& buffer,
    uint32_t& request_id,
    int32_t& flight_id) {

    size_t offset;
    request_id = readCheckedHeader(buffer, MSG_GET_SEAT_COUNT_REQUEST, offset);
    flight_id = readInt32(buffer, offset);
}

void MessageSerializer::marshalGetSeatCountResponse(
    uint32_t request_id,
    int32_t status_code,
    int32_t seat_count,
    std::vector<uint8_t>& buffer) {

    size_t length_pos = beginMessage(buffer, MSG_GET_SEAT_COUNT_RESPONSE, request_id);
    writeInt32(buffer, status_code);
    writeInt32(buffer, seat_count);
    finishMessage(buffer, length_pos);
}

void MessageSerializer::unmarshalGetSeatCountResponse(
    const std::vector<uint8_t>& buffer,
    uint32_t& request_id,
    int32_t& status_code,
    int32_t& seat_count) {

    size_t offset;
    request_id = readCheckedHeader(buffer, MSG_GET_SEAT_COUNT_RESPONSE, offset);
    status_code = readInt32(buffer, offset);
    seat_count = readInt32(buffer, offset);
}

// ============================================================
// Service 6: cancel reservation (non-idempotent)
// ============================================================

void MessageSerializer::marshalCancelReservationRequest(
    uint32_t request_id,
    int32_t flight_id,
    int32_t seat_count,
    std::vector<uint8_t>& buffer) {

    size_t length_pos = beginMessage(buffer, MSG_CANCEL_RESERVATION_REQUEST, request_id);
    writeInt32(buffer, flight_id);
    writeInt32(buffer, seat_count);
    finishMessage(buffer, length_pos);
}

void MessageSerializer::unmarshalCancelReservationRequest(
    const std::vector<uint8_t>& buffer,
    uint32_t& request_id,
    int32_t& flight_id,
    int32_t& seat_count) {

    size_t offset;
    request_id = readCheckedHeader(buffer, MSG_CANCEL_RESERVATION_REQUEST, offset);
    flight_id = readInt32(buffer, offset);
    seat_count = readInt32(buffer, offset);
}

void MessageSerializer::marshalCancelReservationResponse(
    uint32_t request_id,
    int32_t status_code,
    int32_t new_seat_count,
    std::vector<uint8_t>& buffer) {

    size_t length_pos = beginMessage(buffer, MSG_CANCEL_RESERVATION_RESPONSE, request_id);
    writeInt32(buffer, status_code);
    writeInt32(buffer, new_seat_count);
    finishMessage(buffer, length_pos);
}

void MessageSerializer::unmarshalCancelReservationResponse(
    const std::vector<uint8_t>& buffer,
    uint32_t& request_id,
    int32_t& status_code,
    int32_t& new_seat_count) {

    size_t offset;
    request_id = readCheckedHeader(buffer, MSG_CANCEL_RESERVATION_RESPONSE, offset);
    status_code = readInt32(buffer, offset);
    new_seat_count = readInt32(buffer, offset);
}
