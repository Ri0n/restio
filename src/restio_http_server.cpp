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
    HttpHandlerStore  handlers;
    tcp::acceptor     acceptor;
    HttpServer::Stats stats;

    awaitable<void> processRequest(Request &request, Response &response)
    {
        stats.requests++;
        auto lookup_result = handlers.lookup(request);
        if (!lookup_result) {
            RESTIO_ERROR("unroutable request: " << request.method_string() << " " << request.target()
                                                << " payload:" << request.body());
            response.result(http::status::not_found);
            stats.unknown_requests++;
            co_return;
        }
        RESTIO_TRACE("request: " << request.method_string() << " " << request.target()
                                 << " payload:" << request.body());

        auto const &[path, handler] = *lookup_result;
        try {
            co_await handler(path, request, response);
        } catch (std::exception &e) {
            stats.exceptions++;
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
                RESTIO_TRACE("Session needs eof. closing.");
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

    awaitable<void> listen()
    {
        auto executor = co_await this_coro::executor;
        for (;;) {
            try {
                tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
                co_spawn(executor, makeSession(std::move(socket)), detached);
            } catch (boost::system::system_error &e) {
                if (e.code() == boost::asio::error::operation_aborted) {
                    RESTIO_INFO("Listening restio tcp socket closed");
                    break;
                }
                RESTIO_ERROR("Failed to accept restio socket: " << e.what());
                if (!acceptor.is_open()) {
                    break;
                }
            } catch (...) {
                RESTIO_ERROR("Unexpected restio error");
                break;
            }
        }
    }

    tcp::acceptor setup_acceptor(boost::asio::io_context &io_context,
                                 const std::string       &bind_address,
                                 uint16_t                 bind_port,
                                 const std::string       &service_name)
    {
        boost::beast::error_code ec;
        tcp::endpoint            endpoint;
        try {
            endpoint = { boost::asio::ip::make_address(bind_address), bind_port };
        } catch (std::exception &) {
            // try to resolve bind_address. maybe it's not an IP
            boost::asio::ip::tcp::resolver resolver(io_context.get_executor());
            auto                           resolved = resolver.resolve(bind_address, "");
            auto                           it       = std::find_if(
                resolved.begin(), resolved.end(), [](auto &e) { return e.endpoint().address().is_v4(); });
            if (it == resolved.end()) {
                throw std::runtime_error(std::string("Failed to resolve IPv4 address for ") + bind_address);
            }
            endpoint = { it->endpoint().address(), bind_port };
        }

        tcp::acceptor acceptor(io_context.get_executor());
        RESTIO_INFO("Bind " << (service_name.empty() ? std::string("restio http service") : service_name) << " to "
                            << endpoint.address().to_string() << ":" << bind_port);
        acceptor.open(endpoint.protocol(), ec);
        ensure_success(ec, "open http endpoint");

        acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
        ensure_success(ec, "set_option");

        acceptor.bind(endpoint, ec);
        ensure_success(ec, "bind");

        acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
        ensure_success(ec, "listen");

        return acceptor;
    }

public:
    HttpServerPrivate(boost::asio::io_context &io_context,
                      const std::string       &bind_address,
                      uint16_t                 bind_port,
                      const std::string       &base_path,
                      const std::string       &service_name) :
        handlers(base_path),
        acceptor(setup_acceptor(io_context, bind_address, bind_port, service_name))
    {
        co_spawn(io_context, listen(), detached);
    }

    void addRoute(http::verb method, std::string &&path, RequestHandler &&handler)
    {
        handlers.add(method, std::move(path), std::move(handler));
    }

    void stop() { acceptor.close(); }

    HttpServer::Stats takeStats()
    {
        auto ret = stats;
        stats    = {};
        return ret;
    }
};

HttpServer::HttpServer(boost::asio::io_context &io_context,
                       const std::string       &bind_address,
                       uint16_t                 bind_port,
                       const std::string       &base_path,
                       const std::string       &service_name) :
    d(std::make_unique<HttpServerPrivate>(io_context, bind_address, bind_port, base_path, service_name))
{
}

void HttpServer::stop() { d->stop(); }

void HttpServer::route(http::verb method, std::string &&path, RequestHandler &&handler)
{
    d->addRoute(method, std::move(path), std::move(handler));
}

HttpServer::Stats HttpServer::takeStats() { return d->takeStats(); }

HttpServer::~HttpServer() = default;

} // namespace restio
