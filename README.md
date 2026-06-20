# x64dbg MCP

An [MCP](https://modelcontextprotocol.io) server that lets an LLM (Claude Code, etc.)
drive [x64dbg](https://x64dbg.com). It is split in two parts:

```
LLM  ──stdio(MCP)──►  x64dbgmcp (C#)  ──HTTP 127.0.0.1:8745──►  x64dbg_mcp.dp64 (C++ plugin)  ──►  x64dbg
```

* **`plugin/`** — a native x64dbg plugin. It embeds a tiny localhost HTTP server and
  dispatches JSON requests onto the x64dbg bridge API (memory, registers, disasm,
  commands, breakpoints, memory map). This is the only component that touches x64dbg.
* **`mcp/`** — a C# MCP server (stdio) built on the official `ModelContextProtocol`
  SDK. Each tool is a thin proxy to the plugin's HTTP endpoint.

## Requirements

* Windows, Visual Studio 2022 (MSVC) + CMake ≥ 3.20
* .NET 10 SDK
* An x64dbg checkout **that has been built** (the plugin links against
  `bin/x64/x64dbg.lib` and `x64bridge.lib`). Default location: `..\x64dbg` relative
  to this repo.

## Build

```powershell
# from the repo root
./build.ps1                      # x64 / Release, installs the plugin and prints the register command
./build.ps1 -Arch x86           # 32-bit plugin for x32dbg
./build.ps1 -X64dbgRoot D:\x64dbg
```

This produces:

* `<x64dbg>\bin\x64\plugins\x64dbg_mcp.dp64` (auto-installed)
* `mcp\app\x64dbgmcp.dll` — the stable runtime folder Claude is registered against

> The C# server is built into `mcp\app` (not the default `bin\`) because that is
> the path the MCP registration points at. While a Claude session has the server
> running, `mcp\app\x64dbgmcp.dll` is **locked** — rebuilding it fails with MSB3021.
> Close the session (or `claude mcp remove x64dbg` + kill stray `dotnet.exe`)
> before rebuilding the server, then re-add it.

Building manually:

```powershell
cmake -S plugin -B plugin/build -G "Visual Studio 17 2022" -A x64 -DINSTALL_TO_X64DBG=ON
cmake --build plugin/build --config Release
dotnet build mcp -c Release
```

## Register with Claude Code

```powershell
# from the repo root, after building (use the absolute path to the built DLL)
claude mcp add --scope user --transport stdio x64dbg -- dotnet "$PWD\mcp\app\x64dbgmcp.dll"
```

Then start x64dbg. On load the plugin logs:

```
[x64dbg_mcp] MCP bridge listening on http://127.0.0.1:8745/  (commands: mcp_port, mcp_status)
```

## Usage

1. Launch x64dbg (the plugin auto-starts its server).
2. In Claude Code the `x64dbg` MCP server exposes these tools:

| Tool | Purpose |
|------|---------|
| **Session** | |
| `dbg_status` | debugging state, current `cip`, module/label |
| `dbg_process_list` / `dbg_attach` / `dbg_detach` | list processes, attach/detach by PID |
| `dbg_load` / `dbg_stop` / `dbg_restart` | load an exe (with args), stop, restart |
| `dbg_command` | run any raw x64dbg command (escape hatch) |
| **Execution** | |
| `dbg_run` / `dbg_pause` / `dbg_step_into` / `dbg_step_over` / `dbg_step_out` | execution control |
| **State** | |
| `dbg_registers` / `dbg_set_register` | read GP registers / set one |
| `dbg_threads` / `dbg_switch_thread` | list / switch threads |
| `dbg_call_stack` / `dbg_read_stack` | backtrace / raw stack slots (symbolized) |
| `dbg_eval` | evaluate an expression → value/hex |
| **Memory** | |
| `dbg_read_memory` / `dbg_write_memory` | read/write debuggee memory (hex) |
| `dbg_read_string` | read an ASCII/UTF-16 string |
| `dbg_memory_map` | memory regions (base/size/protect/info) |
| `dbg_memory_base` | base+size of the region at an address |
| `dbg_is_valid_ptr` | is an address readable |
| `dbg_set_page_rights` | change page protection |
| `dbg_mem_alloc` / `dbg_mem_free` | allocate/free memory in the debuggee |
| `dbg_find_pattern` | search a range for a byte pattern (wildcards) |
| **Code / modules** | |
| `dbg_disassemble` | disassemble N instructions |
| `dbg_assemble` | assemble & patch an instruction in memory |
| `dbg_address_info` | describe an address (module/section/label/func/string/...) |
| `dbg_branch_destination` | resolve a branch/call/jmp target |
| `dbg_xrefs` | cross-references to an address |
| `dbg_modules` / `dbg_module_sections` / `dbg_module_exports` | enumerate modules, sections, exports |
| `dbg_symbols` | enumerate a module's symbols |
| `dbg_labels` | list all labels |
| `dbg_set_comment` / `dbg_set_label` | annotate addresses |
| `dbg_patches` | list in-memory patches |
| **Breakpoints** | |
| `dbg_list_breakpoints` | all breakpoints |
| `dbg_set_breakpoint` / `dbg_delete_breakpoint` | manage breakpoints (sw/hw/mem) |
| **System** | |
| `dbg_handles` | enumerate open handles |
| `dbg_tcp_connections` | enumerate TCP connections |

Anything not covered by a dedicated tool can be driven through `dbg_command`
(e.g. `init C:\path\app.exe`, `bp kernel32.CreateFileW`, `g`).

## Configuration

* **Plugin port** — default `8745`. Change it with the x64dbg command
  `mcp_port <n>`, or persist it in `x64dbg.ini`:

  ```ini
  [x64dbg_mcp]
  port=8745
  ```

  `mcp_status` reports the current state.

* **Server target** — the C# server connects to `http://127.0.0.1:8745/` by
  default; override with the `X64DBG_MCP_URL` environment variable (must match the
  plugin port).

## Security note

The plugin binds to `127.0.0.1` only and is intended for local use. It exposes
full read/write/command control over whatever x64dbg is debugging — treat it as
you would the debugger itself. Don't expose the port beyond localhost.

## Layout

```
x64dbg_mcp/
├─ plugin/
│  ├─ src/{plugin,http_server,rpc}.cpp + headers
│  ├─ third_party/json.hpp          (nlohmann/json, vendored)
│  └─ CMakeLists.txt
├─ mcp/
│  ├─ Program.cs                    (stdio MCP host)
│  ├─ X64DbgClient.cs               (HTTP client to the plugin)
│  ├─ X64DbgTools.cs                (the MCP tools)
│  └─ x64dbgmcp.csproj
├─ build.ps1
├─ LICENSE
├─ THIRD_PARTY.md
└─ README.md
```

## License

Licensed under the **GNU General Public License v3.0** — see [LICENSE](LICENSE).

The native plugin links against the x64dbg bridge and SDK (x64dbg is GPLv3), so
the project as a whole is distributed under GPLv3. Third-party components and
their licenses are listed in [THIRD_PARTY.md](THIRD_PARTY.md).

> **Disclaimer:** this is a debugging/reverse-engineering tool. Use it only on
> software you are authorized to analyze. Provided "as is", without warranty.
