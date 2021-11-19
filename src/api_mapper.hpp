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

#include <boost/algorithm/string.hpp>
#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>
#include <span>

#include "common.hpp"
#include "properties.hpp"

using namespace nlohmann;
namespace beast = boost::beast; // from <boost/beast.hpp>
namespace http  = beast::http;  // from <boost/beast/http.hpp>
using boost::asio::awaitable;

namespace restio::api {

struct API {
    struct Method {
        using Handler
            = std::function<awaitable<void>(Request &request, Response &response, const Properties &properties)>;

        http::verb  method;
        std::string uri;
        std::string comment;
        json        inputExample;
        json        outputExample;
        std::string responseStatus;
        Handler     handler;

        template <typename Request, typename Response>
        static Method sample(http::verb    method,
                             std::string &&uri,
                             std::string &&desc,
                             std::string &&responseStatus,
                             Handler     &&handler)
        {
            return Method {
                method,
                std::move(uri),
                std::move(desc),
                Request::docSample(),
                Response::docSample(),
                std::move(responseStatus),
                std::move(handler),
            };
        }
        struct Dummy {
            static json docSample() { return nullptr; }
        };
        template <typename Response, typename... Args> static Method get(Args &&...args)
        {
            return sample<Dummy, Response>(http::verb::get, std::forward<Args>(args)...);
        }
        template <typename Request, typename Response, typename... Args> static Method post(Args &&...args)
        {
            return sample<Request, Response>(http::verb::post, std::forward<Args>(args)...);
        }
        template <typename Request, typename Response = Dummy, typename... Args> static Method put(Args &&...args)
        {
            return sample<Request, Response>(http::verb::put, std::forward<Args>(args)...);
        }
        template <typename... Args> static Method delete_(Args &&...args)
        {
            return sample<Dummy, Dummy>(http::verb::delete_, std::forward<Args>(args)...);
        }
    };

    struct ParsedNode {
        enum class Type : std::uint8_t { ConstString, VarString, Integer };
        Type                                              type;
        std::string                                       id; // var name or const string value
        std::vector<std::reference_wrapper<const Method>> methods;
        std::vector<ParsedNode>                           children;

        bool operator==(const ParsedNode &other) const { return type == other.type && id == other.id; }
    };

    struct LookupResult {
        Properties                           properties;
        std::reference_wrapper<const Method> method;
    };

    void                        buildParser();
    std::optional<LookupResult> lookup(http::verb method, std::string_view target) const;

    int                     version = 0;
    std::vector<Method>     methods;
    std::vector<ParsedNode> roots;
};

} // namespace restio::api
