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

#include "detail/handler_store.hpp"
#include "http_server.hpp"
#include "detail/log.hpp"

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
    struct Session : public std::enable_shared_from_this<Session> {
        using EofCallback  = std::function<void(std::shared_ptr<Session>)>;
        using ReadCallback = std::function<void(std::shared_ptr<Session>, Request &, Response &)>;

        beast::flat_buffer        buffer;
        Request                   request;
        Response                  response;
        boost::beast::tcp_stream  stream; //(std::move(socket));
        boost::system::error_code ec;

        ReadCallback readCallback;
        EofCallback  eofCallback;

        Session(tcp::socket &&socket, ReadCallback &&readCallback, EofCallback &&eofCallback) :
            stream(std::move(socket)), readCallback(std::move(readCallback)), eofCallback(std::move(eofCallback))
        {
        }

        ~Session() { close(); }

        void process()
        {
            using namespace std::placeholders;
            request = {};
            http::async_read(stream,
                             buffer,
                             request,
                             [weak_this = std::weak_ptr(shared_from_this())](
                                 const boost::system::error_code &ec, [[maybe_unused]] std::size_t bytes_transferred) {
                                 auto self = weak_this.lock();
                                 if (self) {
                                     self->onRead(ec);
                                 }
                             });
        }

        void onRead(const boost::system::error_code &ec)
        {
            RESTIO_TRACE("onRead: " << ec);
            if (ec) {
                if (ec != http::error::end_of_stream)
                    RESTIO_ERROR("Session failed: " << ec);
                onFinish(ec);
                return;
            }
            response = {};
            response.version(request.version());
            response.set(http::field::server, "Restio/" RESTIO_VERSION);
            response.keep_alive(request.keep_alive());
            response.result(http::status::ok);
            readCallback(shared_from_this(), request, response);
        }

        void onFinish(const boost::system::error_code &ec)
        {
            RESTIO_TRACE("onFinish: " << ec);
            this->ec = ec;
            eofCallback(shared_from_this());
        }

        void close()
        {
            if (stream.socket().is_open()) {
                // Send a TCP shutdown
                stream.socket().shutdown(tcp::socket::shutdown_send, ec);
                stream.close();
            }
        }

        void write(std::string_view reason = {})
        {
            if (!reason.empty()) {
                response.reason(reason);
            }
            response.prepare_payload();
            using namespace std::placeholders;
            http::async_write(stream,
                              response,
                              [weak_this = std::weak_ptr(shared_from_this())](
                                  const boost::system::error_code &ec, [[maybe_unused]] std::size_t bytes_transferred) {
                                  auto self = weak_this.lock();
                                  if (self) {
                                      self->onWritten(ec);
                                  }
                              });
        }

        void onWritten(const boost::system::error_code &ec)
        {
            RESTIO_TRACE("onWritten: " << ec);
            if (ec) {
                RESTIO_ERROR("Session failed: " << ec);
            }
            if (response.need_eof()) {
                RESTIO_ERROR("Session needs eof. closing.");
            }
            if (ec || response.need_eof()) {
                onFinish(ec);
            } else {
                process();
            }
        }
    };

    boost::asio::io_context                          &io_context;
    HttpHandlerStore                                  handlers;
    std::map<tcp::endpoint, std::shared_ptr<Session>> sessions;

    void processRequest(std::shared_ptr<Session> session, Request &request, Response &response)
    {
        auto lookup_result = handlers.lookup(request);
        if (lookup_result) {
            auto const &[path, handler] = *lookup_result;
            co_spawn(
                io_context.get_executor(),
                [path = path, session, handler = handler, &request, &response]() -> awaitable<void> {
                    try {
                        co_await handler(path, request, response);
                        session->write();
                    } catch (std::exception &e) {
                        RESTIO_ERROR("Session failed: " << e.what());
                        response.result(http::status::internal_server_error);
                        session->write("Exception happened");
                    }
                },
                detached);
            return;
        }
        RESTIO_ERROR("Failed to lookup any HTTP handler for " << request.method_string() << " " << request.target());
        response.result(http::status::not_found);
        session->write();
    }

    void makeSession(tcp::socket socket)
    {
        using namespace std::placeholders;
        auto endpoint = socket.remote_endpoint();
        auto session  = std::make_shared<Session>(std::move(socket),
                                                 std::bind(&HttpServerPrivate::processRequest, this, _1, _2, _3),
                                                 [endpoint, this](std::shared_ptr<Session>) {
                                                     auto it = sessions.find(endpoint);
                                                     if (it != sessions.end()) {
                                                         sessions.erase(it);
                                                     }
                                                 });

        auto const &[it, inserted] = sessions.insert(std::make_pair(endpoint, session));
        if (!inserted) {
            it->second->close();
            sessions[endpoint] = session;
        }
        session->process();
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
                makeSession(std::move(socket));
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
        io_context(io_context),
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
