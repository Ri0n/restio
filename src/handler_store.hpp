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

#include "restio_http_server.hpp"

#include <map>
#include <optional>
#include <tuple>

namespace restio {

class HttpHandlerStore {
public:
    HttpHandlerStore(const std::string &base_path = {});

    inline void add(std::string &&path, RequestHandler &&handler)
    {
        add(http::verb::unknown, std::move(path), std::move(handler));
    }
    void        add(http::verb verb, std::string &&path, RequestHandler &&handler);
    inline void remove(const std::string &path) { remove(http::verb::unknown, path); }
    void        remove(http::verb verb, const std::string &path);
    inline void clear() { nodes_.clear(); }

    inline const std::string &path() const { return base_path_; }

    std::optional<std::pair<std::string_view, std::reference_wrapper<const RequestHandler>>>
    lookup(const http::request<http::string_body> &req) const;

private:
    struct Node;
    using NodeMap    = std::unordered_map<std::string, std::unique_ptr<Node>>;
    using HandlerMap = std::unordered_map<http::verb, RequestHandler>;
    struct Node {
        HandlerMap handlers;
        NodeMap    children;
    };

    void remove_helper(http::verb verb, std::string_view path_part, NodeMap &nmap);

    std::optional<std::tuple<std::string_view, const Node *, const RequestHandler *>>
    lookup_node(http::verb verb, std::string_view path) const;

    std::string base_path_;
    NodeMap     nodes_;
};

} // namespace restio
