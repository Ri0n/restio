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

#include <boost/algorithm/string/trim.hpp>

using namespace boost::beast::http;

namespace restio {

constexpr http::verb max_verb = http::verb(255);

HttpHandlerStore::HttpHandlerStore(const std::string &base_path) :
    base_path_(boost::trim_right_copy_if(base_path, boost::is_any_of("/")))
{
}

void HttpHandlerStore::add(std::string &&path, RequestHandler &&handler)
{
    // put theoretical max for the verb to move further any other verbs in the map for faster lookup
    add(std::move(path), max_verb, std::move(handler));
}

void HttpHandlerStore::add(std::string &&path, http::verb verb, RequestHandler &&handler)
{
    boost::trim_if(path, boost::is_any_of("/"));
    handlers_[std::make_pair(std::move(path), verb)] = std::move(handler);
}

void HttpHandlerStore::remove(const std::string &path, http::verb verb)
{
    auto it = handlers_.find(std::make_pair(path, verb));
    if (it != handlers_.end()) {
        handlers_.erase(it);
    }
}

std::optional<std::pair<std::string_view, std::reference_wrapper<const RequestHandler>>>
HttpHandlerStore::lookup(const http::request<http::string_body> &req) const
{
    if (handlers_.empty()) {
        return std::nullopt;
    }
    auto req_target = req.target();
    if (req_target.size() < base_path_.size()) {
        return std::nullopt;
    }
    req_target = req_target.substr(base_path_.size());
    if (req_target.starts_with('/')) {
        req_target = req_target.substr(1);
    }
    auto req_verb     = req.method();
    req_verb          = req_verb == http::verb::unknown ? max_verb : req_verb;
    auto check_target = [&req_target, &req_verb](auto it) -> std::optional<std::string_view> {
        auto const &[path_verb, handler] = *it;
        auto const &[path, verb]         = path_verb;
        if (verb != max_verb && req_verb != verb) {
            return std::nullopt;
        }
        if (!req_target.starts_with(path)) {
            return std::nullopt;
        }
        if (req_target.size() == path.size()) {
            return std::string_view(); // return not nullopt but the remaining mepty path
        }
        auto c = req_target[path.size()];
        if (c == '/' || c == '#' || c == '?') {
            return req_target.substr(path.size());
        }
        return std::nullopt;
    };
    auto it = handlers_.lower_bound(std::make_pair(std::string(req_target), req_verb));
    if (it == handlers_.end()) { // our last resort is *rbegin() element
        auto it     = handlers_.rbegin();
        auto result = check_target(it);
        if (result) {
            return std::make_pair(*result, std::cref(it->second));
        }
        return std::nullopt;
    }
    auto result = check_target(it);
    if (result) {
        return std::make_pair(*result, std::cref(it->second));
    }
    if (it != handlers_.begin()) {
        --it;
        auto result = check_target(it);
        if (result) {
            return std::make_pair(*result, std::cref(it->second));
        }
    }
    return std::nullopt;
}

} // namespace restio
