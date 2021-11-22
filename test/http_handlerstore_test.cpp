#include <gtest/gtest.h>

#include "handler_store.hpp"
#include "restio_common.hpp"

using namespace restio;

static int calledId = -1;

RequestHandler makeCallback(int id)
{
    return [id](std::string_view, Request &, Response &) -> boost::asio::awaitable<void> {
        calledId = id;
        return {};
    };
}

http::request<http::string_body> makeRequest(std::string &&path, http::verb verb)
{
    http::request<http::string_body> req;
    req.target(std::move(path));
    req.method(verb);
    return req;
}

TEST(HandlerStoreTest, RegisterAndLookupOne)
{
    HttpHandlerStore store;

    store.add("test", makeCallback(1));
    auto result = store.lookup(makeRequest("/unknown", http::verb::get));
    EXPECT_FALSE(bool(result));
    auto request = makeRequest("/test/ggg?hello", http::verb::get);
    result       = store.lookup(request);
    EXPECT_TRUE(bool(result));
    auto const &[path, handler] = *result;
    EXPECT_EQ(path, std::string_view("/ggg?hello"));
    Response response;
    std::ignore = handler(path, request, response);
    EXPECT_EQ(calledId, 1);
}

TEST(HandlerStoreTest, ConflictingRoutes)
{
    HttpHandlerStore store;

    store.add("test", makeCallback(1));
    store.add("/test", makeCallback(2));
    store.add("/test/", makeCallback(3));
    store.add(http::verb::get, "/test/", makeCallback(40));
    store.add(http::verb::get, "/test/", makeCallback(4));
    store.add("/test/a", makeCallback(5));
    {
        auto get_request = makeRequest("/test/ggg?hello", http::verb::get);
        auto result      = store.lookup(get_request);
        EXPECT_TRUE(bool(result));
        auto const &[path, handler] = *result;
        EXPECT_EQ(path, std::string_view("/ggg?hello"));
        Response response;
        std::ignore = handler(path, get_request, response);
        EXPECT_EQ(calledId, 4);
    }
    {
        auto post_request = makeRequest("/test/ggg?hello", http::verb::post);
        auto result       = store.lookup(post_request);
        EXPECT_TRUE(bool(result));
        auto const &[path, handler] = *result;
        EXPECT_EQ(path, std::string_view("/ggg?hello"));
        Response response;
        std::ignore = handler(path, post_request, response);
        EXPECT_EQ(calledId, 3);
    }
    {
        auto get_request = makeRequest("/test/aggg?hello", http::verb::get);
        auto result      = store.lookup(get_request);
        EXPECT_TRUE(bool(result));
        auto const &[path, handler] = *result;
        EXPECT_EQ(path, std::string_view("/aggg?hello"));
        Response response;
        std::ignore = handler(path, get_request, response);
        EXPECT_EQ(calledId, 4);
    }
    {
        auto get_request = makeRequest("/test/a/ggg?hello", http::verb::get);
        auto result      = store.lookup(get_request);
        EXPECT_TRUE(bool(result));
        auto const &[path, handler] = *result;
        EXPECT_EQ(path, std::string_view("/ggg?hello"));
        Response response;
        std::ignore = handler(path, get_request, response);
        EXPECT_EQ(calledId, 5);
    }
}

TEST(HandlerStoreTest, RouteRemoval)
{
    HttpHandlerStore store;

    store.add("test/a/b/c", makeCallback(1));
    store.add(http::verb::get, "test/a/b/c", makeCallback(2));
    {
        auto get_request = makeRequest("/test/a/b/c/ggg?hello", http::verb::get);
        auto result      = store.lookup(get_request);
        EXPECT_TRUE(bool(result));
        auto const &[path, handler] = *result;
        EXPECT_EQ(path, std::string_view("/ggg?hello"));
        Response response;
        std::ignore = handler(path, get_request, response);
        EXPECT_EQ(calledId, 2);
    }
    store.remove(http::verb::get, "test/a/b/c");
    {
        auto get_request = makeRequest("/test/a/b/c/ggg?hello", http::verb::get);
        auto result      = store.lookup(get_request);
        EXPECT_TRUE(bool(result));
        auto const &[path, handler] = *result;
        EXPECT_EQ(path, std::string_view("/ggg?hello"));
        Response response;
        std::ignore = handler(path, get_request, response);
        EXPECT_EQ(calledId, 1);
    }
    store.remove("test/a/b/c");
    {
        auto get_request = makeRequest("/test/a/b/c/ggg?hello", http::verb::get);
        auto result      = store.lookup(get_request);
        EXPECT_FALSE(bool(result));
    }
}
