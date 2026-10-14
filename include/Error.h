#pragma once

#include <iostream>
#include <string>
#include <utility>
#include <vector>

template <typename EnumType>
struct Error
{
    EnumType code;
    std::vector<std::string> message;

    Error(EnumType l_code, std::string l_message) : code(l_code)
    {
        message.push_back(l_message);
    }

    Error(EnumType l_code, std::vector<std::string> l_messages)
        : code(l_code), message(std::move(l_messages)) {}

    template <typename OtherEnumType>
    Error(EnumType l_code, Error<OtherEnumType>&& prev, std::string l_message)
        : code(l_code), message(std::move(prev.message))
    {
        message.push_back(std::move(l_message));
    }

    Error&& withContext(std::string context) &&
    {
        message.push_back(std::move(context));
        return std::move(*this);
    }

    friend std::ostream& operator<<(std::ostream& os, const Error& error)
    {
        os << "Error stack:\n{\n";
        for (const auto& mess : error.message)
        {
            os << "    " << mess << "\n";
        }
        os << "}\n";
        return os;
    }
};