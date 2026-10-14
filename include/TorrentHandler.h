#pragma once

#include "DiskWriter.h"
#include "PeerClient.h"
#include "TrackerClient.h"

enum class TorrentHandlerErrorCode
{
    PeerError,
    DiskWriterError,
};

struct TorrentSettings : ClientSettings
{
    uint16_t peerClientCount;
    std::string outputDirName;
};

using TorrentHandlerError = Error<TorrentHandlerErrorCode>;

class TorrentHandler
{
public:
    TorrentHandler(TorrentSettings l_settings) : m_settings(l_settings) {};

    std::expected<void, TrackerError> loadTorrentAndPeers(fs::path torrentPath);
    std::expected<void, TorrentHandlerError> downloadFile();

private:
    TorrentSettings m_settings;
    TrackerClient m_trackerClient;
};