#include "radeonmon/webserver.hpp"

#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <iphlpapi.h>
#include <ws2tcpip.h>

#include "radeonmon/resource_ids.h"
#include "radeonmon/structures.hpp"
#include "radeonmon/helpers.hpp"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

WebServer::WebServer() : m_hReqQueue(NULL), m_running(false), m_initialized(false) {}

WebServer::~WebServer()
{
    Stop();

    if (m_initialized)
        HttpTerminate(HTTP_INITIALIZE_SERVER, nullptr);
}

bool WebServer::Init(const std::wstring &urlPrefix, const std::wstring &htmlFilePath)
{

    // required for winsock functions
    WSADATA wsaData;
    int r = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (r != 0)
        LOG_DEBUG("[WebServer] WSAStartup failed: %d", r);

    m_urlPrefix = urlPrefix;
    m_htmlFilePath = htmlFilePath;

    ULONG result = NO_ERROR;
    HTTP_BINDING_INFO bindingInfo = {};

    result = HttpInitialize(HTTPAPI_VERSION_2, HTTP_INITIALIZE_SERVER, nullptr);

    if (result != NO_ERROR)
        goto fail;

    result = HttpCreateServerSession(HTTPAPI_VERSION_2, &m_serverSession, 0);

    if (result != NO_ERROR)
        goto fail;

    result = HttpCreateUrlGroup(m_serverSession, &m_urlGroup, 0);

    if (result != NO_ERROR)
        goto fail;

    result = HttpCreateRequestQueue(HTTPAPI_VERSION_2, nullptr, nullptr, 0, &m_hReqQueue);

    if (result != NO_ERROR)
        goto fail;

    bindingInfo.Flags.Present = 1;
    bindingInfo.RequestQueueHandle = m_hReqQueue;

    result = HttpSetUrlGroupProperty(m_urlGroup, HttpServerBindingProperty, &bindingInfo, sizeof(bindingInfo));

    if (result != NO_ERROR)
        goto fail;

    result = HttpAddUrlToUrlGroup(m_urlGroup, m_urlPrefix.c_str(), 0, 0);

    if (result != NO_ERROR)
        goto fail;

    m_initialized = true;
    return true;

fail:

    if (m_hReqQueue)
    {
        CloseHandle(m_hReqQueue);
        m_hReqQueue = NULL;
    }

    if (m_urlGroup)
    {
        HttpCloseUrlGroup(m_urlGroup);
        m_urlGroup = 0;
    }

    if (m_serverSession)
    {
        HttpCloseServerSession(m_serverSession);
        m_serverSession = 0;
    }

    HttpTerminate(HTTP_INITIALIZE_SERVER, nullptr);

    return false;
}

bool WebServer::Start()
{
    if (!m_initialized || m_running)
        return false;

    m_running = true;
    m_workerThread = std::thread(&WebServer::WorkerThread, this);
    return true;
}

void WebServer::Stop()
{
    LOG_INFO("[WebServer] Stopping server");

    if (!m_running)
        return;

    m_running = false;

    if (m_urlGroup)
        HttpRemoveUrlFromUrlGroup(m_urlGroup, m_urlPrefix.c_str(), 0);

    if (m_hReqQueue)
    {
        CloseHandle(m_hReqQueue);
        m_hReqQueue = NULL;
    }

    if (m_urlGroup)
    {
        HttpCloseUrlGroup(m_urlGroup);
        m_urlGroup = 0;
    }

    if (m_serverSession)
    {
        HttpCloseServerSession(m_serverSession);
        m_serverSession = 0;
    }

    if (m_workerThread.joinable())
        m_workerThread.join();

    m_boundInterface.reset();

    WSACleanup();

    LOG_INFO("[WebServer] Server stopped");
}

void WebServer::WorkerThread()
{
    ULONG bufferSize = 8192;
    std::vector<UCHAR> buffer(bufferSize);
    HTTP_REQUEST *pRequest = reinterpret_cast<HTTP_REQUEST *>(buffer.data());

    while (m_running)
    {
        RtlZeroMemory(pRequest, bufferSize);

        ULONG bytesRead = 0;
        ULONG result = HttpReceiveHttpRequest(m_hReqQueue, HTTP_NULL_ID, 0, pRequest, bufferSize, &bytesRead, NULL);

        if (result == NO_ERROR)
        {
            HandleRequest(pRequest);
        }
        else if (result == ERROR_MORE_DATA)
        {
            // Request (e.g. headers) larger than our buffer - grow and retry.
            bufferSize = bytesRead;
            buffer.resize(bufferSize);
            pRequest = reinterpret_cast<HTTP_REQUEST *>(buffer.data());
        }
        else
        {
            // Happens when Stop() closes the queue handle. Exit the loop.
            break;
        }
    }
}

static const char *GetContentTypeFromResourceId(int resourceId)
{
    switch (resourceId)
    {
    case IDR_INDEX_HTML:
        return "text/html; charset=utf-8";

    case IDR_STYLES_CSS:
        return "text/css; charset=utf-8";

    case IDR_SCRIPT_JS:
        return "application/javascript; charset=utf-8";

        // case IDR_JSON:
        //     return "application/json; charset=utf-8";

        // case IDR_PNG:
        //     return "image/png";

        // case IDR_ICO:
        //     return "image/x-icon";

    default:
        return "text/html; charset=utf-8";
    }
}

bool WebServer::SendResourceResponse(HTTP_REQUEST_ID requestId, int resourceId)
{
    // LOG_DEBUG("[WebServer] SendResourceResponse");

    HMODULE module = GetModuleHandle(nullptr);

    HRSRC resource = FindResource(module, MAKEINTRESOURCE(resourceId), RT_RCDATA);
    if (!resource)
    {
        LOG_ERROR("FindResource failed, GetLastError=%lu, resourceId=%d", GetLastError(), resourceId);
        return false;
    }

    if (!resource)
        return false;

    HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded)
        return false;

    DWORD size = SizeofResource(module, resource);

    BYTE *data = static_cast<BYTE *>(LockResource(loaded));
    if (!data || size == 0)
        return false;

    HTTP_RESPONSE response;
    RtlZeroMemory(&response, sizeof(response));

    response.StatusCode = 200;
    response.pReason = "OK";
    response.ReasonLength = 2;

    const char *contentType = GetContentTypeFromResourceId(resourceId);

    HTTP_KNOWN_HEADER contentTypeHeader;
    RtlZeroMemory(&contentTypeHeader, sizeof(contentTypeHeader));

    contentTypeHeader.pRawValue = contentType;
    contentTypeHeader.RawValueLength = (USHORT)strlen(contentType);

    response.Headers.KnownHeaders[HttpHeaderContentType] = contentTypeHeader;

    HTTP_DATA_CHUNK dataChunk;
    RtlZeroMemory(&dataChunk, sizeof(dataChunk));

    dataChunk.DataChunkType = HttpDataChunkFromMemory;
    dataChunk.FromMemory.pBuffer = data;
    dataChunk.FromMemory.BufferLength = size;

    response.EntityChunkCount = 1;
    response.pEntityChunks = &dataChunk;

    ULONG bytesSent = 0;

    ULONG result = HttpSendHttpResponse(m_hReqQueue, requestId, 0, &response, nullptr, &bytesSent, nullptr, 0, nullptr, nullptr);

    return result == NO_ERROR;
}

#ifdef _DEBUG
[[maybe_unused]]
static std::string GetPeerIp(HTTP_REQUEST *pRequest)
{
    char ip[INET6_ADDRSTRLEN] = "unknown";

    if (pRequest->Address.pRemoteAddress)
    {
        int addrLen = (pRequest->Address.pRemoteAddress->sa_family == AF_INET) ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6);
        int rc = getnameinfo(pRequest->Address.pRemoteAddress, addrLen, ip, sizeof(ip), nullptr, 0, NI_NUMERICHOST);
        if (rc != 0)
        {
            LOG_DEBUG("[WebServer] getnameinfo failed: %d (WSAGetLastError=%d)", rc, WSAGetLastError());
            return "unknown";
        }
    }
    else
    {
        LOG_DEBUG("[WebServer] pRemoteAddress is null");
    }

    return ip;
}
#endif

void WebServer::HandleRequest(HTTP_REQUEST *pRequest)
{
    if (pRequest->Verb != HttpVerbGET)
    {
        SendErrorResponse(pRequest->RequestId, 405, "Method Not Allowed");
        return;
    }

    // pAbsPath is the decoded request path, e.g. L"/" or L"/api".
    // CookedUrl.pAbsPath is not null-terminated by length alone; it *is*
    // null-terminated in practice for HTTP Server API requests, but we use
    // AbsPathLength defensively instead of relying on that.
    const wchar_t *path = pRequest->CookedUrl.pAbsPath;
    size_t pathLen = pRequest->CookedUrl.AbsPathLength / sizeof(WCHAR);
    std::wstring route(path, pathLen);

    // LOG_DEBUG("[WebServer] route: %ls", route.c_str());

    if (route == L"/api")
    {
        if (!m_apiHandler)
        {
            SendErrorResponse(pRequest->RequestId, 500, "No API handler registered");
            return;
        }

        // START_CHRONO(jsons);
        // std::string json = m_apiHandler();
        char jsonBuffer[GPU_JSON_BUFFER_SIZE];
        int jsonLength = BuildCombinedJson(jsonBuffer, sizeof(jsonBuffer));
        // END_CHRONO(jsons, "json builder");
        // SendJsonResponse(pRequest->RequestId, json);
        SendJsonResponse(pRequest->RequestId, jsonBuffer, jsonLength);

        return;
    }
#ifdef LOCALFILES
    if (route == L"/styles.css")
    {
        SendFileResponse(pRequest->RequestId, L"styles.css");
        return;
    }

    if (route == L"/script.js")
    {
        SendFileResponse(pRequest->RequestId, L"script.js");
        return;
    }

    if (route == L"/")
    {
        SendFileResponse(pRequest->RequestId);
        return;
    }
#else
    if (route == L"/styles.css")
    {
        if (!SendResourceResponse(pRequest->RequestId, IDR_STYLES_CSS))
            SendErrorResponse(pRequest->RequestId, 500, "Failed to load resource");
        return;
    }

    if (route == L"/script.js")
    {
        if (!SendResourceResponse(pRequest->RequestId, IDR_SCRIPT_JS))
            SendErrorResponse(pRequest->RequestId, 500, "Failed to load resource");
        return;
    }
    if (route == L"/")
    {
        LOG_DEBUG("[WebServer] [%s] GET %ls", GetPeerIp(pRequest).c_str(), route.c_str());
        if (!SendResourceResponse(pRequest->RequestId, IDR_INDEX_HTML))
            SendErrorResponse(pRequest->RequestId, 500, "Failed to load resource");
        return;
    }
#endif

    SendErrorResponse(pRequest->RequestId, 404, "Not Found");
}

static const char *GetContentType(const std::wstring &filename)
{
    if (filename.ends_with(L".css"))
        return "text/css; charset=utf-8";

    if (filename.ends_with(L".js"))
        return "application/javascript; charset=utf-8";

    if (filename.ends_with(L".html"))
        return "text/html; charset=utf-8";

    return "text/html; charset=utf-8";
}

bool WebServer::SendFileResponse(HTTP_REQUEST_ID requestId, std::wstring filename)
{
    std::ifstream file;

    if (filename.empty())
        file = std::ifstream(m_htmlFilePath, std::ios::binary);
    else
        file = std::ifstream(filename, std::ios::binary);

    if (!file)
        return SendErrorResponse(requestId, 404, "Not Found");

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string body = ss.str();

    HTTP_RESPONSE response;
    RtlZeroMemory(&response, sizeof(response));
    response.StatusCode = 200;
    response.pReason = "OK";
    response.ReasonLength = (USHORT)strlen(response.pReason);

    const char *contentType = GetContentType(filename);
    HTTP_KNOWN_HEADER contentTypeHeader;
    RtlZeroMemory(&contentTypeHeader, sizeof(contentTypeHeader));
    contentTypeHeader.pRawValue = contentType;
    contentTypeHeader.RawValueLength = (USHORT)strlen(contentType);
    response.Headers.KnownHeaders[HttpHeaderContentType] = contentTypeHeader;

    HTTP_DATA_CHUNK dataChunk;
    RtlZeroMemory(&dataChunk, sizeof(dataChunk));
    dataChunk.DataChunkType = HttpDataChunkFromMemory;
    dataChunk.FromMemory.pBuffer = (PVOID)body.data();
    dataChunk.FromMemory.BufferLength = (ULONG)body.size();

    response.EntityChunkCount = 1;
    response.pEntityChunks = &dataChunk;

    ULONG bytesSent = 0;
    ULONG result = HttpSendHttpResponse(
        m_hReqQueue, requestId, 0, &response, NULL,
        &bytesSent, NULL, 0, NULL, NULL);

    return result == NO_ERROR;
}

bool WebServer::SendJsonResponse(HTTP_REQUEST_ID requestId, const char *json, ULONG jsonSize)
{
    if (json == nullptr)
    {
        json = "";
        jsonSize = 0;
    }

    HTTP_RESPONSE response = {};
    response.StatusCode = 200;
    response.pReason = "OK";
    response.ReasonLength = static_cast<USHORT>(strlen(response.pReason));

    const char *contentType = "application/json; charset=utf-8";
    response.Headers.KnownHeaders[HttpHeaderContentType].pRawValue = contentType;
    response.Headers.KnownHeaders[HttpHeaderContentType].RawValueLength = static_cast<USHORT>(strlen(contentType));

    HTTP_DATA_CHUNK dataChunk = {};
    dataChunk.DataChunkType = HttpDataChunkFromMemory;
    dataChunk.FromMemory.pBuffer = const_cast<char *>(json);
    dataChunk.FromMemory.BufferLength = jsonSize;

    response.EntityChunkCount = 1;
    response.pEntityChunks = &dataChunk;

    ULONG bytesSent = 0;
    ULONG result = HttpSendHttpResponse(m_hReqQueue, requestId, 0, &response, nullptr, &bytesSent, nullptr, 0, nullptr, nullptr);

    return result == NO_ERROR;
}

bool WebServer::SendErrorResponse(HTTP_REQUEST_ID requestId, USHORT statusCode, const char *reason)
{
    HTTP_RESPONSE response;
    RtlZeroMemory(&response, sizeof(response));
    response.StatusCode = statusCode;
    response.pReason = reason;
    response.ReasonLength = (USHORT)strlen(reason);

    const char *body = "Error";
    HTTP_DATA_CHUNK dataChunk;
    RtlZeroMemory(&dataChunk, sizeof(dataChunk));
    dataChunk.DataChunkType = HttpDataChunkFromMemory;
    dataChunk.FromMemory.pBuffer = (PVOID)body;
    dataChunk.FromMemory.BufferLength = (ULONG)strlen(body);

    response.EntityChunkCount = 1;
    response.pEntityChunks = &dataChunk;

    ULONG bytesSent = 0;
    ULONG result = HttpSendHttpResponse(
        m_hReqQueue, requestId, 0, &response, NULL,
        &bytesSent, NULL, 0, NULL, NULL);

    return result == NO_ERROR;
}

void WebServer::CheckInterface(const std::vector<NetworkInterface> &interfaces)
{
    if (!m_initialized)
    {
        LOG_DEBUG("[WebServer] Not initialized");
        return;
    }

    if (!m_boundInterface)
    {
        LOG_DEBUG("[WebServer] No bound interface");
        return;
    }

    auto it = std::find_if(interfaces.begin(), interfaces.end(), [&](const auto &iface)
                           { return iface.luid.Value == m_boundInterface->luid.Value; });

    if (it == interfaces.end())
    {
        LOG_DEBUG("[WebServer] Bound interface removed: %ls", m_boundInterface->display().c_str());

        // notify UI
        // stop listener
        return;
    }

    if (it->address != m_boundInterface->address)
    {
        LOG_DEBUG("[WebServer] Bound IP changed: %ls -> %ls", m_boundInterface->address.c_str(), it->address.c_str());

        // rebind socket
        // notify clients/UI if needed
        return;
    }

    // No change. Do nothing.
    LOG_DEBUG("[WebServer] No change detected");
}

bool WebServer::LaunchServerOnInterface(const NetworkInterface &netIf)
{
    if (IsRunning())
    {
        LOG_ERROR("[WebServer] Already running!");
        return false;
    }

    std::wstring urlPrefix = L"http://" + netIf.address + L":" + WEBSERVER_PORT + L"/";

    auto html = LoadResourceString(IDR_INDEX_HTML);

    if (!Init(urlPrefix, L"index.html"))
    {
        LOG_ERROR("[WebServer] Init failed for %ls, ERROR_ACCESS_DENIED", std::wstring(netIf.address.begin(), netIf.address.end()).c_str());
        return false;
    }

    SetApiHandler(BuildCombinedJson);

    if (!Start())
    {
        LOG_ERROR("[WebServer] Start failed");
        return false;
    }

    if (IsRunning())
        LOG_DEBUG("[WebServer] Server running on %ls:%ls (%ls)", netIf.address.c_str(), WEBSERVER_PORT, netIf.adapterName.c_str());

    m_boundInterface = netIf;

    return IsRunning();
}