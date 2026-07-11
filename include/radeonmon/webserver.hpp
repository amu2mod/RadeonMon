#pragma once

// Header order matters here: windows.h pulls in the legacy winsock.h by
// default, which then conflicts with winsock2.h/ws2tcpip.h (used by http.h).
// WIN32_LEAN_AND_MEAN stops windows.h from including winsock.h, and
// including winsock2.h first guarantees the new-style socket headers win.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <http.h>

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <string>
#include <optional>

#include <radeonmon/structures.hpp>

#pragma comment(lib, "httpapi.lib")

/**
 * Minimal HTTP server built on the Windows HTTP Server API (http.sys).
 * Serves one HTML file at "/" and a JSON payload at "/api" (produced by a caller-supplied callback), so the app decides what data to expose.
 *
 * Must be Init() first to bind to an IPv4 interface
 * then Start() to launch the worker thread
 */
class WebServer
{
public:
    // Called on the server's worker thread whenever /api is requested.
    // Must return a JSON-encoded string.
    // using ApiHandler = std::function<std::string()>;
    using ApiHandler = std::function<int(char *, int)>;

    WebServer();
    ~WebServer();

    // urlPrefix example: L"http://localhost:8080/"  (must end with '/')
    // htmlFilePath: full path to the .html file to serve at "/"
    bool Init(const std::wstring &urlPrefix, const std::wstring &htmlFilePath);

    // Starts listening on a background thread.
    bool Start();

    // Stops the server and blocks until the worker thread exits.
    void Stop();

    bool LaunchServerOnInterface(const NetworkInterface &);

    bool IsRunning() const { return m_running; }

    // Registers the callback used to answer GET /api. Not thread-safe with
    // respect to concurrent requests - call this before Start().
    void SetApiHandler(ApiHandler handler) { m_apiHandler = std::move(handler); }

    // Check the validity of the bound interface in case of network change event
    void CheckInterface(const std::vector<NetworkInterface> &interfaces);

    inline const std::optional<NetworkInterface> GetBoundInterface() const { return m_boundInterface; }
    inline bool isBoundTo(const NetworkInterface &netIf) const { return (m_boundInterface == netIf); }

private:
    void WorkerThread();
    void HandleRequest(HTTP_REQUEST *pRequest);
    bool SendFileResponse(HTTP_REQUEST_ID requestId, std::wstring filename = L"");
    // bool SendJsonResponse(HTTP_REQUEST_ID requestId, const std::string &json);
    bool SendErrorResponse(HTTP_REQUEST_ID requestId, USHORT statusCode, const char *reason);
    bool SendResourceResponse(HTTP_REQUEST_ID requestId, int resourceId);
    bool SendJsonResponse(HTTP_REQUEST_ID requestId, const char *json, ULONG jsonSize);

    HANDLE m_hReqQueue = NULL;
    HTTP_SERVER_SESSION_ID m_serverSession = 0;
    HTTP_URL_GROUP_ID m_urlGroup = 0;
    std::wstring m_urlPrefix;
    std::wstring m_htmlFilePath;
    std::thread m_workerThread;
    std::atomic<bool> m_running;
    bool m_initialized;
    ApiHandler m_apiHandler;
    std::optional<NetworkInterface> m_boundInterface;
    uint16_t m_port{};
};