#include "Bencode.h"

std::expected<BencodeDocument, BencodeParseError> BencodeParser::parse(std::string input)
{
    input.reserve(sizeof(std::string)); //* disabling SSO for proper string_view work
    BencodeParser parser{ std::string_view(input) };
    auto result = parser.parseInternal();

    if (!result)
    {
        return std::unexpected(result.error());
    }
    return BencodeDocument(std::move(input), std::move(*result));
}

std::expected<BencodeValue, BencodeParseError> BencodeParser::parseInternal()
{
    if (m_input.empty())
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::UnexpectedEndOfInput,
                                                 "Input is empty"));
    }

    char head = m_input[0];
    if (head == 'i')
    {
        return parseInteger();
    }
    else if (std::isdigit(static_cast<unsigned char>(head)))
    {
        return parseString();
    }
    else if (head == 'l')
    {
        return parseList();
    }
    else if (head == 'd')
    {
        return parseDictionary();
    }
    else
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::MissingDelimiter,
                                                 "Invalid starting character"));
    }
}

std::expected<BencodeValue, BencodeParseError> BencodeParser::parseInteger()
{
    auto itBegin = m_input.begin();
    m_input.remove_prefix(1); //* Remove 'i'

    size_t endPos = m_input.find('e');
    if (endPos == std::string_view::npos)
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::MissingDelimiter,
                                                 "Missing 'e' delimiter for integer"));
    }

    if (endPos > 1 && m_input[0] == '0')
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::InvalidInteger,
                                                 "Leading zeros are not allowed"));
    }
    if (endPos == 0)
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::InvalidInteger,
                                                 "Empty integer value"));
    }
    if (m_input[0] == '-' && (endPos == 1 || m_input[1] == '0'))
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::InvalidInteger,
                                                 "Invalid negative integer format"));
    }

    int64_t value  = 0;
    auto [ptr, ec] = std::from_chars(m_input.data(), m_input.data() + endPos, value);
    if (ec == std::errc::invalid_argument || ptr != m_input.data() + endPos)
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::InvalidInteger,
                                                 "Invalid integer format"));
    }
    else if (ec == std::errc::result_out_of_range)
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::InvalidInteger,
                                                 "Integer value out of range"));
    }

    m_input.remove_prefix(endPos + 1); //* Remove the integer and 'e'
    auto itEnd = m_input.begin();
    return BencodeValue(value, { itBegin, itEnd });
}

std::expected<BencodeValue, BencodeParseError> BencodeParser::parseString()
{
    auto itBegin    = m_input.begin();
    size_t colonPos = m_input.find(':');
    if (colonPos == std::string_view::npos)
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::MissingDelimiter,
                                                 "Missing ':' delimiter for string"));
    }
    if (colonPos > 1 && m_input[0] == '0')
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::InvalidStringLength,
                                                 "Leading zeros are not allowed in string length"));
    }
    if (m_input[0] == '-')
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::InvalidStringLength,
                                                 "Negative string length is not allowed"));
    }

    size_t length  = 0;
    auto [ptr, ec] = std::from_chars(m_input.data(), m_input.data() + colonPos, length);
    if (ec == std::errc::invalid_argument || ptr != m_input.data() + colonPos)
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::InvalidStringLength,
                                                 "Invalid string length format"));
    }
    else if (ec == std::errc::result_out_of_range)
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::InvalidStringLength,
                                                 "String length out of range"));
    }

    m_input.remove_prefix(colonPos + 1); //* Remove the length and ':'
    if (m_input.size() < length)
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::UnexpectedEndOfInput,
                                                 "String data is shorter than string length"));
    }

    std::string_view str(m_input.substr(0, length));
    m_input.remove_prefix(length); //* Remove the string data
    auto itEnd = m_input.begin();
    return BencodeValue(str, { itBegin, itEnd });
}

std::expected<BencodeValue, BencodeParseError> BencodeParser::parseList()
{
    auto itBegin = m_input.begin();
    m_input.remove_prefix(1); //* Remove 'l'
    BencodeValue::List list;
    while (!m_input.empty() && m_input[0] != 'e')
    {
        auto value = parseInternal();
        if (!value)
        {
            BencodeParseError error = value.error();
            error.message.push_back("Error parsing list element");
            return std::unexpected(error);
        }
        list.emplace_back(std::move(*value));
    }
    if (m_input.empty())
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::UnexpectedEndOfInput,
                                                 "Unexpected end of input while parsing list"));
    }
    m_input.remove_prefix(1); //* Remove 'e'
    auto itEnd = m_input.begin();
    return BencodeValue(std::move(list), { itBegin, itEnd });
}

std::expected<BencodeValue, BencodeParseError> BencodeParser::parseDictionary()
{
    auto itBegin = m_input.begin();
    m_input.remove_prefix(1); //* Remove 'd'
    BencodeValue::Dictionary dict;
    std::string_view previousKey;
    bool hasPreviousKey = false;

    while (!m_input.empty() && m_input[0] != 'e')
    {
        auto keyValue = parseString();
        if (!keyValue)
        {
            BencodeParseError error = keyValue.error();
            error.message.push_back("Error parsing dictionary key");
            return std::unexpected(error);
        }
        std::string_view key = *keyValue->getString();

        if (hasPreviousKey && key <= previousKey)
        {
            if (key == previousKey)
            {
                return std::unexpected(BencodeParseError(BencodeParseErrorCode::DuplicateKeyInDictionary,
                                                         "Duplicate key in dictionary: " + std::string(key)));
            }
            return std::unexpected(BencodeParseError(BencodeParseErrorCode::UnsortedDictionaryKey,
                                                     "Dictionary keys are not sorted"));
        }
        previousKey    = key;
        hasPreviousKey = true;

        auto value = parseInternal();
        if (!value)
        {
            BencodeParseError error = value.error();
            error.message.push_back("Error parsing dictionary value for key: " + std::string(key));
            return std::unexpected(error);
        }
        dict.emplace_back(std::make_pair(key, std::move(*value)));
    }
    if (m_input.empty())
    {
        return std::unexpected(BencodeParseError(BencodeParseErrorCode::UnexpectedEndOfInput,
                                                 "Unexpected end of input while parsing dictionary"));
    }
    m_input.remove_prefix(1); //* Remove 'e'
    auto itEnd = m_input.begin();
    return BencodeValue(std::move(dict), { itBegin, itEnd });
}

void BencodeValue::print(std::ostream& os, int indent) const
{
    std::string indentStr(indent, ' ');

    if (isInt())
    {
        os << indentStr << "Integer: " << *getInt() << "\n";
    }
    else if (isString())
    {
        os << indentStr << "String: " << *getString() << "\n";
    }
    else if (isList())
    {
        os << indentStr << "List:\n";
        for (const auto& item : *getList())
        {
            item.print(os, indent + 2);
        }
    }
    else if (isDictionary())
    {
        os << indentStr << "Dictionary:\n";
        for (const auto& [key, value] : *getDictionary())
        {
            os << indentStr << "  Key: " << key << "\n";
            value.print(os, indent + 4);
        }
    }
}

std::string BencodeParser::encode(const BencodeValue& value)
{
    std::string result;

    if (value.isInt())
    {
        result += 'i' + std::to_string(*value.getInt()) + 'e';
    }
    else if (value.isString())
    {
        const auto& str  = *value.getString();
        result          += std::to_string(str.size()) + ':' + std::string(str);
    }
    else if (value.isList())
    {
        result += 'l';
        for (const auto& item : *value.getList())
        {
            result += encode(item);
        }
        result += 'e';
    }
    else if (value.isDictionary())
    {
        result += 'd';
        for (const auto& [key, val] : *value.getDictionary())
        {
            result += std::to_string(key.size()) + ':' + std::string(key);
            result += encode(val);
        }
        result += 'e';
    }

    return result;
}

BencodeReader::BencodeReader(const BencodeValue* l_dict) : m_dict(l_dict)
{
    if (!m_dict)
    {
        m_error = "Dictionary is null";
    }
    else if (!m_dict->isDictionary())
    {
        m_error = "Value is not dictionary";
    }
}

std::string_view BencodeReader::requireString(std::string_view key)
{
    if (hasError())
    {
        return {};
    }
    const BencodeValue* val = m_dict->findInDictionary(key);
    if (!val)
    {
        return errorMissing(key), std::string_view{};
    }
    if (!val->isString())
    {
        return errorWrongType(key, "string"), std::string_view{};
    }
    return *val->getString();
}

int64_t BencodeReader::requireInt(std::string_view key)
{
    if (hasError())
    {
        return 0;
    }
    const BencodeValue* val = m_dict->findInDictionary(key);
    if (!val)
    {
        return errorMissing(key), 0;
    }
    if (!val->isInt())
    {
        return errorWrongType(key, "integer"), 0;
    }
    return *val->getInt();
}

const BencodeValue::List* BencodeReader::requireList(std::string_view key)
{
    if (hasError())
    {
        return nullptr;
    }
    const BencodeValue* val = m_dict->findInDictionary(key);
    if (!val)
    {
        return errorMissing(key), nullptr;
    }
    if (!val->isList())
    {
        return errorWrongType(key, "list"), nullptr;
    }
    return val->getList();
}
const BencodeValue* BencodeReader::requireDict(std::string_view key)
{
    if (hasError())
    {
        return nullptr;
    }
    const BencodeValue* val = m_dict->findInDictionary(key);
    if (!val)
    {
        return errorMissing(key), nullptr;
    }
    if (!val->isDictionary())
    {
        return errorWrongType(key, "dictionary"), nullptr;
    }
    return val;
}
std::optional<std::string_view> BencodeReader::optionalString(std::string_view key)
{
    if (hasError())
    {
        return std::nullopt;
    }
    const BencodeValue* val = m_dict->findInDictionary(key);
    if (!val)
    {
        return std::nullopt;
    }
    if (!val->isString())
    {
        return errorWrongType(key, "string"), std::nullopt;
    }
    return *val->getString();
}
std::optional<int64_t> BencodeReader::optionalInt(std::string_view key)
{
    if (hasError())
    {
        return std::nullopt;
    }
    const BencodeValue* val = m_dict->findInDictionary(key);
    if (!val)
    {
        return std::nullopt;
    }
    if (!val->isInt())
    {
        return errorWrongType(key, "integer"), std::nullopt;
    }
    return *val->getInt();
}
const BencodeValue::List* BencodeReader::optionalList(std::string_view key)
{
    if (hasError())
    {
        return nullptr;
    }
    const BencodeValue* val = m_dict->findInDictionary(key);
    if (!val)
    {
        return nullptr;
    }
    if (!val->isList())
    {
        return errorWrongType(key, "list"), nullptr;
    }
    return val->getList();
}
const BencodeValue* BencodeReader::optionalDict(std::string_view key)
{
    if (hasError())
    {
        return nullptr;
    }
    const BencodeValue* val = m_dict->findInDictionary(key);
    if (!val)
    {
        return nullptr;
    }
    if (!val->isDictionary())
    {
        return errorWrongType(key, "dictionary"), nullptr;
    }
    return val;
}