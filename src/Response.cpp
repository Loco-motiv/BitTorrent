#include "Response.h"

std::expected<Response, ResponseError> Response::parse(std::string input)
{
    auto parseResult = BencodeParser::parse(std::move(input));
    if (!parseResult)
    {
        return std::unexpected(ResponseError(
            ResponseErrorCode::BencodeParseError,
            std::move(parseResult.error()),
            "Failed to parse response body"));
    }

    Response response(std::move(*parseResult));
    BencodeReader reader(&response.m_data.root());

    response.failureReason = reader.optionalString("failure reason");
    if (response.hasError())
    {
        return std::unexpected(ResponseError(
            ResponseErrorCode::TrackerError,
            { std::string(*response.failureReason), "Error in tracker response" }));
    }

    response.interval    = reader.requireInt("interval");
    response.minInterval = reader.optionalInt("min interval");
    response.trackerID   = reader.optionalString("tracker id");
    response.complete    = reader.optionalInt("complete");
    response.incomplete  = reader.optionalInt("incomplete");

    if (reader.hasError())
    {
        return std::unexpected(ResponseError(
            ResponseErrorCode::InvalidDataType,
            { reader.getError(), "Error parsing response" }));
    }

    auto peers = response.m_data.root().findInDictionary("peers");
    if (!peers)
    {
        return std::unexpected(ResponseError(
            ResponseErrorCode::MissingRequiredKey,
            "Missing required key 'peers'"));
    }

    if (peers->isString())
    {
        response.peersBinary = *peers->getString();
    }
    else if (peers->isList())
    {
        std::vector<std::pair<std::string_view, int64_t>> peersTemp;
        for (const auto& listElement : *peers->getList())
        {
            if (!listElement.isDictionary())
            {
                return std::unexpected(ResponseError(
                    ResponseErrorCode::InvalidDataType,
                    "Element of 'peers' must be a dictionary"));
            }

            BencodeReader listReader(&listElement);
            std::string_view ip = listReader.requireString("ip");
            int64_t port        = listReader.requireInt("port");

            if (listReader.hasError())
            {
                return std::unexpected(ResponseError(
                    ResponseErrorCode::InvalidDataType,
                    { listReader.getError(), "Error parsing peers list" }));
            }

            peersTemp.emplace_back(ip, port);
        }
        response.peersDict = std::move(peersTemp);
    }
    else
    {
        return std::unexpected(ResponseError(
            ResponseErrorCode::InvalidDataType,
            "Value of 'peers' must be either string or list"));
    }

    return response;
}