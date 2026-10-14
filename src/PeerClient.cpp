#include "PeerClient.h"

PeerClient::PeerClient(PieceQueue& l_pieceQueue, PeerClientErrorQueue& l_peerErrorQueue,
                       PieceRequestQueue& l_pieceRequestQueue, PeerIPQueue& l_peerIPQueue,
                       size_t l_pieceSize, size_t l_fileSize,
                       std::string l_infoHash, std::string l_peerID)
    : m_pieceQueue(l_pieceQueue), m_peerErrorQueue(l_peerErrorQueue),
      m_pieceRequestQueue(l_pieceRequestQueue), m_peerIPQueue(l_peerIPQueue),
      m_pieceSize(l_pieceSize), m_fileSize(l_fileSize),
      m_infoHash(l_infoHash), m_peerID(l_peerID)
{
    try
    {
        m_thread = std::jthread([this](std::stop_token stoken)
                                { workerLoop(stoken); });
    }
    catch (const std::system_error& e)
    {
        m_peerErrorQueue.push({ PeerClientErrorCode::OSError,
                                "Failed to start PeerClient thread" });
    }
}

void PeerClient::workerLoop(std::stop_token stoken)
{
    while (!stoken.stop_requested())
    {
        if (m_pieceRequestQueue.empty())
        {
            return;
        }
        auto ip = m_peerIPQueue.pop();
        if (!ip.has_value())
        {
            m_peerErrorQueue.push({ PeerClientErrorCode::NoMoreIPs, "PeerIPQueue is empty" });
            return;
        }

        auto socExpected = Socket::connect(ip->ip, ip->port);
        if (!socExpected)
        {
            if (handleSocketError(std::move(socExpected.error()), "Connection failed"))
            {
                return;
            }
            continue;
        }

        Socket soc = std::move(socExpected.value());

        if (auto result = soc.send(getHandshakeString()); !result)
        {
            if (handleSocketError(std::move(result.error()), "Failed to send handshake"))
            {
                return;
            }
            continue;
        }

        auto handshakeResponse = soc.receiveExact(68);
        if (!handshakeResponse)
        {
            if (handleSocketError(std::move(handshakeResponse.error()),
                                  "Failed to receive handshake"))
            {
                return;
            }
            continue;
        }

        if (!isHandshakeResponseValid(handshakeResponse.value()))
        {
            continue;
        }

        m_amInterested   = false;
        m_peerChoking    = true;
        m_breakWhile     = false;
        m_blocksReceived = 0;
        m_pieceBuffer.clear();
        size_t pieceCount = (m_fileSize + m_pieceSize - 1) / m_pieceSize;
        std::vector<bool> bitfield(pieceCount);

        auto pieceRequest = m_pieceRequestQueue.pop();
        if (!pieceRequest)
        {
            return;
        }
        while (!stoken.stop_requested() and !m_breakWhile)
        {
            auto msgRes = getMessage(soc);
            if (!msgRes)
            {
                break;
            }

            const Message& msg = msgRes.value();

            switch (msg.type)
            {
            case MessageType::Bitfield:
            {
                if (!msg.payload or msg.payload->size() != (pieceCount + 7) / 8)
                {
                    m_breakWhile = true;
                    break;
                }
                for (size_t piece = 0; piece < msg.payload->size(); piece++)
                {
                    uint8_t word = static_cast<uint8_t>((*msg.payload)[piece]);
                    for (uint8_t i = 0; i < 8; i++)
                    {
                        if (piece * 8 + i < bitfield.size())
                        {
                            bitfield[piece * 8 + i] = (word >> (7 - i)) & 1;
                        }
                    }
                }
                if (bitfield[pieceRequest->index])
                {
                    sendMessage(soc, { MessageType::Interested, std::nullopt });
                    m_amInterested = true;
                }
                else
                {
                    m_breakWhile = true;
                }
                break;
            }
            case MessageType::Have:
            {
                if (!msg.payload or msg.payload->size() != 4 or
                    getInteger(*msg.payload) >= bitfield.size())
                {
                    m_breakWhile = true;
                    break;
                }
                bitfield[getInteger(*msg.payload)] = true;
                break;
            }

            case MessageType::Unchoke:
            {
                if (!m_peerChoking)
                {
                    break;
                }
                if (!m_amInterested)
                {
                    m_breakWhile = true;
                    break;
                }

                m_peerChoking = false;
                m_pieceBuffer.assign(getPieceSize(pieceRequest->index), '\0');
                m_blockToRequest = 0;

                auto res = requestBlocks(soc, pieceRequest->index);
                if (!res)
                {
                    m_breakWhile = true;
                    break;
                }
                break;
            }

            case MessageType::Choke:
            {
                m_peerChoking = true;
                m_breakWhile  = true;
                break;
            }

            case MessageType::Piece:
            {
                if (!msg.payload or msg.payload->size() < 8)
                {
                    m_breakWhile = true;
                    break;
                }
                uint32_t pieceIndex = getInteger(*msg.payload);

                if (pieceIndex != pieceRequest->index)
                {
                    m_breakWhile = true;
                    break;
                }

                uint32_t blockOffset = getInteger(msg.payload->substr(4, 4));
                uint32_t payloadSize = msg.payload->size() - 8;

                if (blockOffset + payloadSize > m_pieceBuffer.size())
                {
                    m_breakWhile = true;
                    break;
                }

                std::memcpy(m_pieceBuffer.data() + blockOffset, msg.payload->data() + 8, payloadSize);
                m_blocksReceived++;

                if (m_blocksReceived == getBlockCount(pieceIndex))
                {
                    if (pieceRequest->hash != SHA1(m_pieceBuffer))
                    {
                        m_breakWhile = true;
                        break;
                    }

                    m_pieceQueue.push({ pieceIndex, std::move(m_pieceBuffer) });
                    m_blocksReceived = 0;
                    m_blockToRequest = 0;

                    pieceRequest = m_pieceRequestQueue.pop();
                    if (!pieceRequest)
                    {
                        return;
                    }
                    if (!bitfield[pieceRequest->index])
                    {
                        m_breakWhile = true;
                        break;
                    }

                    m_pieceBuffer.assign(getPieceSize(pieceRequest->index), '\0');
                    auto res = requestBlocks(soc, pieceRequest->index);
                    if (!res)
                    {
                        m_breakWhile = true;
                        break;
                    }
                }
                else if (m_blockToRequest < getBlockCount(pieceIndex))
                {
                    auto res = requestBlocks(soc, pieceIndex);
                    if (!res)
                    {
                        m_breakWhile = true;
                        break;
                    }
                }
                break;
            }

            default:
                break;
            }
        }
        if (pieceRequest)
        {
            m_pieceRequestQueue.push(pieceRequest.value());
        }
    }
}

bool PeerClient::handleSocketError(SocketError&& error, const std::string& contextMsg)
{
    switch (error.code)
    {
    case SocketErrorCode::SystemError:
    case SocketErrorCode::AddressInUse:
    case SocketErrorCode::NotConnected:
        m_peerErrorQueue.push({ PeerClientErrorCode::OSError, std::move(error), contextMsg });
        return true; //* terminate
    default:
        m_peerErrorQueue.push({ PeerClientErrorCode::PeerError, std::move(error), contextMsg });
        return false; //* continue
    }
}

std::string PeerClient::getHandshakeString()
{
    std::string handshake;
    handshake.push_back(static_cast<char>(19));
    handshake += "BitTorrent protocol";
    handshake.append(8, '\0');
    handshake += m_infoHash;
    handshake += m_peerID;
    return handshake;
}

bool PeerClient::isHandshakeResponseValid(std::string_view response)
{
    if (response[0] != static_cast<char>(19) or
        response.substr(1, 19) != "BitTorrent protocol" or
        response.substr(28, 20) != m_infoHash)
    {
        return false;
    }
    return true;
}

std::expected<void, PeerClientError> PeerClient::sendMessage(Socket& soc, const Message& message)
{
    std::string buffer;

    if (message.type == MessageType::KeepAlive)
    {
        buffer.assign(4, '\0');
    }
    else
    {
        uint32_t length = 1 + (message.payload ? message.payload->size() : 0);

        buffer.reserve(4 + length);
        buffer += makeInteger(length);
        buffer.push_back(static_cast<char>(message.type));

        if (message.payload)
        {
            buffer += *message.payload;
        }
    }

    if (auto res = soc.send(buffer); !res)
    {
        return std::unexpected(PeerClientError(PeerClientErrorCode::PeerError,
                                               std::move(res.error()), "Failed to send message"));
    }

    return {};
}

std::expected<Message, PeerClientError> PeerClient::getMessage(Socket& soc)
{
    auto lenRes = soc.receiveExact(4);
    if (!lenRes)
    {
        return std::unexpected(PeerClientError(PeerClientErrorCode::PeerError,
                                               std::move(lenRes.error()), "Failed to receive message length"));
    }

    uint32_t length = getInteger(lenRes.value());
    if (length == 0)
    {
        return Message{ MessageType::KeepAlive, std::nullopt };
    }

    auto typeRes = soc.receiveExact(1);
    if (!typeRes)
    {
        return std::unexpected(PeerClientError(PeerClientErrorCode::PeerError,
                                               std::move(typeRes.error()), "Failed to receive message type"));
    }

    MessageType type = static_cast<MessageType>(typeRes.value()[0]);
    std::optional<std::string> payload;

    if (length > 1)
    {
        auto payloadRes = soc.receiveExact(length - 1);
        if (!payloadRes)
        {
            return std::unexpected(PeerClientError(PeerClientErrorCode::PeerError,
                                                   std::move(payloadRes.error()), "Failed to receive message payload"));
        }
        payload = std::move(payloadRes.value());
    }

    return Message{ type, std::move(payload) };
}

std::expected<void, PeerClientError> PeerClient::requestBlocks(Socket& soc, uint32_t pieceIndex)
{
    uint32_t blockCount = getBlockCount(pieceIndex);
    uint32_t limit      = std::min(m_blocksReceived + QUEUING_SIZE, blockCount);

    for (; m_blockToRequest < limit; m_blockToRequest++)
    {
        std::string message;
        message.reserve(12);
        message  += makeInteger(pieceIndex);
        message  += makeInteger(m_blockToRequest * BLOCK_SIZE);
        message  += makeInteger(getBlockSize(pieceIndex, m_blockToRequest));
        auto res  = sendMessage(soc, { MessageType::Request, message });
        if (!res)
        {
            return res;
        }
    }
    return {};
}

uint32_t PeerClient::getInteger(std::string_view integer)
{
    const unsigned char* temp = reinterpret_cast<const unsigned char*>(integer.data());
    return (static_cast<uint32_t>(temp[0]) << 24) |
           (static_cast<uint32_t>(temp[1]) << 16) |
           (static_cast<uint32_t>(temp[2]) << 8) |
           (static_cast<uint32_t>(temp[3]));
}

std::string PeerClient::makeInteger(uint32_t integer)
{
    std::string res(4, '\0');
    res[0] = static_cast<char>(integer >> 24);
    res[1] = static_cast<char>(integer >> 16);
    res[2] = static_cast<char>(integer >> 8);
    res[3] = static_cast<char>(integer);
    return res;
}

uint32_t PeerClient::getPieceSize(uint32_t pieceIndex) const
{
    uint64_t pieceOffset = static_cast<uint64_t>(pieceIndex) * m_pieceSize;
    uint64_t remaining   = m_fileSize - pieceOffset;

    return static_cast<uint32_t>(std::min(static_cast<uint64_t>(m_pieceSize), remaining));
}

uint32_t PeerClient::getBlockCount(uint32_t pieceIndex) const
{
    uint32_t pieceSize = getPieceSize(pieceIndex);

    return (pieceSize + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

uint32_t PeerClient::getBlockSize(uint32_t pieceIndex, uint32_t index) const
{
    uint32_t pieceSize = getPieceSize(pieceIndex);
    uint32_t remaining = pieceSize - index * BLOCK_SIZE;

    return std::min(BLOCK_SIZE, remaining);
}