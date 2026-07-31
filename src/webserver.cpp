#include "radeonmon/webserver.hpp"

#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <filesystem>
#include <iphlpapi.h>
#include <ws2tcpip.h>

#include "radeonmon/resource_ids.h"
#include "radeonmon/structures.hpp"
#include "radeonmon/helpers.hpp"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")

WebServer::WebServer() : m_hReqQueue(NULL), m_running(false), m_initialized(false) {}

WebServer::~WebServer()
{
    Stop();

    if (m_initialized)
        HttpTerminate(HTTP_INITIALIZE_SERVER, nullptr);
}

bool WebServer::Init(const std::wstring &netInterfaceAddress, const std::wstring &htmlFilePath)
{
    // required for winsock functions
    WSADATA wsaData;
    int r = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (r != 0)
        LOG_DEBUG("[WebServer] WSAStartup failed: %d", r);

    std::wstring urlPrefix = L"https://" + netInterfaceAddress + L":" + WEBSERVER_PORT + L"/";

    m_urlPrefix = urlPrefix;
    m_htmlFilePath = htmlFilePath;

    ULONG result = NO_ERROR;
    HTTP_BINDING_INFO bindingInfo = {};

    result = HttpInitialize(HTTPAPI_VERSION_2, HTTP_INITIALIZE_SERVER | HTTP_INITIALIZE_CONFIG, nullptr);

    if (result != NO_ERROR)
        goto fail;

    // bind the SSL cert
    if (m_urlPrefix.rfind(L"https://", 0) == 0)
    {
        auto cert = GetOrCreateSelfSignedCert(netInterfaceAddress);
        if (cert)
        {
            if (!BindSslCert(netInterfaceAddress, WEBSERVER_PORT_NUM, cert))
                LOG_DEBUG("[WebServer] BindSslCert failed, continuing without HTTPS bind");
            CertFreeCertificateContext(cert);
        }
    }

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
        // LOG_DEBUG("API call");
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
    // Get current exe path to be able
    static const std::filesystem::path s_basePath = []()
    {
        wchar_t modulePath[MAX_PATH];
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        return std::filesystem::path(modulePath).parent_path();
    }();

    if (route == L"/styles.css")
    {
        SendFileResponse(pRequest->RequestId, (s_basePath / L"styles.css").wstring());
        return;
    }

    if (route == L"/script.js")
    {
        SendFileResponse(pRequest->RequestId, (s_basePath / L"script.js").wstring());
        return;
    }

    if (route == L"/")
    {
        if (g_currentWebTemplate == IDM_WEBSERVER_TEMPLATE_LIGHT)
        {
            SendFileResponse(pRequest->RequestId, (s_basePath / L"index_light.html").wstring());
        }
        else
        {
            SendFileResponse(pRequest->RequestId, (s_basePath / L"index.html").wstring());
        }
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
#ifdef _DEBUG
        LOG_DEBUG("[WebServer] [%s] GET %ls", GetPeerIp(pRequest).c_str(), route.c_str());
#endif
        if (!SendResourceResponse(pRequest->RequestId, g_currentWebTemplate == IDM_WEBSERVER_TEMPLATE_LIGHT ? IDR_INDEX2_HTML : IDR_INDEX_HTML))
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

    LOG_DEBUG("[WebServer] Serving %ls", filename.c_str());

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

    // auto cert = GetOrCreateSelfSignedCert(netIf.address);
    // if (cert)
    // {
    //     BindSslCert(netIf.address, WEBSERVER_PORT_NUM, cert);
    //     CertFreeCertificateContext(cert);
    // }

    // std::wstring urlPrefix = L"https://" + netIf.address + L":" + WEBSERVER_PORT + L"/";
    // std::wstring urlPrefix = L"http://" + netIf.address + L":" + WEBSERVER_PORT + L"/";

    auto html = LoadResourceString(IDR_INDEX_HTML);

    if (!Init(netIf.address, L"index.html"))
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

// Returns a cert context with a private key, creating one in the machine
// store if it doesn't already exist. Caller must CertFreeCertificateContext.
PCCERT_CONTEXT WebServer::GetOrCreateSelfSignedCert(const std::wstring &subjectCn)
{
    HCERTSTORE hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"MY");
    if (!hStore)
        return nullptr;

    // Look for an existing cert we made previously (tag it via a friendly name)
    PCCERT_CONTEXT existing = CertFindCertificateInStore(hStore, X509_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR, subjectCn.c_str(), nullptr);
    if (existing)
    {
        CertCloseStore(hStore, 0);
        return existing;
    }

    // Build subject name "CN=<subjectCn>"
    std::wstring subjectStr = L"CN=" + subjectCn;
    DWORD encodedLen = 0;
    CertStrToNameW(X509_ASN_ENCODING, subjectStr.c_str(), CERT_X500_NAME_STR, nullptr, nullptr, &encodedLen, nullptr);
    std::vector<BYTE> encodedName(encodedLen);
    CertStrToNameW(X509_ASN_ENCODING, subjectStr.c_str(), CERT_X500_NAME_STR, nullptr, encodedName.data(), &encodedLen, nullptr);

    CERT_NAME_BLOB subjectBlob = {(DWORD)encodedName.size(), encodedName.data()};

    // Create a CNG key pair for the cert
    NCRYPT_PROV_HANDLE hProvider = 0;
    NCryptOpenStorageProvider(&hProvider, MS_KEY_STORAGE_PROVIDER, 0);

    NCRYPT_KEY_HANDLE hKey = 0;
    std::wstring keyName = L"WebServerTlsKey_" + subjectCn;
    NCryptCreatePersistedKey(hProvider, &hKey, NCRYPT_RSA_ALGORITHM, keyName.c_str(), 0, NCRYPT_MACHINE_KEY_FLAG);

    DWORD keyLen = 2048;
    NCryptSetProperty(hKey, NCRYPT_LENGTH_PROPERTY, (PBYTE)&keyLen, sizeof(keyLen), 0);
    NCryptFinalizeKey(hKey, 0);

    CRYPT_KEY_PROV_INFO keyProvInfo = {};
    keyProvInfo.pwszContainerName = (LPWSTR)keyName.c_str();
    keyProvInfo.pwszProvName = (LPWSTR)MS_KEY_STORAGE_PROVIDER;
    keyProvInfo.dwFlags = NCRYPT_MACHINE_KEY_FLAG;
    keyProvInfo.dwKeySpec = AT_KEYEXCHANGE;

    SYSTEMTIME startTime, endTime;
    GetSystemTime(&startTime);
    GetSystemTime(&endTime);
    endTime.wYear += 5; // 5-year validity

    CRYPT_ALGORITHM_IDENTIFIER sigAlg = {};
    sigAlg.pszObjId = (LPSTR)szOID_RSA_SHA256RSA;

    PCCERT_CONTEXT cert = CertCreateSelfSignCertificate(hKey, &subjectBlob, 0, &keyProvInfo, &sigAlg, &startTime, &endTime, nullptr);

    // Add IP/DNS SANs (loopback + LAN) so the browser accepts the name match
    // (build via CertExtensions / szOID_SUBJECT_ALT_NAME2 — see note below)

    if (cert)
    {
        CertAddCertificateContextToStore(hStore, cert, CERT_STORE_ADD_REPLACE_EXISTING, nullptr);
        // Mark it exportable/trusted for the machine, and optionally add to
        // "Trusted Root" store too, so the phone... no, that's for THIS PC only.
    }

    NCryptFreeObject(hKey);
    NCryptFreeObject(hProvider);
    CertCloseStore(hStore, 0);
    return cert;
}

bool WebServer::BindSslCert(const std::wstring &ipAddress, USHORT port, PCCERT_CONTEXT cert)
{
    LOG_DEBUG("[WebServer] BindSslCert: binding cert to %ls:%d", ipAddress.c_str(), port);

    SOCKADDR_IN addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    int ptonResult = InetPtonW(AF_INET, ipAddress.c_str(), &addr.sin_addr);
    if (ptonResult != 1)
    {
        LOG_DEBUG("[WebServer] BindSslCert: InetPtonW failed for '%ls', result=%d, WSAError=%d",
                  ipAddress.c_str(), ptonResult, WSAGetLastError());
        return false;
    }

    HTTP_SERVICE_CONFIG_SSL_SET sslSet = {};
    sslSet.KeyDesc.pIpPort = (PSOCKADDR)&addr;

    BYTE hash[20];
    DWORD hashLen = sizeof(hash);
    if (!CertGetCertificateContextProperty(cert, CERT_HASH_PROP_ID, hash, &hashLen))
    {
        LOG_DEBUG("[WebServer] BindSslCert: CertGetCertificateContextProperty failed, GetLastError=%lu", GetLastError());
        return false;
    }

    LOG_DEBUG("[WebServer] BindSslCert: cert hash length=%lu, hash=%02X%02X%02X%02X...",
              hashLen, hash[0], hash[1], hash[2], hash[3]);

    sslSet.ParamDesc.pSslHash = hash;
    sslSet.ParamDesc.SslHashLength = hashLen;

    GUID appId = {0x11111111, 0x2222, 0x3333, {0x44, 0x44, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55}};
    sslSet.ParamDesc.AppId = appId;
    sslSet.ParamDesc.pSslCertStoreName = (PWSTR)L"MY";

    ULONG result = HttpSetServiceConfiguration(nullptr, HttpServiceConfigSSLCertInfo,
                                               &sslSet, sizeof(sslSet), nullptr);

    LOG_DEBUG("[WebServer] BindSslCert: HttpSetServiceConfiguration result=%lu", result);

    if (result == ERROR_ALREADY_EXISTS)
    {
        LOG_DEBUG("[WebServer] BindSslCert: binding already exists, deleting and retrying");

        ULONG delResult = HttpDeleteServiceConfiguration(nullptr, HttpServiceConfigSSLCertInfo,
                                                         &sslSet, sizeof(sslSet), nullptr);
        LOG_DEBUG("[WebServer] BindSslCert: HttpDeleteServiceConfiguration result=%lu", delResult);

        result = HttpSetServiceConfiguration(nullptr, HttpServiceConfigSSLCertInfo,
                                             &sslSet, sizeof(sslSet), nullptr);
        LOG_DEBUG("[WebServer] BindSslCert: retry HttpSetServiceConfiguration result=%lu", result);
    }

    if (result != NO_ERROR)
    {
        LOG_DEBUG("[WebServer] BindSslCert: FAILED, final result=%lu", result);
        return false;
    }

    LOG_DEBUG("[WebServer] BindSslCert: succeeded");
    return true;
}