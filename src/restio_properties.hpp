/*
Copyright (c) 2021, Sergei Ilinykh <rion4ik@gmail.com>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace restio {

class Properties {
public:
    using MappedType = std::variant<int, std::string, std::vector<std::uint8_t>, bool, double>;

    template <typename T> std::optional<T> value(std::string &&key) const
    {
        auto it = params.find(key);
        if (it == params.end())
            return std::nullopt;
        return std::get<T>(it->second);
    }

    template <typename T> T value(std::string &&key, T &&defaultValue) const
    {
        auto it = params.find(key);
        if (it == params.end())
            return std::move(defaultValue);
        return std::get<T>(it->second);
    }

    MappedType &operator[](const std::string &key) { return params[key]; }
    MappedType &operator[](std::string &&key) { return params[std::move(key)]; }

    std::unordered_map<std::string, MappedType> params;
};

inline Properties operator+(const Properties &a, const Properties &b)
{
    Properties result;
    result.params.insert(a.params.begin(), a.params.end());
    result.params.insert(b.params.begin(), b.params.end());
    return result;
}

} // namespace restio
