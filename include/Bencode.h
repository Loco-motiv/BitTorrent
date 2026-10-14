#pragma once

#include "Error.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

class BencodeValue
{
public:
    using Int         = int64_t;
    using String      = std::string_view;
    using List        = std::vector<BencodeValue>;
    using Dictionary  = std::vector<std::pair<String, BencodeValue>>;
    using VariantType = std::variant<Int, String, List, Dictionary>;

    BencodeValue() {}
    template <typename T>
        requires(!std::same_as<std::remove_cvref_t<T>, BencodeValue> &&
                 std::is_constructible_v<VariantType, T>)
    BencodeValue(T&& data, String l_original) : m_data(std::forward<T>(data)), m_original(l_original)
    {
    }

    bool isInt() const noexcept { return std::holds_alternative<Int>(m_data); }
    bool isString() const noexcept { return std::holds_alternative<String>(m_data); }
    bool isList() const noexcept { return std::holds_alternative<List>(m_data); }
    bool isDictionary() const noexcept { return std::holds_alternative<Dictionary>(m_data); }

    const Int* getInt() const noexcept { return std::get_if<Int>(&m_data); }
    const String* getString() const noexcept { return std::get_if<String>(&m_data); }
    const List* getList() const noexcept { return std::get_if<List>(&m_data); }
    const Dictionary* getDictionary() const noexcept { return std::get_if<Dictionary>(&m_data); }

    String getOriginal() const noexcept { return m_original; }

    const BencodeValue* findInDictionary(std::string_view key) const
    {
        if (auto dict = getDictionary())
        {
            auto it = std::lower_bound(dict->begin(), dict->end(), key,
                                       [](const auto& pair, std::string_view searchKey)
                                       { return pair.first < searchKey; });

            if (it != dict->end() && it->first == key)
            {
                return &it->second;
            }
        }
        return nullptr;
    }

    void print(std::ostream& os, int indent = 0) const;

private:
    VariantType m_data;
    String m_original;
};

enum class BencodeParseErrorCode
{
    UnexpectedEndOfInput,
    InvalidInteger,
    InvalidStringLength,
    InvalidList,
    InvalidDictionary,
    DuplicateKeyInDictionary,
    UnsortedDictionaryKey,
    MissingDelimiter,
    ExtraData,
};

using BencodeParseError = Error<BencodeParseErrorCode>;

class BencodeDocument
{
public:
    BencodeDocument() {}
    BencodeDocument(std::string l_data, BencodeValue l_root)
        : m_data(std::move(l_data)), m_root(std::move(l_root)) {}

    BencodeDocument(const BencodeDocument&)            = delete;
    BencodeDocument& operator=(const BencodeDocument&) = delete;
    BencodeDocument(BencodeDocument&&)                 = default;
    BencodeDocument& operator=(BencodeDocument&&)      = default;

    const BencodeValue& root() const { return m_root; }
    const std::string& data() const { return m_data; }

private:
    std::string m_data;
    BencodeValue m_root;
};

class BencodeParser
{
public:
    static std::expected<BencodeDocument, BencodeParseError> parse(std::string input);
    static std::string encode(const BencodeValue& value);

private:
    BencodeParser(std::string_view l_input) : m_input(l_input) {}
    std::expected<BencodeValue, BencodeParseError> parseInternal();
    std::expected<BencodeValue, BencodeParseError> parseInteger();
    std::expected<BencodeValue, BencodeParseError> parseString();
    std::expected<BencodeValue, BencodeParseError> parseList();
    std::expected<BencodeValue, BencodeParseError> parseDictionary();

    std::string_view m_input;
};

class BencodeReader
{
public:
    BencodeReader(const BencodeValue* l_dict);

    bool hasError() const { return m_error.has_value(); }
    std::string getError() const { return m_error.value_or(""); }

    std::string_view requireString(std::string_view key);
    int64_t requireInt(std::string_view key);
    const BencodeValue::List* requireList(std::string_view key);
    const BencodeValue* requireDict(std::string_view key);

    std::optional<std::string_view> optionalString(std::string_view key);
    std::optional<int64_t> optionalInt(std::string_view key);
    const BencodeValue::List* optionalList(std::string_view key);
    const BencodeValue* optionalDict(std::string_view key);

private:
    void errorMissing(std::string_view key) { m_error = "Missing required key '" +
                                                        std::string(key) + "'"; }
    void errorWrongType(std::string_view key, std::string_view type)
    {
        m_error = "Value by key '" + std::string(key) +
                  "' must be type '" + std::string(type) + "'";
    }

    const BencodeValue* m_dict = nullptr;
    std::optional<std::string> m_error;
};