#include "http_server.h"
#include "pluginmain.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace
{
    bool g_wsaInit = false;

    // Read the full HTTP request (headers + body honoring Content-Length).
    bool RecvRequest(SOCKET s, std::string& body)
    {
        std::string buf;
        char chunk[4096];
        size_t headerEnd = std::string::npos;

        // Read until we have the header terminator.
        while (headerEnd == std::string::npos)
        {
            int n = recv(s, chunk, sizeof(chunk), 0);
            if (n <= 0)
                return false;
            buf.append(chunk, n);
            headerEnd = buf.find("\r\n\r\n");
            if (buf.size() > 64 * 1024 * 1024) // 64 MiB guard
                return false;
        }

        // Parse Content-Length (case-insensitive).
        size_t contentLength = 0;
        {
            std::string headers = buf.substr(0, headerEnd);
            std::string lower = headers;
            for (auto& c : lower)
                c = (char)tolower((unsigned char)c);
            size_t pos = lower.find("content-length:");
            if (pos != std::string::npos)
            {
                pos += strlen("content-length:");
                contentLength = (size_t)strtoull(headers.c_str() + pos, nullptr, 10);
            }
        }

        size_t bodyStart = headerEnd + 4;
        size_t have = buf.size() - bodyStart;
        while (have < contentLength)
        {
            int n = recv(s, chunk, sizeof(chunk), 0);
            if (n <= 0)
                return false;
            buf.append(chunk, n);
            have += n;
        }

        body = buf.substr(bodyStart, contentLength);
        return true;
    }

    void SendAll(SOCKET s, const std::string& data)
    {
        size_t sent = 0;
        while (sent < data.size())
        {
            int n = send(s, data.data() + sent, (int)(data.size() - sent), 0);
            if (n <= 0)
                return;
            sent += n;
        }
    }

    void SendResponse(SOCKET s, const std::string& jsonBody)
    {
        std::string resp;
        resp.reserve(jsonBody.size() + 128);
        resp += "HTTP/1.1 200 OK\r\n";
        resp += "Content-Type: application/json\r\n";
        resp += "Content-Length: " + std::to_string(jsonBody.size()) + "\r\n";
        resp += "Connection: close\r\n";
        resp += "Access-Control-Allow-Origin: *\r\n";
        resp += "\r\n";
        resp += jsonBody;
        SendAll(s, resp);
    }
}

HttpServer::~HttpServer()
{
    Stop();
}

bool HttpServer::Start(unsigned short port, Handler handler)
{
    if (m_running)
        return false;

    if (!g_wsaInit)
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return false;
        g_wsaInit = true;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET)
        return false;

    BOOL reuse = TRUE;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR ||
        listen(listenSock, 4) == SOCKET_ERROR)
    {
        closesocket(listenSock);
        return false;
    }

    m_handler = std::move(handler);
    m_port = port;
    m_listenSocket = (uintptr_t)listenSock;
    m_running = true;

    m_thread = (void*)_beginthreadex(nullptr, 0,
        [](void* self) -> unsigned
        {
            ((HttpServer*)self)->Loop();
            return 0;
        },
        this, 0, nullptr);

    if (!m_thread)
    {
        m_running = false;
        closesocket(listenSock);
        m_listenSocket = ~uintptr_t(0);
        return false;
    }
    return true;
}

void HttpServer::Loop()
{
    SOCKET listenSock = (SOCKET)m_listenSocket;
    while (m_running)
    {
        SOCKET client = accept(listenSock, nullptr, nullptr);
        if (client == INVALID_SOCKET)
        {
            if (!m_running)
                break;
            continue;
        }

        std::string body;
        if (RecvRequest(client, body))
        {
            std::string response;
            try
            {
                response = m_handler(body);
            }
            catch (const std::exception& e)
            {
                response = std::string("{\"error\":\"handler exception: ") + e.what() + "\"}";
            }
            catch (...)
            {
                response = "{\"error\":\"handler exception\"}";
            }
            SendResponse(client, response);
        }
        closesocket(client);
    }
}

void HttpServer::Stop()
{
    if (!m_running)
        return;
    m_running = false;

    SOCKET listenSock = (SOCKET)m_listenSocket;
    if (listenSock != (SOCKET)~uintptr_t(0))
    {
        closesocket(listenSock); // unblocks accept()
        m_listenSocket = ~uintptr_t(0);
    }

    if (m_thread)
    {
        WaitForSingleObject((HANDLE)m_thread, 2000);
        CloseHandle((HANDLE)m_thread);
        m_thread = nullptr;
    }
}
