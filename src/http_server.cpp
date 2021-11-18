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

#ifdef BOOST_ASIO_HAS_CO_AWAIT
#include "detail/coro_compat.h"
#endif

#include "handler_store.hpp"
#include "http_server.hpp"
#include "log.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/config.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <ranges>
#include <string_view>

namespace beast = boost::beast; // from <boost/beast.hpp>
namespace http  = beast::http;  // from <boost/beast/http.hpp>
using tcp       = boost::asio::ip::tcp;
using boost::asio::awaitable;
using boost::asio::detached;
using boost::asio::use_awaitable;
namespace this_coro = boost::asio::this_coro;

#define ensure_success(ec, msg)                                                                                        \
    if (ec)                                                                                                            \
        throw std::runtime_error(std::string("Failed to ") + msg + " on " + bind_address + ":"                         \
                                 + std::to_string(bind_port));

namespace restio {

class HttpServerPrivate {
    HttpHandlerStore                 handlers;
    http::request<http::string_body> request;

    inline awaitable<void> makeSession(tcp::socket socket)
    {
        // This buffer is required to persist across reads
        beast::flat_buffer       buffer;
        boost::beast::tcp_stream stream(std::move(socket));
        auto                     ec = boost::system::error_code();

        try {
            for (;;) {
                // stream.expires_after(std::chrono::seconds(30));

                co_await http::async_read(stream, buffer, request, boost::asio::redirect_error(use_awaitable, ec));
                auto version = request.version();

                Response response;
                auto     lookup_result = handlers.lookup(request);
                if (lookup_result) {
                    auto const &[path, handler] = *lookup_result;
                    response                    = co_await handler(path, request);
                    if (!response.body().empty())
                        response.set(http::field::content_length, std::to_string(response.body().size()));
                } else {
                    response.result(http::status::not_found);
                }

                response.set(http::field::server, "Restio/" RESTIO_VERSION);
                response.version(version);
                co_await http::async_write(stream, response, use_awaitable);

                if (response.need_eof()) { // referenced in session
                    break;
                }
            }
        } catch (boost::system::system_error &e) {
            if (e.code() != http::error::end_of_stream)
                RESTIO_ERROR("Session failed: " << e.what());
        } catch (std::exception &e) {
            RESTIO_ERROR("Session failed: " << e.what());
        }

        // Send a TCP shutdown
        stream.socket().shutdown(tcp::socket::shutdown_send, ec);
    }

    awaitable<void> listen(std::string bind_address, uint16_t bind_port)
    {
        auto                     executor = co_await this_coro::executor;
        boost::beast::error_code ec;
        auto                     endpoint = tcp::endpoint { boost::asio::ip::make_address(bind_address), bind_port };

        tcp::acceptor acceptor(executor);
        acceptor.open(endpoint.protocol(), ec);
        ensure_success(ec, "open http endpoint");

        acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
        ensure_success(ec, "set_option");

        acceptor.bind(endpoint, ec);
        ensure_success(ec, "bind");

        acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
        ensure_success(ec, "listen");

        for (;;) {
            try {
                tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
                co_spawn(executor, makeSession(std::move(socket)), detached);
            } catch (std::exception &e) {
                RESTIO_ERROR("Failed to accept socket: " << e.what());
            }
        }
    }

public:
    HttpServerPrivate(boost::asio::io_context &io_context,
                      const std::string       &bind_address,
                      uint16_t                 bind_port,
                      const std::string       &base_path) :
        handlers(base_path)
    {
        co_spawn(io_context, listen(bind_address, bind_port), detached);
    }

    void addRoute(std::string &&path, RequestHandler &&handler) { handlers.add(std::move(path), std::move(handler)); }
};

HttpServer::HttpServer(boost::asio::io_context &io_context,
                       const std::string       &bind_address,
                       uint16_t                 bind_port,
                       const std::string       &base_path) :
    d(std::make_unique<HttpServerPrivate>(io_context, bind_address, bind_port, base_path))
{
}

void HttpServer::route(std::string &&path, RequestHandler &&handler)
{
    d->addRoute(std::move(path), std::move(handler));
}

HttpServer::~HttpServer() = default;

} // namespace restio
