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

#include "restio_common.hpp"
#include "restio_properties.hpp"

#include <type_traits>

namespace restio::api {

using namespace ::nlohmann;
namespace http = ::boost::beast::http; // from <boost/beast/http.hpp>
using ::boost::asio::awaitable;

struct API {
    struct Method {
        using Handler
            = std::function<awaitable<void>(Request &request, Response &response, const Properties &properties)>;
        // using SyncHandler = std::function<void(Request &request, Response &response, const Properties &properties)>;

        http::verb  method;
        std::string uri;
        std::string comment;
        json        inputExample;
        json        outputExample;
        std::string responseStatus;
        Handler     handler;

        // Wrapping hardler to an async func if it's synchronous
        template <typename HandlerType>
        inline static Handler wrapHandler(
            HandlerType &&handler,
            typename std::enable_if<
                std::is_same<typename std::invoke_result<HandlerType, Request &, Response &, const Properties &>::type,
                             awaitable<void>>::value>::type * = 0)
        {
            return handler;
        }

        template <typename HandlerType>
        inline static Handler wrapHandler(
            HandlerType &&handler,
            typename std::enable_if<
                std::is_same<typename std::invoke_result<HandlerType, Request &, Response &, const Properties &>::type,
                             void>::value>::type * = 0)
        {
            return [handler = std::move(handler)](
                       Request &request, Response &response, const Properties &properties) -> awaitable<void> {
                handler(request, response, properties);
                co_return;
            };
        }

        template <typename RequestMessage, typename ResponseMessage, typename HandlerType>
        inline static Method sample(http::verb    method,
                                    std::string &&uri,
                                    std::string &&desc,
                                    std::string &&responseStatus,
                                    HandlerType &&handler)
        {
            return Method {
                method,
                std::move(uri),
                std::move(desc),
                RequestMessage::docSample(),
                ResponseMessage::docSample(),
                std::move(responseStatus),
                std::move(wrapHandler(std::move(handler))),
            };
        }

        struct Dummy {
            static json docSample() { return nullptr; }
        };

        template <typename ResponseMessage, typename... Args> inline static Method get(Args &&...args)
        {
            return sample<Dummy, ResponseMessage>(http::verb::get, std::forward<Args>(args)...);
        }

        template <typename RequestMessage, typename ResponseMessage, typename... Args>
        inline static Method post(Args &&...args)
        {
            return sample<RequestMessage, ResponseMessage>(http::verb::post, std::forward<Args>(args)...);
        }

        template <typename RequestMessage, typename ResponseMessage = Dummy, typename... Args>
        inline static Method put(Args &&...args)
        {
            return sample<RequestMessage, ResponseMessage>(http::verb::put, std::forward<Args>(args)...);
        }

        template <typename... Args> inline static Method delete_(Args &&...args)
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

    inline API(int version = 1) : version(version) { }

    template <typename ResponseMessage, typename... Args> inline API &get(Args &&...args)
    {
        methods.push_back(Method::get<ResponseMessage>(std::forward<Args>(args)...));
        return *this;
    }

    template <typename RequestMessage, typename ResponseMessage, typename... Args> inline API &post(Args &&...args)
    {
        methods.push_back(Method::post<RequestMessage, ResponseMessage>(std::forward<Args>(args)...));
        return *this;
    }

    template <typename RequestMessage, typename ResponseMessage = Method::Dummy, typename... Args>
    inline API &put(Args &&...args)
    {
        methods.push_back(Method::put<RequestMessage, ResponseMessage>(std::forward<Args>(args)...));
        return *this;
    }

    template <typename... Args> inline API &delete_(Args &&...args)
    {
        methods.push_back(Method::delete_(std::forward<Args>(args)...));
        return *this;
    }

    void                        buildParser();
    std::optional<LookupResult> lookup(http::verb method, std::string_view target) const;

    int                     version = 1;
    std::vector<Method>     methods;
    std::vector<ParsedNode> roots;
};

} // namespace restio::api
