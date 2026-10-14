#include "Metainfo.h"

std::expected<Metainfo, MetainfoError> Metainfo::parse(std::string input)
{
    auto parseResult = BencodeParser::parse(std::move(input));
    if (!parseResult)
    {
        return std::unexpected(MetainfoError(
            MetainfoErrorCode::BencodeParseError,
            std::move(parseResult.error()),
            "Failed to parse .torrent file"));
    }

    Metainfo meta(std::move(*parseResult));
    BencodeReader reader(&meta.m_data.root());

    meta.m_announce     = reader.requireString("announce");
    meta.m_creationDate = reader.optionalInt("creation date");
    meta.m_comment      = reader.optionalString("comment");
    meta.m_createdBy    = reader.optionalString("created by");
    meta.m_encoding     = reader.optionalString("encoding");

    const BencodeValue* infoDict = reader.requireDict("info");

    if (reader.hasError())
    {
        return std::unexpected(MetainfoError(
            MetainfoErrorCode::InvalidDataType,
            { reader.getError(), "Error parsing root of torrent file" }));
    }

    auto tempParsedAnnounce = meta.parseAnnounce(meta.m_announce);
    if (!tempParsedAnnounce)
    {
        return std::unexpected(std::move(tempParsedAnnounce.error())
                                   .withContext("Error while parsing announce"));
    }
    meta.m_parsedAnnounce = tempParsedAnnounce.value();

    meta.m_infoString = infoDict->getOriginal();

    BencodeReader infoReader(infoDict);

    meta.m_name        = infoReader.requireString("name");
    meta.m_pieces      = infoReader.requireString("pieces");
    meta.m_pieceLength = infoReader.requireInt("piece length");
    meta.m_length      = infoReader.optionalInt("length");

    if (auto priv = infoReader.optionalInt("private"))
    {
        meta.m_privateField = *priv;
    }
    if (infoReader.hasError())
    {
        return std::unexpected(MetainfoError(
            MetainfoErrorCode::InvalidDataType,
            { infoReader.getError(), "Error parsing 'info' dictionary" }));
    }

    if (const BencodeValue::List* fList = infoReader.optionalList("files"))
    {
        std::vector<std::pair<std::vector<std::string_view>, int64_t>> parsedFiles;
        for (const auto& fileItem : *fList)
        {
            BencodeReader fileReader(&fileItem);
            int64_t fileLen                    = fileReader.requireInt("length");
            const BencodeValue::List* pathList = fileReader.requireList("path");

            if (fileReader.hasError())
            {
                return std::unexpected(MetainfoError(
                    MetainfoErrorCode::InvalidDataType,
                    { fileReader.getError(), "Error parsing 'files' item" }));
            }

            std::vector<std::string_view> parsedPath;
            for (const auto& pathSegment : *pathList)
            {
                if (!pathSegment.isString())
                {
                    return std::unexpected(MetainfoError(
                        MetainfoErrorCode::InvalidDataType,
                        "Path segment is not a string"));
                }
                parsedPath.push_back(*pathSegment.getString());
            }
            parsedFiles.emplace_back(std::move(parsedPath), fileLen);
        }
        meta.m_files = std::move(parsedFiles);
    }
    else if (!meta.m_length.has_value())
    {
        return std::unexpected(MetainfoError(
            MetainfoErrorCode::MissingRequiredKey,
            "Missing required key 'length' or 'files'"));
    }

    if (const BencodeValue::List* aList = reader.optionalList("announce-list"))
    {
        std::vector<std::vector<std::string_view>> tempAnnounceList;
        std::vector<std::vector<parsedAnnounce>> tempParsedAnnounceList;
        for (const auto& listElement : *aList)
        {
            if (!listElement.isList())
            {
                return std::unexpected(MetainfoError(
                    MetainfoErrorCode::InvalidDataType,
                    "Element of 'announce-list' must be a list"));
            }

            std::vector<std::string_view> parsedList;
            std::vector<parsedAnnounce> parsedAnnList;
            for (const auto& url : *listElement.getList())
            {
                if (!url.isString())
                {
                    return std::unexpected(MetainfoError(
                        MetainfoErrorCode::InvalidDataType,
                        "URL in 'announce-list' must be a string"));
                }
                parsedList.push_back(*url.getString());

                auto tempAnnounce = meta.parseAnnounce(*url.getString());
                if (!tempAnnounce)
                {
                    return std::unexpected(std::move(tempAnnounce.error())
                                               .withContext("Error while parsing announce-list"));
                }
                parsedAnnList.push_back(tempAnnounce.value());
            }
            tempAnnounceList.push_back(std::move(parsedList));
            tempParsedAnnounceList.push_back(std::move(parsedAnnList));
        }
        meta.m_announceList       = std::move(tempAnnounceList);
        meta.m_parsedAnnounceList = std::move(tempParsedAnnounceList);
    }

    if (reader.hasError())
    {
        return std::unexpected(MetainfoError(
            MetainfoErrorCode::InvalidDataType,
            { reader.getError(), "Error parsing root of torrent file" }));
    }

    return meta;
}

std::expected<parsedAnnounce, MetainfoError> Metainfo::parseAnnounce(std::string_view l_announce)
{
    parsedAnnounce result;
    if (l_announce.starts_with("http://"))
    {
        result.protocol = l_announce.substr(0, 7);
        l_announce.remove_prefix(7);
    }
    else if (l_announce.starts_with("https://"))
    {
        result.protocol = l_announce.substr(0, 8);
        l_announce.remove_prefix(8);
    }
    else
    {
        return std::unexpected(MetainfoError(
            MetainfoErrorCode::InvalidAnnounce,
            "Unexpected tracker protocol"));
    }

    size_t index = l_announce.find("/");
    if (index == l_announce.npos)
    {
        return std::unexpected(MetainfoError(
            MetainfoErrorCode::InvalidAnnounce,
            "Tracker URL has no announce path"));
    }
    if (index == 0)
    {
        return std::unexpected(MetainfoError(
            MetainfoErrorCode::InvalidAnnounce,
            "Tracker URL has no host"));
    }

    result.host     = l_announce.substr(0, index);
    result.announce = l_announce.substr(index + 1);

    return result;
}