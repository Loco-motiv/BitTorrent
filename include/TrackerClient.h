#pragma once

#include "Metainfo.h"
#include "Response.h"
#include "Socket.h"
#include "Utils.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

struct ClientSettings
{
    std::string peerID;
    uint16_t port;
    uint8_t compact;
    uint16_t numwant;
};

enum class TrackerErrorCode
{
    BencodeParseError,
    MetainfoError,
    SocketError,
    InvalidResponse,
    FileDoesNotExist,
    FileIsNotRegularFile,
    FileOpenError,
    SystemError,
};

using TrackerError = Error<TrackerErrorCode>;

class TrackerClient
{
public:
    TrackerClient() {}
    static std::expected<TrackerClient, TrackerError> create(fs::path metaPath,
                                                             ClientSettings l_settings);

    TrackerClient(const TrackerClient&)            = delete;
    TrackerClient& operator=(const TrackerClient&) = delete;
    TrackerClient(TrackerClient&&)                 = default;
    TrackerClient& operator=(TrackerClient&&)      = default;

    std::vector<std::pair<std::string, std::string>>& getPeers();
    std::string getInfoHash();
    size_t getPieceSize();
    size_t getFileSize();
    std::string getFileName();
    std::string_view getPieces();

    void stopEvent();
    void completeEvent();

private:
    TrackerClient(Metainfo l_meta, ClientSettings l_settings)
        : m_meta(std::move(l_meta)), m_settings(l_settings) {};
    std::string buildRequest();
    void parseBinaryPeers(std::string_view response);
    std::expected<std::string, TrackerError> parseResponse(std::string_view response);

    Metainfo m_meta;
    ClientSettings m_settings;
    std::string m_host;
    std::string m_announce;
    uint64_t m_uploaded   = 0;
    uint64_t m_downloaded = 0;
    uint64_t m_left;
    std::string m_trackerID;
    uint64_t m_interval;
    std::optional<uint64_t> m_minInterval;
    std::optional<uint64_t> m_complete;
    std::optional<uint64_t> m_incomplete;
    std::string m_event = "started";
    std::vector<std::pair<std::string, std::string>> m_peers;
    std::string m_infoHash;
};