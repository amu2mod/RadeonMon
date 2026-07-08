#pragma once

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace VersionChecker
{
    namespace detail
    {
        // Splits a full URL into host, path, and port/scheme info usable by WinHttpCrackUrl.
        inline bool CrackUrl(const std::wstring &url, URL_COMPONENTS &urlComp,
                             std::wstring &host, std::wstring &path)
        {
            wchar_t hostBuf[256] = {0};
            wchar_t pathBuf[2048] = {0};

            ZeroMemory(&urlComp, sizeof(urlComp));
            urlComp.dwStructSize = sizeof(urlComp);
            urlComp.lpszHostName = hostBuf;
            urlComp.dwHostNameLength = ARRAYSIZE(hostBuf);
            urlComp.lpszUrlPath = pathBuf;
            urlComp.dwUrlPathLength = ARRAYSIZE(pathBuf);

            if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.length()), 0, &urlComp))
            {
                LOG_ERROR("VersionChecker: WinHttpCrackUrl failed (err=%lu)", GetLastError());
                return false;
            }

            host = hostBuf;
            path = pathBuf;
            return true;
        }

        // Converts a UTF-8 (or ASCII-safe) byte buffer, as returned by WinHttpReadData,
        // into a std::wstring using the Win32 conversion API.
        inline std::wstring Utf8ToWide(const std::string &utf8)
        {
            if (utf8.empty())
            {
                return std::wstring();
            }

            int required = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            if (required <= 0)
            {
                return std::wstring();
            }

            std::wstring wide(required, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &wide[0], required);
            return wide;
        }

        // Extracts the value of "tag_name": "<value>" from a raw wide JSON string.
        // Lightweight manual parse (no external JSON dependency needed for this single field).
        inline bool ExtractTagName(const std::wstring &json, std::wstring &outVersion)
        {
            const std::wstring key = L"\"tag_name\"";
            size_t keyPos = json.find(key);
            if (keyPos == std::wstring::npos)
            {
                LOG_ERROR("VersionChecker: 'tag_name' key not found in JSON response");
                return false;
            }

            size_t colonPos = json.find(L':', keyPos + key.length());
            if (colonPos == std::wstring::npos)
            {
                LOG_ERROR("VersionChecker: malformed JSON near 'tag_name'");
                return false;
            }

            size_t firstQuote = json.find(L'"', colonPos + 1);
            if (firstQuote == std::wstring::npos)
            {
                LOG_ERROR("VersionChecker: could not find opening quote for tag_name value");
                return false;
            }

            size_t secondQuote = json.find(L'"', firstQuote + 1);
            if (secondQuote == std::wstring::npos)
            {
                LOG_ERROR("VersionChecker: could not find closing quote for tag_name value");
                return false;
            }

            outVersion = json.substr(firstQuote + 1, secondQuote - firstQuote - 1);
            return true;
        }
    } // namespace detail

    // Performs the HTTPS GET request against REPOURL and returns the response body as a wide string.
    // Returns true on HTTP 200 with a non-empty body, false otherwise.
    // outHttpStatus receives the HTTP status code (0 if the request never completed).
    inline bool FetchJson(std::wstring &outBody, DWORD &outHttpStatus)
    {
        outBody.clear();
        outHttpStatus = 0;

        if (REPOURL == nullptr)
        {
            LOG_ERROR("VersionChecker: REPOURL is null");
            return false;
        }

        std::wstring url(REPOURL);
        URL_COMPONENTS urlComp;
        std::wstring host, path;

        if (!detail::CrackUrl(url, urlComp, host, path))
        {
            return false;
        }

        bool useHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
        INTERNET_PORT port = urlComp.nPort;

        HINTERNET hSession = nullptr, hConnect = nullptr, hRequest = nullptr;
        bool success = false;

        hSession = WinHttpOpen(L"VersionChecker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession)
        {
            LOG_ERROR("VersionChecker: WinHttpOpen failed (err=%lu)", GetLastError());
            return false;
        }

        do
        {
            hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
            if (!hConnect)
            {
                LOG_ERROR("VersionChecker: WinHttpConnect failed (err=%lu)", GetLastError());
                break;
            }

            DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
            hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                          nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
            if (!hRequest)
            {
                LOG_ERROR("VersionChecker: WinHttpOpenRequest failed (err=%lu)", GetLastError());
                break;
            }

            // Some APIs (e.g. GitHub) require a User-Agent header or they reject the request.
            const wchar_t *extraHeaders = L"User-Agent: VersionChecker/1.0\r\nAccept: application/json\r\n";
            BOOL sent = WinHttpSendRequest(hRequest, extraHeaders, static_cast<DWORD>(-1),
                                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
            if (!sent)
            {
                LOG_ERROR("VersionChecker: WinHttpSendRequest failed (err=%lu)", GetLastError());
                break;
            }

            if (!WinHttpReceiveResponse(hRequest, nullptr))
            {
                LOG_ERROR("VersionChecker: WinHttpReceiveResponse failed (err=%lu)", GetLastError());
                break;
            }

            // Retrieve HTTP status code.
            DWORD statusCode = 0;
            DWORD statusSize = sizeof(statusCode);
            if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX))
            {
                LOG_ERROR("VersionChecker: WinHttpQueryHeaders failed (err=%lu)", GetLastError());
                break;
            }

            outHttpStatus = statusCode;

            if (statusCode == 404)
            {
                LOG_ERROR("VersionChecker: HTTP 404 - resource not found at REPOURL");
                break;
            }
            else if (statusCode != 200)
            {
                LOG_ERROR("VersionChecker: unexpected HTTP status %lu", statusCode);
                break;
            }

            // Read the response body (raw bytes are UTF-8/ASCII JSON text).
            std::string rawBody;
            DWORD bytesAvailable = 0;
            do
            {
                bytesAvailable = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable))
                {
                    LOG_ERROR("VersionChecker: WinHttpQueryDataAvailable failed (err=%lu)", GetLastError());
                    break;
                }

                if (bytesAvailable == 0)
                {
                    break;
                }

                std::vector<char> buffer(bytesAvailable + 1, 0);
                DWORD bytesRead = 0;
                if (!WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead))
                {
                    LOG_ERROR("VersionChecker: WinHttpReadData failed (err=%lu)", GetLastError());
                    break;
                }

                rawBody.append(buffer.data(), bytesRead);

            } while (bytesAvailable > 0);

            if (rawBody.empty())
            {
                LOG_ERROR("VersionChecker: received empty response body (HTTP 200)");
                break;
            }

            outBody = detail::Utf8ToWide(rawBody);
            success = true;
            LOG_DEBUG("VersionChecker: fetched %zu bytes from REPOURL (HTTP 200)", rawBody.size());

        } while (false);

        if (hRequest)
            WinHttpCloseHandle(hRequest);
        if (hConnect)
            WinHttpCloseHandle(hConnect);
        if (hSession)
            WinHttpCloseHandle(hSession);

        return success;
    }

    // High-level helper: fetches REPOURL and extracts "tag_name" into outVersion.
    // Returns true only if the HTTP request succeeded (200) AND tag_name was parsed.
    inline bool GetLatestVersion(std::wstring &outVersion)
    {
        std::wstring body;
        DWORD httpStatus = 0;

        if (!FetchJson(body, httpStatus))
        {
            LOG_ERROR("VersionChecker: GetLatestVersion failed (httpStatus=%lu)", httpStatus);
            return false;
        }

        if (!detail::ExtractTagName(body, outVersion))
        {
            return false;
        }

        LOG_DEBUG("VersionChecker: latest tag_name = %ls", outVersion.c_str());
        return true;
    }

}