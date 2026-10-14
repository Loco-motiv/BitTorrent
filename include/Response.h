#pragma once

#include "Bencode.h"

enum class ResponseErrorCode
{
    BencodeParseError,
    MissingRequiredKey,
    InvalidDataType,
    TrackerError,
};

using ResponseError = Error<ResponseErrorCode>;

class Response
{
public:
    static std::expected<Response, ResponseError> parse(std::string input);

    Response(const Response&)            = delete;
    Response& operator=(const Response&) = delete;
    Response(Response&&)                 = default;
    Response& operator=(Response&&)      = default;

    std::optional<std::string_view> failureReason;
    std::optional<std::string_view> warningMessage;
    int64_t interval;
    std::optional<int64_t> minInterval;
    std::optional<std::string_view> trackerID;
    std::optional<int64_t> complete;
    std::optional<int64_t> incomplete;
    std::optional<std::vector<std::pair<std::string_view, int64_t>>> peersDict;
    std::optional<std::string_view> peersBinary;
    bool isPeersDict() const { return peersDict.has_value(); }
    bool isPeersBinary() const { return peersBinary.has_value(); }
    bool hasError() const { return failureReason.has_value(); }

private:
    Response(BencodeDocument doc) : m_data(std::move(doc)) {}
    BencodeDocument m_data;
};