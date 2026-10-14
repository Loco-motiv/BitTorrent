#pragma once

#include "DiskWriter.h"
#include "Error.h"
#include "Socket.h"
#include "Utils.h"

#include <algorithm>
#include <cstring>
#include <expected>

enum PeerClientErrorCode
{
    OSError,
    PeerError,
    NoMoreIPs,
};

using PeerClientError      = Error<PeerClientErrorCode>;
using PeerClientErrorQueue = Queue<PeerClientError>;

struct PieceRequest
{
    size_t index;
    std::string hash;
};

struct PeerIP
{
    std::string ip;
    std::string port;
};

using PieceRequestQueue = Queue<PieceRequest>;
using PeerIPQueue       = Queue<PeerIP>;

enum class MessageType
{
    KeepAlive     = -1,
    Choke         = 0,
    Unchoke       = 1,
    Interested    = 2,
    NotInterested = 3,
    Have          = 4,
    Bitfield      = 5,
    Request       = 6,
    Piece         = 7,
    Cancel        = 8,
};

struct Message
{
    MessageType type;
    std::optional<std::string> payload;
};

class PeerClient
{

public:
    PeerClient(PieceQueue& l_pieceQueue, PeerClientErrorQueue& l_peerErrorQueue,
               PieceRequestQueue& l_pieceRequestQueue, PeerIPQueue& l_peerIPQueue,
               size_t l_pieceSize, size_t l_fileSize,
               std::string l_infoHash, std::string l_peerID);

private:
    void workerLoop(std::stop_token stoken);
    bool handleSocketError(SocketError&& error, const std::string& contextMsg);
    std::string getHandshakeString();
    bool isHandshakeResponseValid(std::string_view response);
    std::expected<void, PeerClientError> sendMessage(Socket& soc, const Message& message);
    std::expected<Message, PeerClientError> getMessage(Socket& soc);
    std::expected<void, PeerClientError> requestBlocks(Socket& soc, uint32_t pieceIndex);
    uint32_t getInteger(std::string_view integer);
    std::string makeInteger(uint32_t integer);
    uint32_t getPieceSize(uint32_t pieceIndex) const;
    uint32_t getBlockCount(uint32_t pieceIndex) const;
    uint32_t getBlockSize(uint32_t pieceIndex, uint32_t index) const;

    PieceQueue& m_pieceQueue;
    PeerClientErrorQueue& m_peerErrorQueue;
    PieceRequestQueue& m_pieceRequestQueue;
    PeerIPQueue& m_peerIPQueue;
    size_t m_pieceSize;
    size_t m_fileSize;
    std::string m_infoHash;
    std::string m_peerID;
    std::jthread m_thread;

    const uint32_t BLOCK_SIZE  = 16384; //* 16 KB
    const uint8_t QUEUING_SIZE = 5;

    bool m_amChoking          = true;
    bool m_amInterested       = false;
    bool m_peerChoking        = true;
    bool m_peerInterested     = false;
    bool m_breakWhile         = false;
    uint32_t m_blockToRequest = 0;
    uint32_t m_blocksReceived = 0;
    std::string m_pieceBuffer;
};
