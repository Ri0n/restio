# C++ Boost REST service library

The idea is build a REST service/server as easy as possible. Currently it's a very premature project. 
But the final version which even be simpler than in the current demo file in the tools directory.

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
