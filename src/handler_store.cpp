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

#include "handler_store.hpp"

#include <boost/algorithm/string/find.hpp>
#include <boost/algorithm/string/find_iterator.hpp>
#include <boost/algorithm/string/trim.hpp>

using namespace boost::beast::http;

namespace restio {

constexpr http::verb max_verb = http::verb(255);

HttpHandlerStore::HttpHandlerStore(const std::string &base_path) :
    base_path_(boost::trim_right_copy_if(base_path, boost::is_any_of("/")))
{
}

void HttpHandlerStore::add(http::verb verb, std::string &&path, RequestHandler &&handler)
{
    boost::trim_if(path, boost::is_any_of("/"));
    auto  nodes = &nodes_;
    Node *node  = nullptr;
    for (auto pit = boost::make_split_iterator(path, boost::first_finder("/", boost::is_equal()));
         pit != decltype(pit)();
         ++pit) {
        std::string part               = copy_range<std::string>(*pit);
        auto const &[nodeIt, inserted] = nodes->try_emplace(std::move(part), Node {});
        if (inserted) {
            nodeIt->second.handlers.reserve(4); // for get/post/put/delete
        }
        node  = &nodeIt->second;
        nodes = &(node->children);
    }
    node->handlers[verb] = std::move(handler);
}

void HttpHandlerStore::remove(http::verb verb, const std::string &path) { remove_helper(verb, path, nodes_); }

void HttpHandlerStore::remove_helper(http::verb verb, std::string_view path_tail, NodeMap &nmap)
{
    auto pit  = std::find_if(path_tail.begin(), path_tail.end(), boost::is_any_of(std::string_view("/?#")));
    auto part = std::string(std::string_view(path_tail.begin(), pit));
    auto nit  = nmap.find(part);
    if (nit == nmap.end()) {
        return;
    }
    bool is_final_part = pit == path_tail.end() || *pit != '/';
    if (is_final_part) {
        auto hit = nit->second.handlers.find(verb);
        if (hit != nit->second.handlers.end()) {
            nit->second.handlers.erase(hit);
        }
    } else {
        remove_helper(verb, path_tail.substr(pit + 1 - path_tail.data()), nit->second.children);
    }
    if (nit->second.handlers.empty() && nit->second.children.empty()) {
        nmap.erase(nit);
    }
}

std::optional<std::pair<std::string_view, std::reference_wrapper<const RequestHandler>>>
HttpHandlerStore::lookup(const http::request<http::string_body> &req) const
{
    auto req_target = req.target();
    auto result     = lookup_node(req.method(), { req_target.data(), req_target.size() });
    if (!result) {
        return std::nullopt;
    }
    auto const &[path_tail, node, handler] = *result;
    return std::make_pair(path_tail, std::cref(*handler));
}

std::optional<std::tuple<std::string_view, const HttpHandlerStore::Node *, const RequestHandler *>>
HttpHandlerStore::lookup_node(http::verb req_verb, std::string_view req_target) const
{
    if (nodes_.empty()) {
        return std::nullopt;
    }
    if (req_target.size() < base_path_.size()) {
        return std::nullopt;
    }
    req_target = req_target.substr(base_path_.size());
    if (req_target.starts_with('/')) {
        req_target = req_target.substr(1);
    }

    const NodeMap        *nodes               = &nodes_;
    const Node           *lastMatchingNode    = nullptr;
    const RequestHandler *lastMatchingHandler = nullptr;
    auto                  path_tail           = req_target;

    auto const get_handler = [req_verb](auto const &node) {
        auto handlerIt = node.handlers.find(req_verb);
        if (handlerIt != node.handlers.end() || req_verb == verb::unknown) {
            return handlerIt;
        }
        return node.handlers.find(http::verb::unknown);
    };

    std::size_t start      = 0;
    auto        path_delim = std::string_view("/?#");
    for (auto ppos = req_target.find_first_of(path_delim);; ppos = req_target.find_first_of(path_delim, start)) {
        bool delim_not_found = ppos == std::string_view::npos;
        if (delim_not_found) {
            ppos = req_target.size();
        }
        std::string_view part = req_target.substr(start, ppos - start);
        if (part.empty()) {
            break;
        }
        auto nodeIt = nodes->find(std::string(part));
        if (nodeIt == nodes->end()) {
            break;
        }
        auto const &node      = nodeIt->second;
        auto        handlerIt = get_handler(node);
        if (handlerIt != node.handlers.end()) {
            lastMatchingNode    = &node;
            lastMatchingHandler = &handlerIt->second;
            path_tail           = req_target.substr(ppos);
        }
        nodes = &node.children;
        if (delim_not_found || req_target[ppos] != '/') {
            break;
        }
        start = ppos + 1;
    }

    if (!lastMatchingNode && !req_target.empty()) { // We didn't check "" node yet.
        auto nodeIt = nodes_.find("");
        if (nodeIt != nodes_.end()) {
            auto const &node      = nodeIt->second;
            auto        handlerIt = get_handler(node);
            if (handlerIt != node.handlers.end()) {
                lastMatchingNode    = &node;
                lastMatchingHandler = &handlerIt->second;
                path_tail           = req_target;
            }
        }
    }

    if (lastMatchingNode) {
        return std::make_tuple(path_tail, lastMatchingNode, lastMatchingHandler);
    }

    return std::nullopt;
}

} // namespace restio
