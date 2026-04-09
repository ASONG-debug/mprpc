#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mprpc
{
constexpr uint32_t kRpcMagic = 0x6d707263; // "mprc"
constexpr uint16_t kRpcProtocolVersion = 1;
constexpr std::size_t kRpcFrameHeaderSize = 24;
constexpr uint32_t kMaxRpcMetadataSize = 64 * 1024;
constexpr uint32_t kMaxRpcBodySize = 16 * 1024 * 1024;

enum RpcMessageType : uint16_t
{
    kRpcMessageRequest = 1,
    kRpcMessageResponse = 2,
};

enum RpcStatusCode : uint32_t
{
    kRpcStatusOk = 0,
    kRpcStatusInvalidFrame = 1,
    kRpcStatusVersionMismatch = 2,
    kRpcStatusInvalidMetadata = 3,
    kRpcStatusServiceNotFound = 4,
    kRpcStatusMethodNotFound = 5,
    kRpcStatusRequestDecodeError = 6,
    kRpcStatusResponseEncodeError = 7,
    kRpcStatusInternalError = 8,
    kRpcStatusServiceDiscoveryError = 9,
    kRpcStatusNetworkError = 10,
    kRpcStatusTimeout = 11,
};

struct RpcFrameHeader
{
    uint32_t magic = kRpcMagic;
    uint16_t version = kRpcProtocolVersion;
    uint16_t message_type = kRpcMessageRequest;
    uint64_t request_id = 0;
    uint32_t metadata_size = 0;
    uint32_t body_size = 0;
};

struct RpcResponseMeta
{
    uint32_t status_code = kRpcStatusOk;
    std::string error_text;
};

inline void AppendUint16(std::string *buffer, uint16_t value)
{
    buffer->push_back(static_cast<char>((value >> 8) & 0xff));
    buffer->push_back(static_cast<char>(value & 0xff));
}

inline void AppendUint32(std::string *buffer, uint32_t value)
{
    buffer->push_back(static_cast<char>((value >> 24) & 0xff));
    buffer->push_back(static_cast<char>((value >> 16) & 0xff));
    buffer->push_back(static_cast<char>((value >> 8) & 0xff));
    buffer->push_back(static_cast<char>(value & 0xff));
}

inline void AppendUint64(std::string *buffer, uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        buffer->push_back(static_cast<char>((value >> shift) & 0xff));
    }
}

inline bool ReadUint16(const char *data, std::size_t len, uint16_t *value)
{
    if (len < 2 || value == nullptr)
    {
        return false;
    }

    *value = (static_cast<uint16_t>(static_cast<unsigned char>(data[0])) << 8) |
             static_cast<uint16_t>(static_cast<unsigned char>(data[1]));
    return true;
}

inline bool ReadUint32(const char *data, std::size_t len, uint32_t *value)
{
    if (len < 4 || value == nullptr)
    {
        return false;
    }

    *value = (static_cast<uint32_t>(static_cast<unsigned char>(data[0])) << 24) |
             (static_cast<uint32_t>(static_cast<unsigned char>(data[1])) << 16) |
             (static_cast<uint32_t>(static_cast<unsigned char>(data[2])) << 8) |
             static_cast<uint32_t>(static_cast<unsigned char>(data[3]));
    return true;
}

inline bool ReadUint64(const char *data, std::size_t len, uint64_t *value)
{
    if (len < 8 || value == nullptr)
    {
        return false;
    }

    uint64_t result = 0;
    for (std::size_t i = 0; i < 8; ++i)
    {
        result = (result << 8) | static_cast<uint64_t>(static_cast<unsigned char>(data[i]));
    }
    *value = result;
    return true;
}

inline std::string SerializeFrameHeader(const RpcFrameHeader &header)
{
    std::string buffer;
    buffer.reserve(kRpcFrameHeaderSize);
    AppendUint32(&buffer, header.magic);
    AppendUint16(&buffer, header.version);
    AppendUint16(&buffer, header.message_type);
    AppendUint64(&buffer, header.request_id);
    AppendUint32(&buffer, header.metadata_size);
    AppendUint32(&buffer, header.body_size);
    return buffer;
}

inline bool ParseFrameHeader(const char *data, std::size_t len, RpcFrameHeader *header)
{
    if (header == nullptr || len < kRpcFrameHeaderSize)
    {
        return false;
    }

    return ReadUint32(data, 4, &header->magic) &&
           ReadUint16(data + 4, 2, &header->version) &&
           ReadUint16(data + 6, 2, &header->message_type) &&
           ReadUint64(data + 8, 8, &header->request_id) &&
           ReadUint32(data + 16, 4, &header->metadata_size) &&
           ReadUint32(data + 20, 4, &header->body_size);
}

inline bool ValidateFrameHeader(const RpcFrameHeader &header, uint16_t expected_message_type, std::string *error_text)
{
    if (header.magic != kRpcMagic)
    {
        if (error_text != nullptr)
        {
            *error_text = "invalid rpc magic";
        }
        return false;
    }

    if (header.version != kRpcProtocolVersion)
    {
        if (error_text != nullptr)
        {
            *error_text = "rpc protocol version mismatch";
        }
        return false;
    }

    if (header.message_type != expected_message_type)
    {
        if (error_text != nullptr)
        {
            *error_text = "unexpected rpc message type";
        }
        return false;
    }

    if (header.metadata_size > kMaxRpcMetadataSize)
    {
        if (error_text != nullptr)
        {
            *error_text = "rpc metadata too large";
        }
        return false;
    }

    if (header.body_size > kMaxRpcBodySize)
    {
        if (error_text != nullptr)
        {
            *error_text = "rpc body too large";
        }
        return false;
    }

    return true;
}

inline std::string BuildFrame(const RpcFrameHeader &header, const std::string &metadata, const std::string &body)
{
    std::string buffer = SerializeFrameHeader(header);
    buffer += metadata;
    buffer += body;
    return buffer;
}

inline std::string SerializeResponseMeta(const RpcResponseMeta &meta)
{
    std::string buffer;
    buffer.reserve(8 + meta.error_text.size());
    AppendUint32(&buffer, meta.status_code);
    AppendUint32(&buffer, static_cast<uint32_t>(meta.error_text.size()));
    buffer += meta.error_text;
    return buffer;
}

inline bool ParseResponseMeta(const std::string &buffer, RpcResponseMeta *meta)
{
    if (meta == nullptr || buffer.size() < 8)
    {
        return false;
    }

    uint32_t status_code = 0;
    uint32_t error_size = 0;
    if (!ReadUint32(buffer.data(), 4, &status_code) ||
        !ReadUint32(buffer.data() + 4, 4, &error_size))
    {
        return false;
    }

    if (buffer.size() != static_cast<std::size_t>(8 + error_size))
    {
        return false;
    }

    meta->status_code = status_code;
    meta->error_text.assign(buffer.data() + 8, error_size);
    return true;
}
} // namespace mprpc
