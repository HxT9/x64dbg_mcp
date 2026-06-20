# Third-party components

This project bundles or depends on the following third-party software.

## Bundled (vendored)

- **nlohmann/json** (`plugin/third_party/json.hpp`) — MIT License.
  Copyright (c) 2013-2022 Niels Lohmann. <https://github.com/nlohmann/json>
  The full MIT text is preserved in the file header.

## NuGet dependencies (C# server, `mcp/`)

- **ModelContextProtocol** — MIT License. <https://github.com/modelcontextprotocol/csharp-sdk>
- **Microsoft.Extensions.Hosting** — MIT License.

## Build/link dependencies (not redistributed)

- **x64dbg** — GPLv3. The native plugin links against the x64dbg bridge
  (`x64bridge.lib` / `x64dbg.lib`) and uses its plugin SDK headers.
  <https://github.com/x64dbg/x64dbg>. This is why this project is licensed GPLv3.
