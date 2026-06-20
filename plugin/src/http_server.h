#pragma once

#include <string>
#include <functional>

// Minimal single-threaded blocking HTTP/1.1 server bound to 127.0.0.1.
// One request -> one handler call -> one response, Connection: close.
// Intended only as a local control channel for the MCP server; not hardened
// for hostile input beyond basic bounds checks.
class HttpServer
{
public:
    // handler receives the raw request body (JSON) and returns the JSON response body.
    using Handler = std::function<std::string(const std::string& body)>;

    HttpServer() = default;
    ~HttpServer();

    // Starts the accept loop on a background thread. Returns false on bind/listen failure.
    bool Start(unsigned short port, Handler handler);
    void Stop();

    bool IsRunning() const { return m_running; }
    unsigned short Port() const { return m_port; }

private:
    void Loop();

    Handler m_handler;
    unsigned short m_port = 0;
    volatile bool m_running = false;
    uintptr_t m_listenSocket = ~uintptr_t(0); // SOCKET, stored opaque to keep winsock out of the header
    void* m_thread = nullptr;                  // HANDLE
};
