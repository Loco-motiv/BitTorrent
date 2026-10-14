#include "TorrentHandler.h"

std::expected<void, TrackerError> TorrentHandler::loadTorrentAndPeers(fs::path torrentPath)
{
    auto result = TrackerClient::create(torrentPath, m_settings);
    if (!result)
    {
        return std::unexpected(result.error());
    }
    m_trackerClient = std::move(result.value());
    return {};
}

std::expected<void, TorrentHandlerError> TorrentHandler::downloadFile()
{
    PieceQueue pieceQueue;
    PieceRequestQueue pieceRequestQueue;
    PeerIPQueue peerIPQueue;
    DiskWriterErrorQueue diskWriterErrorQueue;
    PeerClientErrorQueue peerClientErrorQueue;

    std::string outputName = "output/";
    if (m_settings.outputDirName.size() != 0)
    {
        outputName += m_settings.outputDirName + "/";
    }
    outputName += m_trackerClient.getFileName();

    DiskWriter diskWriter(pieceQueue, diskWriterErrorQueue, m_trackerClient.getPieceSize(),
                          m_trackerClient.getFileSize(), outputName);
    auto& vec = m_trackerClient.getPeers();
    for (const auto& [ip, port] : vec)
    {
        peerIPQueue.push({ ip, port });
    }
    std::string_view pieces = m_trackerClient.getPieces();
    for (size_t offset = 0; offset * 20 < pieces.size(); offset++)
    {
        pieceRequestQueue.push({ offset, std::string(pieces.substr(offset * 20, 20)) });
    }

    std::vector<PeerClient> peerClients;
    peerClients.reserve(m_settings.peerClientCount);
    for (uint16_t i = 0; i < m_settings.peerClientCount; i++)
    {
        peerClients.emplace_back(pieceQueue, peerClientErrorQueue, pieceRequestQueue, peerIPQueue,
                                 m_trackerClient.getPieceSize(), m_trackerClient.getFileSize(),
                                 m_trackerClient.getInfoHash(), m_settings.peerID);
    }

    uint16_t activePeersCount = m_settings.peerClientCount;
    while (!diskWriter.isFinished())
    {
        if (auto res = diskWriterErrorQueue.pop())
        {
            return std::unexpected(TorrentHandlerError(TorrentHandlerErrorCode::DiskWriterError, res->message));
        }
        if (auto res = peerClientErrorQueue.pop())
        {
            if (res->code == PeerClientErrorCode::NoMoreIPs or
                res->code == PeerClientErrorCode::OSError)
            {
                activePeersCount--;
            }

            std::cout << res.value();

            if (activePeersCount == 0)
            {
                return std::unexpected(TorrentHandlerError(TorrentHandlerErrorCode::PeerError,
                                                           "All peers encountered an error"));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return {};
}
