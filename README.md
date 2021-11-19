# C++ Boost REST service library

The idea is build a REST service/server as easy as possible.
Take a look at the demo in the tools directory. The final version should be even more neat and simple than that.

Features:

 * REST handler and web server are two distinct components and can be used separately
 * Support for multiple API versions in the same time
 * Capability to generate introspection html page for registered APIs
 * Simple API method declaration

An example of API method declaration

```
// Declare HTTP GET method for `/resource/<some string id>`
API::Method::get<ResourceGetResponse>(
                "resource/<string:id>",
                "resource info",
                "200 - ok<br>404 - resource not found",
                apiCB(onResoureGetRequest)),
```

TODO:
 
 * TLS
 * Boost.Json based Boost.Describe
 * More serializators support
 * High level API support to abstract away http stuff completely (no idea how yet)
 * Think of WebSockets or whatever (e.g. some user hook to extract payload format and use it to lookup API)
