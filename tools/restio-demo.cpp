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

#include "rest_service.hpp"

#include "api_mapper.hpp"
#include "http_server.hpp"
#include "properties.hpp"
#include "rest_handler.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/lexical_cast.hpp>

#include <iostream>
#include <memory>
#include <set>

//#include "api/version_response.h"

using boost::asio::awaitable;
using namespace nlohmann;
using namespace restio;
using namespace restio::api;
namespace beast = boost::beast; // from <boost/beast.hpp>
namespace http  = beast::http;  // from <boost/beast/http.hpp>

struct ResourceAddRequest {
    std::string                      name;
    inline static ResourceAddRequest docSample() { return { "world" }; }
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ResourceAddRequest, name)

struct ResourceAddResponse {
    std::string                       echo;
    inline static ResourceAddResponse docSample() { return { "hello world" }; }
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ResourceAddResponse, echo)

struct ResourceGetResponse {
    std::string                       echo;
    inline static ResourceAddResponse docSample() { return { "hello world" }; }
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ResourceGetResponse, echo)

class RESTService {
    awaitable<Response> onResoureAddRequest(const std::string &body, const Properties &)
    {
        auto request              = json::parse(body).get<ResourceAddRequest>();
        auto const &[_, inserted] = resources.insert(request.name);
        if (!inserted) {
            Response response;
            response.result(http::status::conflict);
            co_return response;
        }
        co_return RestHandler::makeOkResponse(ResourceAddResponse { "hello " + request.name });
    }

    awaitable<Response> onResourceDeleteRequest([[maybe_unused]] const std::string &body, const Properties &p)
    {
        auto it = resources.find(*p.value<std::string>("id"));
        if (it == resources.end()) {
            Response response;
            response.result(http::status::not_found);
            co_return response;
        }
        resources.erase(it);
        co_return RestHandler::makeOkResponse();
    }

    awaitable<Response> onResoureGetRequest([[maybe_unused]] const std::string &body, const Properties &p)
    {
        auto it = resources.find(*p.value<std::string>("id"));
        if (it == resources.end()) {
            Response response;
            response.result(http::status::not_found);
            co_return response;
        }
        co_return RestHandler::makeOkResponse(ResourceGetResponse { "It's " + *it });
    }

private:
    HttpServer            server;
    RestHandler           restHandler;
    std::set<std::string> resources;

public:
    RESTService(boost::asio::io_context &ioc) : server(ioc, "0.0.0.0", 8080), restHandler(server)
    {
        using namespace std::placeholders;
#define apiCB(f) std::bind(&RESTService::f, this, _1, _2)
        using M = API::Method;

        API api;
        api.version = 1;
        // clang-format off
        api.methods = {
            M::post<ResourceAddRequest, ResourceAddResponse>(
                "resource",
                "Add new resource",
                "200 - added",
                apiCB(onResoureAddRequest)),
            M::delete_(
                "resource/<string:id>",
                "Delete resource",
                "204 - deleted<br>404 - resource not found",
                apiCB(onResourceDeleteRequest)),
            M::get<ResourceGetResponse>(
                "resource/<string:id>",
                "resource info",
                "200 - ok<br>404 - resource not found",
                apiCB(onResoureGetRequest)),
        };
        restHandler.registerAPI(std::move(api));
        // clang-format on
#undef apiCB
    }
};

int main()
{
    boost::asio::io_context ioc;
    RESTService             service(ioc);
    ioc.run();
    std::cout << "finsihed\n";
}
