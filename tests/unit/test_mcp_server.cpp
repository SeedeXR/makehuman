// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The MCP protocol layer, tested with no mesh, no GPU and no subprocess. That
// is the reason it knows nothing about characters: a protocol bug should fail
// here in milliseconds rather than presenting as a rendering bug later.
#include "makehuman/mcp/Server.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>

using namespace mh::mcp;

namespace {

Server bare() {
    return Server("makehuman-test", "0.0.0");
}

Server withEcho() {
    Server s = bare();
    s.add(Tool{.name        = "echo",
               .description = "Returns its argument.",
               .inputSchema = Json{{"type", "object"}},
               .call        = [](const Json& args) { return args; }});
    return s;
}

Json request(std::string method, Json params = Json::object(), Json id = 1) {
    Json r;
    r["jsonrpc"] = "2.0";
    r["id"]      = std::move(id);
    r["method"]  = std::move(method);
    r["params"]  = std::move(params);
    return r;
}

Json initialise(Server& s, const std::string& version = "2025-06-18") {
    return s.handle(request("initialize", Json{{"protocolVersion", version}}));
}

}  // namespace

TEST_CASE("initialize echoes a version we implement", "[mcp]") {
    // Echoing the client's version is what lets this server keep working when
    // the client updates. Refusing an unknown one would break on the next
    // release for no reason.
    Server s = bare();
    for (const std::string& v : supportedProtocolVersions()) {
        Server fresh = bare();
        const Json r = initialise(fresh, v);
        INFO("asked for " << v);
        CHECK(r["result"]["protocolVersion"] == v);
    }
    const Json r = initialise(s, "1999-01-01");
    // An unknown version is answered with OURS rather than refused: the client
    // then knows what we speak and can decide, which is the specification's
    // own guidance.
    CHECK(r["result"]["protocolVersion"] == supportedProtocolVersions().front());
    CHECK(r["result"]["serverInfo"]["name"] == "makehuman-test");
}

TEST_CASE("ping answers before initialize", "[mcp]") {
    // A liveness probe that only works on a negotiated session cannot
    // distinguish "down" from "not yet initialised", which is the one thing a
    // probe exists to tell you.
    Server s     = bare();
    const Json r = s.handle(request("ping"));
    REQUIRE(r.contains("result"));
    CHECK_FALSE(r.contains("error"));
}

TEST_CASE("everything else is refused before initialize", "[mcp]") {
    Server s     = withEcho();
    const Json r = s.handle(request("tools/list"));
    REQUIRE(r.contains("error"));
    CHECK(r["error"]["code"] == static_cast<int>(ErrorCode::InvalidRequest));
}

TEST_CASE("tools/list reports what was registered", "[mcp]") {
    Server s = withEcho();
    initialise(s);
    const Json r = s.handle(request("tools/list"));
    REQUIRE(r["result"]["tools"].size() == 1);
    CHECK(r["result"]["tools"][0]["name"] == "echo");
    // The schema travels with it: a client validates before calling, which
    // turns a class of failure into a question the model answers itself.
    CHECK(r["result"]["tools"][0].contains("inputSchema"));
}

TEST_CASE("a tool call returns its content", "[mcp]") {
    Server s = withEcho();
    initialise(s);
    const Json r =
        s.handle(request("tools/call", Json{{"name", "echo"}, {"arguments", Json{{"hello", 1}}}}));
    CHECK(r["result"]["isError"] == false);
    // `content` is an ARRAY of blocks, which the specification requires and a
    // strict client enforces. The data the tool returned is repeated verbatim
    // under `structuredContent` so a client need not re-parse the text.
    REQUIRE(r["result"]["content"].is_array());
    CHECK(r["result"]["content"][0]["type"] == "text");
    CHECK(r["result"]["structuredContent"]["hello"] == 1);
    CHECK(s.stats().toolCalls == 1);
}

TEST_CASE("a tool returning an array supplies content blocks directly", "[mcp]") {
    // THE REASON THE ARRAY CASE EXISTS. An image is a content block, not a
    // description of one -- a tool that could only return data would have to
    // hand back a file path and hope the client can read files, which the
    // model driving an "adjust, look, adjust" loop cannot rely on.
    Server s = bare();
    s.add(Tool{.name        = "picture",
               .description = "Returns an image block.",
               .inputSchema = Json{{"type", "object"}},
               .call        = [](const Json&) {
                   return Json::array(
                       {Json{{"type", "image"}, {"data", "iVBOR"}, {"mimeType", "image/png"}}});
               }});
    initialise(s);
    const Json r = s.handle(request("tools/call", Json{{"name", "picture"}}));
    CHECK(r["result"]["content"][0]["type"] == "image");
    CHECK(r["result"]["content"][0]["mimeType"] == "image/png");
    // Passed through untouched: no text wrapper, no structured copy of a
    // base64 blob nobody would read.
    CHECK_FALSE(r["result"].contains("structuredContent"));
}

TEST_CASE("a ToolError is a result, not a transport fault", "[mcp]") {
    // THE DISTINCTION THAT MATTERS. A failure the caller can act on comes back
    // as a successful exchange carrying isError, so the model reads the text
    // and tries something else. Reporting it as a JSON-RPC error would tell the
    // client the SERVER is broken, and a model that believes that stops asking.
    Server s = bare();
    s.add(Tool{.name        = "picky",
               .description = "Always refuses.",
               .inputSchema = Json{{"type", "object"}},
               .call        = [](const Json&) -> Json { throw ToolError("height must be 0..1"); }});
    initialise(s);
    const Json r = s.handle(request("tools/call", Json{{"name", "picky"}}));
    REQUIRE(r.contains("result"));
    CHECK(r["result"]["isError"] == true);
    CHECK(r["result"]["content"][0]["text"] == "height must be 0..1");
    CHECK(s.stats().toolErrors == 1);
}

TEST_CASE("an unexpected exception is an InternalError", "[mcp]") {
    // The other half of the same distinction: this one says "the server has a
    // bug", which is true, and must not be dressed up as a usable answer.
    Server s = bare();
    s.add(Tool{.name        = "broken",
               .description = "Throws.",
               .inputSchema = Json{{"type", "object"}},
               .call        = [](const Json&) -> Json { throw std::runtime_error("bad index"); }});
    initialise(s);
    const Json r = s.handle(request("tools/call", Json{{"name", "broken"}}));
    REQUIRE(r.contains("error"));
    CHECK(r["error"]["code"] == static_cast<int>(ErrorCode::InternalError));
}

TEST_CASE("an unknown tool and an unknown method are different errors", "[mcp]") {
    Server s = withEcho();
    initialise(s);
    const Json badTool = s.handle(request("tools/call", Json{{"name", "nope"}}));
    CHECK(badTool["error"]["code"] == static_cast<int>(ErrorCode::InvalidParams));
    const Json badMethod = s.handle(request("wat"));
    CHECK(badMethod["error"]["code"] == static_cast<int>(ErrorCode::MethodNotFound));
}

TEST_CASE("a notification is never answered", "[mcp]") {
    // A request without an id. Answering one is a protocol violation and some
    // clients treat the stray response as a reply to their NEXT request, which
    // desynchronises the session in a way that is painful to debug.
    Server s = withEcho();
    initialise(s);
    Json n;
    n["jsonrpc"] = "2.0";
    n["method"]  = "notifications/initialized";
    CHECK(s.handle(n).is_null());

    Json call;
    call["jsonrpc"] = "2.0";
    call["method"]  = "tools/call";
    call["params"]  = Json{{"name", "echo"}, {"arguments", Json::object()}};
    CHECK(s.handle(call).is_null());
}

TEST_CASE("a duplicate tool name is refused at registration", "[mcp]") {
    // Otherwise tools/call depends on registration order, which works until
    // someone reorders a file.
    Server s = withEcho();
    CHECK_THROWS_AS(s.add(Tool{.name        = "echo",
                               .description = "Another one.",
                               .inputSchema = Json{{"type", "object"}},
                               .call        = [](const Json& a) { return a; }}),
                    std::logic_error);
}

TEST_CASE("a malformed line does not end the session", "[mcp]") {
    // ONE bad line from a client must not look like a crash. The loop answers
    // with a ParseError and carries on, so the next request still works.
    Server s = withEcho();
    std::istringstream in(
        "{ this is not json\n"
        R"({"jsonrpc":"2.0","id":1,"method":"ping"})"
        "\n");
    std::ostringstream out;
    CHECK(s.run(in, out) == 0);

    std::istringstream replies(out.str());
    std::string first;
    std::string second;
    REQUIRE(std::getline(replies, first));
    REQUIRE(std::getline(replies, second));
    const Json a = Json::parse(first);
    const Json b = Json::parse(second);
    CHECK(a["error"]["code"] == static_cast<int>(ErrorCode::ParseError));
    CHECK(a["id"].is_null());  // the spec's answer when the id is unknown
    CHECK(b.contains("result"));
}

TEST_CASE("a non-2.0 message is an InvalidRequest", "[mcp]") {
    Server s = withEcho();
    Json r;
    r["id"]        = 1;
    r["method"]    = "ping";
    const Json out = s.handle(r);
    CHECK(out["error"]["code"] == static_cast<int>(ErrorCode::InvalidRequest));
}

TEST_CASE("the loop writes one JSON object per line", "[mcp]") {
    // The stdio transport is line-delimited, so a pretty-printed response would
    // be read as several malformed messages.
    Server s = withEcho();
    std::istringstream in(R"({"jsonrpc":"2.0","id":7,"method":"ping"})"
                          "\n");
    std::ostringstream out;
    s.run(in, out);
    const std::string text = out.str();
    CHECK(text.find('\n') == text.size() - 1);
    CHECK(Json::parse(text)["id"] == 7);
}
