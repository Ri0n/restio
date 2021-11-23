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

#include "coro_compat.h"

#include "handler_store.hpp"
#include "restio_http_server.hpp"
#include "restio_log.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/system/error_code.hpp>

namespace beast = boost::beast;
namespace http  = beast::http;
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
    HttpHandlerStore handlers;

    awaitable<void> processRequest(Request &request, Response &response)
    {
        auto lookup_result = handlers.lookup(request);
        if (!lookup_result) {
            RESTIO_ERROR("Failed to lookup any HTTP handler for " << request.method_string() << " "
                                                                  << request.target());
            response.result(http::status::not_found);
            co_return;
        }
        auto const &[path, handler] = *lookup_result;
        try {
            co_await handler(path, request, response);
        } catch (std::exception &e) {
            RESTIO_ERROR("Session failed: " << e.what());
            response.result(http::status::internal_server_error);
            response.reason("Exception happened");
        }
    }

    awaitable<void> makeSession(tcp::socket socket)
    {
        beast::flat_buffer        buffer;
        Request                   request;
        Response                  response;
        boost::beast::tcp_stream  stream = boost::beast::tcp_stream(std::move(socket));
        boost::system::error_code ec;

        for (;;) {
            request = {};
            co_await http::async_read(stream, buffer, request, boost::asio::redirect_error(use_awaitable, ec));
            if (ec) {
                if (ec != http::error::end_of_stream)
                    RESTIO_ERROR("Session failed: " << ec);
                break;
            }
            RESTIO_DEBUG("Got http request: " << request.method_string() << " " << request.target());
            RESTIO_TRACE("Request body: " << request.body());
            response = {};
            response.version(request.version());
            response.set(http::field::server, "Restio/" RESTIO_VERSION);
            response.keep_alive(request.keep_alive());
            response.result(http::status::ok);

            co_await processRequest(request, response);

            response.prepare_payload();
            co_await http::async_write(stream, response, boost::asio::redirect_error(use_awaitable, ec));

            RESTIO_TRACE("onWritten: " << ec);
            if (ec) {
                RESTIO_ERROR("Session failed: " << ec);
                break;
            }
            if (response.need_eof()) {
                RESTIO_ERROR("Session needs eof. closing.");
                break;
            }
        }
        RESTIO_TRACE("Finishing http session: " << ec);
        if (stream.socket().is_open()) {
            // Send a TCP shutdown
            stream.socket().shutdown(tcp::socket::shutdown_send, ec);
            stream.close();
        }
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
                if (!acceptor.is_open()) {
                    co_return;
                }
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

    void addRoute(http::verb method, std::string &&path, RequestHandler &&handler)
    {
        handlers.add(method, std::move(path), std::move(handler));
    }
};

HttpServer::HttpServer(boost::asio::io_context &io_context,
                       const std::string       &bind_address,
                       uint16_t                 bind_port,
                       const std::string       &base_path) :
    d(std::make_unique<HttpServerPrivate>(io_context, bind_address, bind_port, base_path))
{
}

void HttpServer::route(http::verb method, std::string &&path, RequestHandler &&handler)
{
    d->addRoute(method, std::move(path), std::move(handler));
}

HttpServer::~HttpServer() = default;

} // namespace restio
