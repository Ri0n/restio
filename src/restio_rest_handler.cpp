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

#include "restio_rest_handler.hpp"

#include "restio_api_mapper.hpp"
#include "log.hpp"
#include "restio_util.hpp"

#include <boost/lexical_cast.hpp>

namespace http = ::boost::beast::http; // from <boost/beast/http.hpp>
using ::boost::asio::awaitable;

namespace restio {

struct RestHandler::Private {
    Private(RouteAdder &&routerAdder) : routerAdder(std::move(routerAdder)) { }

    void registerAPI(api::API &&api)
    {
        auto api_path = "api/v" + std::to_string(api.version);
        using namespace std::placeholders;
        // routerAdder(std::move(api_path), std::bind(&Private::handleRequest, this, _1, _2));
        routerAdder(std::move(api_path),
                    [this, version = api.version](std::string_view path, Request &request, Response &response)
                        -> boost::asio::awaitable<void> { return onRequest(version, path, request, response); });
        (apis[api.version] = std::move(api)).buildParser();
    }

    awaitable<void> onRequest(int apiVersion, std::string_view target, Request &request, Response &response)
    {
        RESTIO_DEBUG("Got http request: " << request.method_string() << " " << request.target() << " "
                                          << request.body());
        try {
            auto it = apis.find(apiVersion);
            BOOST_ASSERT(it != apis.end());
            if (target.empty())
                handleAPIIntrospection(it->second, response);
            else {
                auto lookupResult = it->second.lookup(request.method(), target);
                if (!lookupResult) {
                    RESTIO_ERROR("Failed to lookup API handler for " << request.method_string() << " " << target);
                    response.result(http::status::not_found);
                } else {
                    co_await lookupResult->method.get().handler(request, response, lookupResult->properties);
                }
            }
        } catch (std::exception &e) {
            RESTIO_ERROR("Unexpected error on HTTP request handling: " << e.what());
            response.result(http::status::internal_server_error);
        }
    }

    void handleAPIIntrospection(api::API &api, Response &response)
    {
        // we are smarter than OpenAPI 3.0
        auto        verStr        = std::to_string(api.version);
        std::string introTemplate = R"(<!DOCTYPE html>
<html>
<head>
  <title>Restio API version )";
        introTemplate += verStr;
        introTemplate += R"(</title>
  <style type="text/css">
body { width:100%; padding:0; margin:0; }
.methods { border: 1px solid black; border-collapse: collapse; width:100%; }
.methods th, .methods td { padding: 0.5em; }
td.code { font-family: monospace; max-width: 60em; padding: 0; }
.code > pre { overflow-x:auto; text-overflow: ellipsis; padding: 0.5em; margin:0; }
  </style>
</head>
<body>
<h2>Restio API version )";
        introTemplate += verStr;
        introTemplate += R"(</h2>
<table border="1" class="methods">
  <tr><th>URI</th>
      <th>Method</th>
      <th>Description</th>
      <th width="35%">Input</th>
      <th width="35%">Output</th>
      <th>Status codes</th>
  </tr>
{api}
</table>
</body>
</html>)";
        std::string introStr;
        auto        uriPrefix = std::string("/api/v") + std::to_string(api.version) + "/";
        for (auto const &method : api.methods) {
            introStr += "<tr><td>";
            introStr += (uriPrefix + htmlEscape(method.uri) + "</td><td>");
            introStr += (std::string(http::to_string(method.method)) + "</td><td>");
            introStr += (method.comment + "</td><td class=\"code\"><pre>");
            introStr += (method.inputExample.dump(2) + "</pre></td><td class=\"code\"><pre>");
            introStr += (method.outputExample.dump(2) + "</pre></td><td>");
            introStr += (method.responseStatus + "</td></tr>");
        }
        boost::replace_first(introTemplate, "{api}", introStr);
        makeOkResponse(response, std::move(introTemplate), "text/html; charset=utf-8");
    }

    RouteAdder                        routerAdder;
    std::unordered_map<int, api::API> apis;
};

RestHandler::RestHandler(RouteAdder &&routerAdder) : impl(std::make_unique<Private>(std::move(routerAdder))) { }

RestHandler::~RestHandler() { }

void RestHandler::registerAPI(api::API &&api) { impl->registerAPI(std::move(api)); }

void RestHandler::makeOkResponse(Response &response, std::string &&body, const std::string_view contentType)
{
    if (body.size()) {
        response.result(http::status::ok);
        response.set(http::field::content_type, { contentType.data(), contentType.size() });
        response.body() = std::move(body);
    } else {
        response.result(http::status::no_content);
    }
}

} // namespace restio
