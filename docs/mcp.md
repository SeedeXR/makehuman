# Driving MakeHuman from an LLM client

`makehuman --mcp` is a [Model Context Protocol](https://modelcontextprotocol.io)
server: JSON-RPC 2.0 over stdio, so Claude Code, Codex or any other MCP client
can build, inspect, photograph and fit a character directly.

It is the same binary as the application. There is no Python, no daemon and no
second install.

## Installing it

Claude Code:

```bash
claude mcp add makehuman -- /Applications/MakeHuman.app/Contents/MacOS/makehuman --mcp
```

Any client that reads an `mcpServers` map — `.mcp.json`, Codex, and most
others:

```json
{
  "mcpServers": {
    "makehuman": {
      "command": "/Applications/MakeHuman.app/Contents/MacOS/makehuman",
      "args": ["--mcp"]
    }
  }
}
```

Built from source rather than installed, the binary is at
`build/macos-arm64-release/src/app/makehuman.app/Contents/MacOS/makehuman`.

### If the client reports the server failed to start

Run the command by hand:

```
$ echo '{"jsonrpc":"2.0","id":1,"method":"ping"}' | makehuman --mcp
MakeHumanCpp: Unknown option 'mcp'.
```

That exact message means the installed application predates `--mcp`. The
installer replaces `/Applications/MakeHuman.app` wholesale, so an app installed
before this feature has no knowledge of it:

```bash
./scripts/install.sh
```

A working server answers `{"jsonrpc":"2.0","id":1,"result":{}}` and exits 0.

## Checking it is alive

`ping` is answered **before** `initialize`, on purpose: a liveness probe that
only works on a negotiated session cannot tell "down" from "not yet
initialised", which is the one thing a probe exists to answer.

```bash
echo '{"jsonrpc":"2.0","id":1,"method":"ping"}' | makehuman --mcp
```

A `{"result":{}}` on stdout means the server is up. The `health` tool adds the
version and what it has served so far.

Diagnostics go to **stderr**, as structured JSON, one object per line — every
tool call with its name and how long it took. stdout carries the protocol and
nothing else; `--mcp` redirects the application's own progress output at the
file descriptor so a stray `printf` cannot corrupt the stream.

## The tools

| Tool | What it does |
|---|---|
| `health` | Liveness, version, counters. Never fails. |
| `list_parameters` | Every parameter: name, range, default, whether it is coupled. |
| `get_parameters` | The character as one number per parameter. Save it to reproduce one exactly. |
| `set_slider` | Set one named parameter and re-apply the modifier stack. |
| `render` | Render and **return the image**, from `front`, `back`, `left` or `right`. |
| `add_reference` | Register a reference photograph for a view, or a closeup by label. |
| `list_references` | What is registered, and which views are still missing. |
| `compare_to_reference` | Score the character against one photograph. |
| `fit_to_references` | Search the body parameters for the best match, and apply it. |

## Building a character from photographs

Supply **front, back, left and right**. More views means a better-constrained
fit — a front shot alone cannot tell a deep chest from a flat one. Closeups of
key areas are registered with a `label` instead of a `view`.

```
add_reference   {"path": "front.jpg", "view": "front"}
add_reference   {"path": "left.jpg",  "view": "left"}
...
fit_to_references {"passes": 4}
render          {"view": "front"}      → look at it
set_slider      ...                    → adjust by eye
```

### What the fit can and cannot do

It moves the **eight parameters that change the body's outline**. Everything
below is a limit of the method, not a defect, and the tool descriptions repeat
each one:

- **No faces.** An outline does not see a nose. Set facial detail yourself from
  the closeups.
- **No absolute stature.** A photograph does not carry one. State the height if
  it matters.
- **It will not reach a perfect score.** A character measured against its own
  render scores 0.998, not 1.0: an antialiased edge masks slightly differently
  through alpha than through colour. The fit reaches about 0.95. Treat the
  result as a strong starting point to adjust by eye.

Measured by rebuilding a known character from its own renders, four views,
deterministic across runs:

| Passes | Score | Mean parameter error | Cost |
|---|---|---|---|
| 2 | 0.87 | 0.13 | ~320 renders, ~5 s |
| 4 | 0.92 | 0.10 | ~640 renders, ~9 s |
| 6 | 0.95 | 0.08 | ~960 renders, ~16 s |

### Photographs the fit can use

The subject is separated from the background by colour distance from the
image's **top-left pixel**. That works for a plain backdrop and fails for a
cluttered room.

`add_reference` and `compare_to_reference` both return `coverage` — the
fraction of the image taken to be the subject. **Above 0.9 the separation
failed** and any score computed from it is meaningless, however reasonable the
number looks. Ask for a shot against a plain backdrop that reaches the
top-left corner.

## Surviving a client update

`initialize` **negotiates**: it echoes the client's protocol version when it is
one this server implements (`2025-06-18`, `2025-03-26`, `2024-11-05`) and
answers with its own when it is not, rather than refusing. Nothing here depends
on a particular client's quirks, and stdio is the transport every MCP client
supports.

A failure you can act on comes back as a successful exchange carrying
`isError`, with the reason in the text — not as a JSON-RPC error. The
distinction matters: a transport error tells a client the *server* is broken,
and a model that believes that stops asking.

## Depth maps

**Not wired into any of the tools above**, and the reason is worth reading
before asking for it.

There is an optional MoGe module (`MH_WITH_MOGE`, **off by default**) that runs
Microsoft's MoGe through ONNX Runtime. It loads, it runs, and it is tested. It
is not used by the MCP server, because what it gives and what the fit needs are
not yet the same thing:

- Its `mask` output marks where the model's **geometry prediction is valid** —
  **not** where the subject is. That distinction was nearly got wrong here: on
  a render against a flat black background the mask traces the body exactly,
  which looks like segmentation and is not. It was excluding the void. Measured
  against a known subject on a cluttered scene, MoGe's mask scores IoU 0.154
  and the corner-colour rule it was supposed to replace scores 0.159 — no
  better.
- Its **metric depth** is implemented (`foundation::recoverFocalShift`) and the
  solve is validated against cameras we chose, to 1–3%. The model behind it is
  not. Measured against real Wikimedia Commons photographs where EXIF gives the
  true field of view exactly, MoGe's camera estimate is wrong by **+31% to
  +510%**, regressing toward a normal lens whatever the real one was. At the
  most favourable real measurement that puts a 1.75 m person at about 1.32 m.
  So it is not wired into the fit, and `metric` means *recovered*, not
  *accurate*.

If you want to experiment: `tools/fetch_moge.sh` downloads the model (134 MB,
SHA256-pinned, cached outside the repository and never committed), then
configure with `-DMH_WITH_MOGE=ON`. Nothing in the MCP tools changes.

Supplying your own depth map is still the cleanest route, and still unbuilt —
no objective has been decided for one. Note that the two **side views** already
carry the chest and belly depth profile a depth map would add, so its marginal
value on top of four orthogonal references is smaller than it looks.
