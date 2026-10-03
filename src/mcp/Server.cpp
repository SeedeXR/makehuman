// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/mcp/Server.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <istream>
#include <ostream>
#include <stdexcept>

namespace mh::mcp {

namespace {

/// Every diagnostic goes here. See the header: stdout carries the protocol and
/// one stray byte on it is a parse error at the client.
void logLine(std::string_view level, std::string_view event, const Json& detail) {
    // Structured, one JSON object per line, so a session log can be read by the
    // same tools that read the protocol. A human-formatted log would be easier
    // to skim and impossible to query, and "what did the model actually ask
    // for" is a query.
    Json entry;
    entry["t"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
    entry["level"] = level;
    entry["event"] = event;
    if (!detail.is_null()) entry["detail"] = detail;
    std::fprintf(stderr, "%s\n", entry.dump().c_str());
}

Json errorObject(ErrorCode code, std::string_view message, const Json& data = Json()) {
    Json e;
    e["code"]    = static_cast<int32_t>(code);
    e["message"] = message;
    if (!data.is_null()) e["data"] = data;
    return e;
}

Json responseFor(const Json& id, Json result) {
    Json r;
    r["jsonrpc"] = "2.0";
    r["id"]      = id;
    r["result"]  = std::move(result);
    return r;
}

Json errorFor(const Json& id, ErrorCode code, std::string_view message, const Json& data = Json()) {
    Json r;
    r["jsonrpc"] = "2.0";
    // A response to a request we could not parse has a null id, which the
    // specification requires rather than omitting the field.
    r["id"]    = id.is_null() ? Json() : id;
    r["error"] = errorObject(code, message, data);
    return r;
}

}  // namespace

std::vector<std::string> supportedProtocolVersions() {
    // Newest first. Listing several is what lets this server keep working when
    // a client updates: the one it asks for is echoed when we implement it, and
    // otherwise it is told what we do speak rather than being failed.
    return {"2025-06-18", "2025-03-26", "2024-11-05"};
}

Server::Server(std::string name, std::string version)
    : name_(std::move(name)), version_(std::move(version)) {}

void Server::add(Tool tool) {
    // A duplicate name would make `tools/call` depend on registration order,
    // which is the kind of thing that works until someone reorders a file.
    const auto clash =
        std::ranges::find_if(tools_, [&](const Tool& t) { return t.name == tool.name; });
    if (clash != tools_.end()) {
        throw std::logic_error("duplicate MCP tool name: " + tool.name);
    }
    tools_.push_back(std::move(tool));
}

Json Server::handle(const Json& request) {
    ++stats_.requests;

    if (!request.is_object() || request.value("jsonrpc", "") != "2.0") {
        ++stats_.protocolErrors;
        logLine("warn", "invalid_request", Json());
        return errorFor(Json(), ErrorCode::InvalidRequest, "expected a JSON-RPC 2.0 object");
    }

    const Json id            = request.contains("id") ? request.at("id") : Json();
    const bool notify        = !request.contains("id");
    const std::string method = request.value("method", "");
    const Json params        = request.contains("params") ? request.at("params") : Json::object();

    const auto reply = [&](Json r) -> Json {
        return notify ? Json() : responseFor(id, std::move(r));
    };
    const auto fail = [&](ErrorCode c, std::string_view m, const Json& d = Json()) -> Json {
        ++stats_.protocolErrors;
        logLine("warn", "request_failed", Json{{"method", method}, {"message", m}});
        return notify ? Json() : errorFor(id, c, m, d);
    };

    if (method == "initialize") {
        const std::string asked = params.value("protocolVersion", "");
        const auto known        = supportedProtocolVersions();
        // Echo what the client asked for when we implement it; otherwise answer
        // with ours. Refusing an unknown version would break on the next client
        // release for no reason -- the specification's own guidance.
        const std::string chosen =
            std::ranges::find(known, asked) != known.end() ? asked : known.front();

        Json result;
        result["protocolVersion"] = chosen;
        result["capabilities"]    = Json{{"tools", Json{{"listChanged", false}}}};
        result["serverInfo"]      = Json{{"name", name_}, {"version", version_}};
        initialised_              = true;
        logLine("info", "initialize",
                Json{{"client", params.value("clientInfo", Json::object())},
                     {"asked", asked},
                     {"chosen", chosen}});
        return reply(result);
    }

    if (method == "notifications/initialized") {
        logLine("info", "initialized", Json());
        return Json();  // a notification: no response, ever
    }

    if (method == "ping") {
        // Answered BEFORE the initialised check on purpose: a liveness probe
        // that only works on a fully negotiated session cannot tell you the
        // difference between "down" and "not yet initialised", which is exactly
        // what a probe is for.
        return reply(Json::object());
    }

    if (!initialised_) {
        return fail(ErrorCode::InvalidRequest, "initialize must be called first");
    }

    if (method == "tools/list") {
        Json list = Json::array();
        for (const Tool& t : tools_) {
            list.push_back(Json{
                {"name", t.name}, {"description", t.description}, {"inputSchema", t.inputSchema}});
        }
        return reply(Json{{"tools", std::move(list)}});
    }

    if (method == "tools/call") {
        const std::string wanted = params.value("name", "");
        const auto it =
            std::ranges::find_if(tools_, [&](const Tool& t) { return t.name == wanted; });
        if (it == tools_.end()) {
            return fail(ErrorCode::InvalidParams, "no such tool: " + wanted);
        }
        const Json arguments =
            params.contains("arguments") ? params.at("arguments") : Json::object();

        ++stats_.toolCalls;
        const auto started = std::chrono::steady_clock::now();
        try {
            Json content  = it->call(arguments);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count();
            logLine("info", "tool_ok", Json{{"tool", wanted}, {"ms", ms}});
            return reply(Json{{"content", std::move(content)}, {"isError", false}});
        } catch (const ToolError& e) {
            // A failure the CALLER can act on. Reported as a successful
            // protocol exchange carrying isError, which is what the
            // specification asks for: the model reads the text and tries
            // something else, rather than seeing a transport fault.
            ++stats_.toolErrors;
            logLine("warn", "tool_error", Json{{"tool", wanted}, {"message", e.what()}});
            return reply(
                Json{{"content", Json::array({Json{{"type", "text"}, {"text", e.what()}}})},
                     {"isError", true}});
        } catch (const std::exception& e) {
            // Anything else is OUR bug, and says so.
            ++stats_.toolErrors;
            logLine("error", "tool_crashed", Json{{"tool", wanted}, {"message", e.what()}});
            return fail(ErrorCode::InternalError, std::string("tool failed: ") + e.what());
        }
    }

    return fail(ErrorCode::MethodNotFound, "unknown method: " + method);
}

int Server::run(std::istream& in, std::ostream& out) {
    logLine("info", "listening",
            Json{{"name", name_}, {"version", version_}, {"tools", tools_.size()}});
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        Json request;
        try {
            request = Json::parse(line);
        } catch (const Json::exception& e) {
            // The loop CONTINUES. One unparseable line from a client must not
            // end the session, or a single malformed request looks like a
            // crash to the user.
            ++stats_.protocolErrors;
            logLine("warn", "parse_error", Json{{"message", e.what()}});
            out << errorFor(Json(), ErrorCode::ParseError, e.what()).dump() << "\n" << std::flush;
            continue;
        }

        const Json response = handle(request);
        if (!response.is_null()) {
            out << response.dump() << "\n" << std::flush;
        }
    }
    logLine("info", "closed",
            Json{{"requests", stats_.requests},
                 {"toolCalls", stats_.toolCalls},
                 {"toolErrors", stats_.toolErrors},
                 {"protocolErrors", stats_.protocolErrors}});
    return 0;
}

}  // namespace mh::mcp
