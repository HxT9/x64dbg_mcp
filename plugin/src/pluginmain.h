#pragma once

// x64dbg plugin SDK headers (resolved from the x64dbg source tree via CMake include dirs)
#include <windows.h>
#include "bridgemain.h"
#include "_plugins.h"
#include "_dbgfunctions.h"
#include "_scriptapi_module.h"
#include "_scriptapi_pattern.h"
#include "_scriptapi_label.h"

#define PLUGIN_NAME "x64dbg_mcp"
#define PLUGIN_VERSION 1

// Default loopback endpoint the embedded HTTP server listens on.
// Overridable via the x64dbg setting [x64dbg_mcp] -> port, or the command "mcp_port <n>".
#define MCP_DEFAULT_PORT 8745

extern int g_pluginHandle;
extern HWND g_hwndDlg;

// Logging helper that routes to the x64dbg log.
void McpLog(const char* fmt, ...);
