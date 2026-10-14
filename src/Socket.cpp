#include "Socket.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>

std::expected<Socket, SocketError> Socket::connect(std::string host, std::string port)
{
    addrinfo hints{};
    addrinfo* servinfo = nullptr;
    hints.ai_family    = AF_UNSPEC;
    hints.ai_socktype  = SOCK_STREAM;

    if (int status = getaddrinfo(host.c_str(), port.c_str(), &hints, &servinfo); status != 0)
    {
        return std::unexpected(SocketError(translateGaiError(status),
                                           { gai_strerror(status), "Failed to resolve host: " + host }));
    }

    int lastErrno = 0;
    int fd        = -1;

    for (addrinfo* p = servinfo; p != nullptr; p = p->ai_next)
    {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == -1)
        {
            lastErrno = errno;
            continue;
        }

        struct timeval tv = { 5, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (::connect(fd, p->ai_addr, p->ai_addrlen) == -1)
        {
            lastErrno = errno;
            close(fd);
            fd = -1;
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo);

    if (fd == -1)
    {
        SocketErrorCode code   = (lastErrno != 0) ? translateErrno(lastErrno) : SocketErrorCode::NetworkUnreachable;
        std::string errMessage = (lastErrno != 0)
                                     ? std::generic_category().message(lastErrno)
                                     : "Connection attempt failed for all resolved IPs";

        return std::unexpected(SocketError(
            code,
            { std::move(errMessage), "Failed to connect to " + host + ":" + port }));
    }

    return Socket(fd);
}

Socket::~Socket()
{
    if (m_fd != -1)
    {
        close(m_fd);
    }
}

Socket& Socket::operator=(Socket&& other)
{
    if (this != &other)
    {
        if (m_fd != -1)
        {
            close(m_fd);
        }
        m_fd       = other.m_fd;
        other.m_fd = -1;
    }
    return *this;
}

std::expected<void, SocketError> Socket::send(std::string_view data)
{
    if (m_fd == -1)
    {
        return std::unexpected(SocketError(SocketErrorCode::NotConnected, "Socket is not connected"));
    }

    size_t totalSend = 0;
    while (totalSend < data.size())
    {
        ssize_t sent = ::send(m_fd, data.data() + totalSend, data.size() - totalSend, MSG_NOSIGNAL);

        if (sent == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return std::unexpected(SocketError(
                translateErrno(errno),
                { std::generic_category().message(errno), "Failed to send data" }));
        }
        totalSend += sent;
    }
    return {};
}

std::expected<std::string, SocketError> Socket::receiveAll()
{
    if (m_fd == -1)
    {
        return std::unexpected(SocketError(SocketErrorCode::NotConnected, "Socket is not connected"));
    }

    std::string buffer;
    char chunk[4096];

    while (true)
    {
        ssize_t bytes_read = recv(m_fd, chunk, sizeof(chunk), 0);
        if (bytes_read > 0)
        {
            buffer.append(chunk, bytes_read);
        }
        else if (bytes_read == 0)
        {
            break;
        }
        else
        {
            if (errno == EINTR)
            {
                continue;
            }
            return std::unexpected(SocketError(
                translateErrno(errno),
                { std::generic_category().message(errno), "Error while reading from socket" }));
        }
    }
    return buffer;
}

std::expected<std::string, SocketError> Socket::receiveExact(size_t size)
{
    if (m_fd == -1)
    {
        return std::unexpected(SocketError(SocketErrorCode::NotConnected, "Socket is not connected"));
    }

    std::string buffer;
    buffer.resize(size);
    size_t totalRead = 0;

    while (totalRead < size)
    {
        ssize_t bytes_read = recv(m_fd, buffer.data() + totalRead, size - totalRead, 0);
        if (bytes_read > 0)
        {
            totalRead += bytes_read;
        }
        else if (bytes_read == 0)
        {
            return std::unexpected(SocketError(
                SocketErrorCode::UnexpectedEof,
                "Connection closed before receiving requested size"));
        }
        else
        {
            if (errno == EINTR)
            {
                continue;
            }
            return std::unexpected(SocketError(
                translateErrno(errno),
                { std::generic_category().message(errno), "Failed to read exact size from socket" }));
        }
    }

    return buffer;
}

SocketErrorCode Socket::translateGaiError(int gaiStatus)
{
    switch (gaiStatus)
    {
    case EAI_NONAME:
    case EAI_NODATA:
        return SocketErrorCode::HostNotFound;
    default:
        return SocketErrorCode::ResolutionFailed;
    }
}

SocketErrorCode Socket::translateErrno(int err)
{
    switch (err)
    {
    case ECONNREFUSED:
        return SocketErrorCode::ConnectionRefused;
    case ECONNRESET:
    case EPIPE:
        return SocketErrorCode::ConnectionReset;
    case ETIMEDOUT:
    case EAGAIN:
    case EINPROGRESS:
        return SocketErrorCode::TimedOut;
    case ENETUNREACH:
    case EHOSTUNREACH:
    case ENETDOWN:
        return SocketErrorCode::NetworkUnreachable;
    case EADDRINUSE:
        return SocketErrorCode::AddressInUse;
    default:
        return SocketErrorCode::SystemError;
    }
}