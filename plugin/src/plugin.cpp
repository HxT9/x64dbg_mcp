#include "pluginmain.h"
#include "http_server.h"
#include "rpc.h"

#include <cstdarg>
#include <cstdio>
#include <string>

int g_pluginHandle = 0;
HWND g_hwndDlg = nullptr;

static HttpServer g_server;
static unsigned short g_port = MCP_DEFAULT_PORT;

void McpLog(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    _plugin_logprintf("[%s] %s\n", PLUGIN_NAME, buf);
}

// Command: "mcp_port <n>"  -> restart the server on a new port.
static bool cbMcpPort(int argc, char** argv)
{
    if (argc < 2)
    {
        McpLog("current port: %u", (unsigned)g_port);
        return true;
    }
    int port = atoi(argv[1]);
    if (port < 1 || port > 65535)
    {
        McpLog("invalid port: %s", argv[1]);
        return false;
    }
    g_server.Stop();
    g_port = (unsigned short)port;
    if (g_server.Start(g_port, HandleRpc))
        McpLog("server restarted on 127.0.0.1:%u", (unsigned)g_port);
    else
        McpLog("FAILED to bind 127.0.0.1:%u", (unsigned)g_port);
    return true;
}

// Command: "mcp_status"
static bool cbMcpStatus(int argc, char** argv)
{
    McpLog("server %s on 127.0.0.1:%u",
        g_server.IsRunning() ? "RUNNING" : "stopped", (unsigned)g_port);
    return true;
}

extern "C" __declspec(dllexport) bool pluginit(PLUG_INITSTRUCT* initStruct)
{
    initStruct->sdkVersion = PLUG_SDKVERSION;
    initStruct->pluginVersion = PLUGIN_VERSION;
    strncpy_s(initStruct->pluginName, PLUGIN_NAME, _TRUNCATE);
    g_pluginHandle = initStruct->pluginHandle;

    _plugin_registercommand(g_pluginHandle, "mcp_port", cbMcpPort, false);
    _plugin_registercommand(g_pluginHandle, "mcp_status", cbMcpStatus, false);

    // Allow overriding the port via the x64dbg.ini setting [x64dbg_mcp] -> port.
    duint iniPort = 0;
    if (BridgeSettingGetUint(PLUGIN_NAME, "port", &iniPort) && iniPort >= 1 && iniPort <= 65535)
        g_port = (unsigned short)iniPort;

    return true;
}

extern "C" __declspec(dllexport) void plugsetup(PLUG_SETUPSTRUCT* setupStruct)
{
    g_hwndDlg = setupStruct->hwndDlg;

    if (g_server.Start(g_port, HandleRpc))
        McpLog("MCP bridge listening on http://127.0.0.1:%u/  (commands: mcp_port, mcp_status)", (unsigned)g_port);
    else
        McpLog("FAILED to start MCP bridge on 127.0.0.1:%u (port in use?). Use 'mcp_port <n>'.", (unsigned)g_port);
}

extern "C" __declspec(dllexport) bool plugstop()
{
    g_server.Stop();
    return true;
}
