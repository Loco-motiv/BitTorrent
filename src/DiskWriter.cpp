#include "DiskWriter.h"

DiskWriter::DiskWriter(PieceQueue& l_pieceQueue, DiskWriterErrorQueue& l_errorQueue,
                       size_t l_pieceSize, size_t l_fileSize, std::filesystem::path l_path)
    : m_pieceQueue(l_pieceQueue), m_errorQueue(l_errorQueue),
      m_pieceSize(l_pieceSize), m_fileSize(l_fileSize)
{
    if (fs::exists(l_path))
    {
        m_errorQueue.push({ DiskWriterErrorCode::FileExists,
                            "File at path " + l_path.string() + " already exists" });
        return;
    }

    if (l_path.has_parent_path())
    {
        std::error_code dir_ec;
        fs::create_directories(l_path.parent_path(), dir_ec);
        if (dir_ec)
        {
            m_errorQueue.push({ DiskWriterErrorCode::OSError,
                                "Failed to create directory: " + dir_ec.message() });
            return;
        }
    }

    m_file.open(l_path, std::ios_base::binary);
    if (!m_file)
    {
        m_errorQueue.push({
            DiskWriterErrorCode::OSError,
            { " Failed to open file at path " + l_path.string(), std::string(strerror(errno)) }
        });
        return;
    }
    std::error_code ec;
    fs::resize_file(l_path, m_fileSize, ec);
    if (ec)
    {
        m_errorQueue.push({ DiskWriterErrorCode::OSError,
                            "Failed to resize file at path " + l_path.string() });
    }

    try
    {
        m_thread = std::jthread([this](std::stop_token stoken)
                                { workerLoop(stoken); });
    }
    catch (const std::system_error& e)
    {
        m_errorQueue.push({ DiskWriterErrorCode::OSError,
                            "Failed to start DiskWriter thread" });
    }
}

bool DiskWriter::isFinished()
{
    return m_finished;
}

void DiskWriter::workerLoop(std::stop_token stoken)
{
    m_writtenSize = 0;
    while (m_writtenSize < m_fileSize and !stoken.stop_requested())
    {
        Piece piece = m_pieceQueue.waitPop();
        m_file.seekp(piece.index * m_pieceSize);
        m_file.write(piece.data.data(), piece.data.size());
        if (m_file.fail())
        {
            m_errorQueue.push({ DiskWriterErrorCode::OSError,
                                "Failed to write to output file" });
            return;
        }
        m_writtenSize += piece.data.size();
        std::cout << "[" << std::fixed << std::setprecision(2) << std::setw(6)
                  << (static_cast<double>(m_writtenSize) / m_fileSize) * 100.0f << "%] "
                  << "Got piece number " << piece.index << "\n";
    }
    std::cout << "Download complete!" << '\n';
    m_finished = true;
}
