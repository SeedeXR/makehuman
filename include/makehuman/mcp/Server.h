// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A Model Context Protocol server: JSON-RPC 2.0 over stdio, so an LLM client
// can drive the character tooling directly instead of guessing at a CLI.
//
// WHY C++ AND NOT A PYTHON SHIM. `CLAUDE.md` and `project_context.md` both say
// the end state ships NO PYTHON. The generators under `tools/` are authoring
// tools and may stay Python; this is shipped runtime -- it is how a user drives
// the application -- so it is C++ like everything else that ships. It also adds
// no dependency: `nlohmann/json` is already pinned and cleared (LICENSING.md
// 5.1) and already used by Qt-free `core`.
//
// STDOUT IS THE PROTOCOL CHANNEL. Every diagnostic goes to stderr, without
// exception. A single stray `printf` corrupts the stream and the client sees a
// parse error rather than the message that caused it. This is not a style rule:
// the same mistake was made this week in `--print-parameters`, where a progress
// line on stdout made a captured parameter vector unreadable by the flag meant
// to read it back.
//
// SURVIVING CLIENT UPDATES. The protocol version is NEGOTIATED, not assumed:
// `initialize` echoes the client's version when it is one we implement and
// falls back to ours when it is not, which is what the specification asks for.
// Nothing here depends on the quirks of a particular client, and the transport
// is the lowest common denominator every MCP client supports.
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace mh::mcp {

using Json = nlohmann::ordered_json;

/// JSON-RPC 2.0 error codes, plus the one MCP adds.
///
/// Spelled out rather than written inline, because a client distinguishes
/// "your request was malformed" from "the tool you asked for failed" and
/// collapsing the two makes every failure look like a bug in the caller.
enum class ErrorCode : int32_t {
    ParseError     = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams  = -32602,
    InternalError  = -32603,
};

/// Thrown by a tool to report a failure the CALLER can act on.
///
/// Distinct from letting an exception escape: an `std::exception` becomes an
/// InternalError, which says "this server has a bug". A `ToolError` says "that
/// input does not work, here is why", which is what an LLM needs in order to
/// try something else rather than give up.
class ToolError : public std::runtime_error {
public:
    explicit ToolError(std::string what) : std::runtime_error(std::move(what)) {}
};

/// One callable tool.
struct Tool {
    std::string name;
    /// Written for a model to read. It is the only thing a client has to decide
    /// WHETHER to call this, so it says what the tool does and what it returns.
    std::string description;
    /// JSON Schema for the arguments. A client validates against it before
    /// calling, which turns a class of failure into a question the model can
    /// answer itself.
    Json inputSchema;
    std::function<Json(const Json& arguments)> call;
};

/// Counts, for the liveness tool and for tests.
struct Stats {
    uint64_t requests{0};
    uint64_t toolCalls{0};
    uint64_t toolErrors{0};
    uint64_t protocolErrors{0};
};

class Server {
public:
    Server(std::string name, std::string version);

    void add(Tool tool);

    [[nodiscard]] const std::vector<Tool>& tools() const noexcept { return tools_; }

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

    /// Handles ONE parsed message and returns the response.
    ///
    /// Returns a null Json for a notification -- a request with no `id` -- which
    /// the specification says must not be answered. Separated from the loop so
    /// every branch is testable without pipes or processes.
    [[nodiscard]] Json handle(const Json& request);

    /// Reads line-delimited JSON from @p in and writes responses to @p out.
    ///
    /// Line-delimited because that is what the stdio transport uses. A message
    /// that fails to parse produces a ParseError response and the loop
    /// CONTINUES: one bad line from a client must not end the session, or a
    /// single malformed request looks to the user like the server crashed.
    int run(std::istream& in, std::ostream& out);

private:
    std::string name_;
    std::string version_;
    std::vector<Tool> tools_;
    Stats stats_;
    bool initialised_{false};
};

/// The protocol versions this server implements, newest first.
[[nodiscard]] std::vector<std::string> supportedProtocolVersions();

}  // namespace mh::mcp
