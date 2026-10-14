#pragma once

#include "Error.h"
#include "Queue.h"

#include <atomic>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string.h>
#include <system_error>
#include <thread>

namespace fs = std::filesystem;

enum class DiskWriterErrorCode
{
    FileExists,
    OSError,
};

using DiskWriterError = Error<DiskWriterErrorCode>;

struct Piece
{
    size_t index;
    std::string data;
};

using DiskWriterErrorQueue = Queue<DiskWriterError>;
using PieceQueue           = Queue<Piece>;

class DiskWriter
{
public:
    DiskWriter(PieceQueue& l_pieceQueue, DiskWriterErrorQueue& l_errorQueue,
               size_t l_pieceSize, size_t l_fileSize, std::filesystem::path l_path);
    bool isFinished();

private:
    void workerLoop(std::stop_token stoken);
    PieceQueue& m_pieceQueue;
    DiskWriterErrorQueue& m_errorQueue;
    size_t m_pieceSize;
    size_t m_fileSize;
    size_t m_writtenSize = 0;
    std::ofstream m_file;
    std::jthread m_thread;
    std::atomic<bool> m_finished = false;
};