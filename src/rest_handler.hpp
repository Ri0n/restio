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

#include "common.hpp"

#include <nlohmann/json.hpp>

#include <memory>

namespace restio {

namespace api {
    struct API;
}

class RestHandler {
public:
    using RouteAdder = std::function<void(std::string &&, RequestHandler &&)>;

    template <class T>
    RestHandler(T &server) :
        RestHandler(
            [&](std::string &&path, RequestHandler &&handler) { server.route(std::move(path), std::move(handler)); })
    {
    }

    RestHandler(RouteAdder &&routerAdder);
    ~RestHandler();

    void registerAPI(api::API &&api);

    static void makeOkResponse(Response              &response,
                               std::string          &&body        = std::string(),
                               const std::string_view contentType = "application/json; charset=utf-8");

    template <class R> static void makeOkResponse(Response &reponse, const R &r)
    {
        nlohmann::json j = r;
        makeOkResponse(reponse, j.dump(), "application/json; charset=utf-8");
    }

private:
    struct Private;
    std::unique_ptr<Private> impl;
};

} // namespace restio
