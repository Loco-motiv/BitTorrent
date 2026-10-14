#pragma once

#include "Error.h"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

enum class SocketErrorCode
{
    HostNotFound,
    ResolutionFailed,
    ConnectionRefused,
    ConnectionReset,
    TimedOut,
    NetworkUnreachable,
    AddressInUse,
    NotConnected,
    UnexpectedEof,
    SystemError,
};

using SocketError = Error<SocketErrorCode>;

class Socket
{
public:
    static std::expected<Socket, SocketError> connect(std::string host, std::string port);
    ~Socket();

    Socket(const Socket&)            = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) : m_fd(other.m_fd) { other.m_fd = -1; };
    Socket& operator=(Socket&& other);

    std::expected<void, SocketError> send(std::string_view data);
    std::expected<std::string, SocketError> receiveAll();
    std::expected<std::string, SocketError> receiveExact(size_t size);

private:
    Socket(int l_fd) : m_fd(l_fd) {}
    static SocketErrorCode translateErrno(int err);
    static SocketErrorCode translateGaiError(int gaiStatus);

    int m_fd;
};