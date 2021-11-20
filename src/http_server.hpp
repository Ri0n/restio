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

#include "restio_common.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/beast/http.hpp>

#include <functional>
#include <memory>
#include <string>

namespace boost::asio {
class io_context;
}

namespace restio {

namespace http = ::boost::beast::http;

class HttpServerPrivate;
class HttpServer {
public:
    /**
     * @param base_path - if something is passed outside of base_path, 404 will be returned
     */
    HttpServer(boost::asio::io_context &io_context,
               const std::string       &bind_address,
               std::uint16_t            bind_port,
               const std::string       &base_path = {});
    ~HttpServer();

    /**
     * @brief route a part of the path relative to base_path passed to contructor
     * @param path something a/b/c where all the remaining after "c" if started with [/,?,#,<nothing>]
     *             will be passed to the handler.
     * @param handler a function which accept remaining part of url (except base_path and route path) and the request
     *
     * If two or more similar handlers are registered
     * Example:
     *   a/b
     *   a/b/c
     * The one with the longest path will have preference for the mathing path, the others won't be called at all.
     */
    inline void route(std::string &&path, RequestHandler &&handler)
    {
        route(http::verb::unknown, std::move(path), std::move(handler));
    }

    void route(http::verb method, std::string &&path, RequestHandler &&handler);

private:
    std::unique_ptr<HttpServerPrivate> d;
};

} // namespace restio
