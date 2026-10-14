#pragma once

#include "Bencode.h"

enum class MetainfoErrorCode
{
    BencodeParseError,
    MissingRequiredKey,
    InvalidDataType,
    InvalidAnnounce,
};

using MetainfoError = Error<MetainfoErrorCode>;

struct parsedAnnounce
{
    std::string_view protocol;
    std::string_view host;
    std::string_view announce;
};

class Metainfo
{
public:
    Metainfo() {}
    static std::expected<Metainfo, MetainfoError> parse(std::string input);

    Metainfo(const Metainfo&)            = delete;
    Metainfo& operator=(const Metainfo&) = delete;
    Metainfo(Metainfo&&)                 = default;
    Metainfo& operator=(Metainfo&&)      = default;

    std::string_view m_announce;
    parsedAnnounce m_parsedAnnounce;
    std::optional<std::vector<std::vector<std::string_view>>> m_announceList;
    std::optional<std::vector<std::vector<parsedAnnounce>>> m_parsedAnnounceList;
    std::optional<int64_t> m_creationDate;
    std::optional<std::string_view> m_comment;
    std::optional<std::string_view> m_createdBy;
    std::optional<std::string_view> m_encoding;
    int64_t m_pieceLength;
    std::string_view m_pieces;
    int64_t m_privateField = 0;
    std::string_view m_name;
    std::optional<int64_t> m_length;
    std::optional<std::vector<std::pair<std::vector<std::string_view>, int64_t>>> m_files;
    std::string_view m_infoString;
    bool isSingleFile() const { return m_files.has_value() == false; }

private:
    Metainfo(BencodeDocument doc) : m_data(std::move(doc)) {}
    std::expected<parsedAnnounce, MetainfoError> parseAnnounce(std::string_view l_announce);
    BencodeDocument m_data;
};
