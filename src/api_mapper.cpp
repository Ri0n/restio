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

#include "api_mapper.hpp"

#include <boost/lexical_cast.hpp>

namespace restio::api {

void API::buildParser()
{
    for (auto const &m : methods) {
        std::vector<std::string> strs;
        boost::split(strs, m.uri, boost::is_any_of("/"));
        if (strs.back() == "")
            strs.pop_back();
        auto currentNode = roots.end();
        for (auto const &s : strs) { // path parts
            assert(s.size() > 0);
            ParsedNode newNode;
            if (s.starts_with('<')) {
                assert(s.size() > 2);
                std::vector<std::string> typeName;
                boost::split(typeName, s.substr(1, s.size() - 2), boost::is_any_of(":"));
                assert(typeName.size() == 2 && typeName[0].size() > 0 && typeName[1].size() > 0);
                newNode.id = typeName[1];
                if (typeName[0] == "string")
                    newNode.type = ParsedNode::Type::VarString;
                else if (typeName[0] == "int")
                    newNode.type = ParsedNode::Type::Integer;
                else
                    assert(false); // unreachable
            } else {
                newNode.type = ParsedNode::Type::ConstString;
                newNode.id   = s;
            }
            auto &nodes = currentNode == roots.end() ? roots : currentNode->children;
            auto  it    = std::find(nodes.begin(), nodes.end(), newNode);
            if (it == nodes.end()) {
                nodes.push_back(std::move(newNode));
                currentNode = nodes.end() - 1;
            } else {
                currentNode = it;
            }
        }
        currentNode->methods.push_back(std::cref(m));
    }
}

static std::optional<API::LookupResult>
lookup_helper(http::verb method, std::span<std::string> target, const std::vector<API::ParsedNode> &nodes)
{
    auto const &view         = target[0];
    bool        numeric      = std::isdigit(view[0]);
    int         numericValue = 0;
    if (numeric) {
        try {
            numericValue = boost::lexical_cast<int>(view);
        } catch (boost::bad_lexical_cast &) {
            numeric = false;
        }
    }
    using Type     = API::ParsedNode::Type;
    auto nodeMatch = [numeric, &view](auto const &node) {
        return (node.type == Type::VarString || (numeric && node.type == Type::Integer)
                || (node.type == Type::ConstString && node.id == view));
    };
    std::optional<API::LookupResult> result;
    for (auto const &node : nodes) {
        if ((target.size() > 1 && node.children.empty()) || !nodeMatch(node))
            continue;
        if (target.size() > 1)
            result = lookup_helper(method, target.subspan(1), node.children);
        else {
            for (auto const &node : nodes) {
                if (!nodeMatch(node))
                    continue;
                auto it = std::find_if(node.methods.begin(), node.methods.end(), [method](auto const &m) {
                    return m.get().method == method;
                });
                if (it != node.methods.end())
                    result = API::LookupResult { {}, *it };
            }
        }
        if (result) {
            if (node.type == Type::Integer)
                result->properties[node.id] = numericValue;
            else if (node.type == Type::VarString)
                result->properties[node.id] = std::string(view);
            return result;
        }
    }
    return std::nullopt;
}

std::optional<API::LookupResult> API::lookup(http::verb method, std::string_view target) const
{
    std::vector<std::string> strs;
    auto                     pos = target.find_first_not_of('/');
    if (pos == std::string_view::npos)
        return std::nullopt;
    target = target.substr(pos);
    boost::split(strs, target, boost::is_any_of("/"), boost::token_compress_on);
    if (strs.back().empty())
        strs.pop_back();
    if (strs.empty())
        return std::nullopt;
    return lookup_helper(method, std::span { strs }, roots);
}

} // namespace restio::api
