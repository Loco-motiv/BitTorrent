#include "TrackerClient.h"

std::expected<TrackerClient, TrackerError> TrackerClient::create(fs::path metaPath,
                                                                 ClientSettings l_settings)
{
    std::error_code ec;
    if (!fs::exists(metaPath, ec))
    {
        return std::unexpected(TrackerError(
            TrackerErrorCode::FileDoesNotExist,
            "File at path " + metaPath.string() + " doesn't exist"));
    }
    if (ec)
    {
        return std::unexpected(TrackerError(
            TrackerErrorCode::SystemError,
            { ec.category().name() + ':' + std::to_string(ec.value()), ec.message(),
              "Error with file at path " + metaPath.string() }));
    }
    if (!fs::is_regular_file(metaPath, ec))
    {
        return std::unexpected(TrackerError(
            TrackerErrorCode::FileIsNotRegularFile,
            "File at path " + metaPath.string() + " isn't regular"));
    }
    if (ec)
    {
        return std::unexpected(TrackerError(
            TrackerErrorCode::SystemError,
            { ec.category().name() + ':' + std::to_string(ec.value()), ec.message(),
              "Error with file at path " + metaPath.string() }));
    }

    std::ifstream f(metaPath, std::ios_base::binary | std::ios_base::in);
    if (!f)
    {
        return std::unexpected(TrackerError(
            TrackerErrorCode::FileOpenError,
            "Failed to open file at path " + metaPath.string()));
    }

    std::uintmax_t fileSize = fs::file_size(metaPath);
    std::string torrent(fileSize, '\0');
    f.read(torrent.data(), fileSize);

    auto metaInfo = Metainfo::parse(std::move(torrent));
    if (!metaInfo)
    {
        auto code = (metaInfo.error().code == MetainfoErrorCode::BencodeParseError)
                        ? TrackerErrorCode::BencodeParseError
                        : TrackerErrorCode::MetainfoError;

        return std::unexpected(TrackerError(
            code,
            std::move(metaInfo.error()),
            "Error creating tracker's client"));
    }

    TrackerClient client(std::move(metaInfo.value()), l_settings);

    if (client.m_meta.isSingleFile())
    {
        client.m_left = client.m_meta.m_length.value();
    }
    else
    {
        // TODO multiple files
    }

    client.m_host     = client.m_meta.m_parsedAnnounce.host;
    client.m_announce = client.m_meta.m_parsedAnnounce.announce;

    std::string request = client.buildRequest();

    auto socket = Socket::connect(client.m_host, "80");
    if (!socket)
    {
        return std::unexpected(TrackerError(
            TrackerErrorCode::SocketError,
            std::move(socket.error()),
            "Error connecting to tracker"));
    }

    if (auto result = socket->send(request); !result)
    {
        return std::unexpected(TrackerError(
            TrackerErrorCode::SocketError,
            std::move(result.error()),
            "Error sending request to tracker"));
    }

    auto res = socket->receiveAll();
    if (!res)
    {
        return std::unexpected(TrackerError(
            TrackerErrorCode::SocketError,
            std::move(res.error()),
            "Error getting response from tracker"));
    }

    auto responseBody = client.parseResponse(*res);
    if (!responseBody)
    {
        return std::unexpected(std::move(responseBody.error()).withContext("Error trying to get body of request"));
    }

    auto response = Response::parse(std::move(*responseBody));
    if (!response)
    {
        auto code = (response.error().code == ResponseErrorCode::BencodeParseError)
                        ? TrackerErrorCode::BencodeParseError
                        : TrackerErrorCode::MetainfoError;

        return std::unexpected(TrackerError(
            code,
            std::move(response.error()),
            "Error parsing tracker response"));
    }

    client.m_interval = response->interval;
    if (response->minInterval.has_value())
    {
        client.m_minInterval = response->minInterval;
    }
    client.m_complete   = response->complete;
    client.m_incomplete = response->incomplete;
    if (response->trackerID.has_value())
    {
        client.m_trackerID = response->trackerID.value();
    }

    if (response->isPeersDict())
    {
        for (const auto& [ip, port] : response->peersDict.value())
        {
            client.m_peers.emplace_back(std::string(ip), std::to_string(port));
        }
    }
    else if (response->isPeersBinary())
    {
        client.parseBinaryPeers(response->peersBinary.value());
    }

    client.m_infoHash = SHA1(client.m_meta.m_infoString);

    return client;
}

std::vector<std::pair<std::string, std::string>>& TrackerClient::getPeers()
{
    return m_peers;
}

std::string TrackerClient::getInfoHash()
{
    return m_infoHash;
}

size_t TrackerClient::getPieceSize()
{
    return m_meta.m_pieceLength;
}

size_t TrackerClient::getFileSize()
{
    return m_meta.m_length.value(); // TODO compute size and store in trackerClient
}

std::string TrackerClient::getFileName()
{
    return std::string(m_meta.m_name);
}

std::string_view TrackerClient::getPieces()
{
    return m_meta.m_pieces;
}

void TrackerClient::stopEvent()
{
    m_event = "stop";
    // TODO stop
}

void TrackerClient::completeEvent()
{
    m_event = "complete";
    // TODO complete
}

std::string TrackerClient::buildRequest()
{
    std::string request  = "GET /" + std::string(m_announce);
    request             += (m_announce.find('?') == std::string_view::npos) ? "?" : "&";
    request             += "info_hash=" + URLEncode(SHA1(m_meta.m_infoString));
    request             += "&peer_id=" + URLEncode(m_settings.peerID);
    request             += "&port=" + std::to_string(m_settings.port);
    request             += "&uploaded=" + std::to_string(m_uploaded);
    request             += "&downloaded=" + std::to_string(m_downloaded);
    request             += "&left=" + std::to_string(m_left);
    request             += "&compact=" + std::to_string(m_settings.compact ? 1 : 0);
    request             += "&event=" + m_event;
    request             += "&numwant=" + std::to_string(m_settings.numwant);

    if (!m_trackerID.empty())
    {
        request += "&trackerid=" + m_trackerID;
    }

    request += " HTTP/1.1\r\n";
    request += "Host: " + m_host + "\r\n";
    request += "User-Agent: MeTorrent/0.0.1\r\n";
    request += "Connection: close\r\n\r\n";

    return request;
}

void TrackerClient::parseBinaryPeers(std::string_view response)
{
    for (size_t i = 0; i + 6 <= response.size(); i += 6)
    {
        std::string ip   = std::to_string(static_cast<uint8_t>(response[i])) + "." +
                           std::to_string(static_cast<uint8_t>(response[i + 1])) + "." +
                           std::to_string(static_cast<uint8_t>(response[i + 2])) + "." +
                           std::to_string(static_cast<uint8_t>(response[i + 3]));
        std::string port = std::to_string((static_cast<uint8_t>(response[i + 4]) << 8) |
                                          (static_cast<uint8_t>(response[i + 5])));
        m_peers.emplace_back(ip, port);
    }
}

std::expected<std::string, TrackerError> TrackerClient::parseResponse(std::string_view response)
{
    size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string_view::npos)
    {
        return std::unexpected(TrackerError(
            TrackerErrorCode::InvalidResponse,
            "Response doesn't contain a valid HTTP body"));
    }

    size_t firstLineEnd         = response.find("\r\n");
    std::string_view statusLine = response.substr(0, firstLineEnd);

    if (statusLine.find(" 200 ") == std::string_view::npos)
    {
        return std::unexpected(TrackerError(
            TrackerErrorCode::InvalidResponse,
            "HTTP Error from tracker: " + std::string(statusLine)));
    }

    return std::string(response.substr(headerEnd + 4));
}