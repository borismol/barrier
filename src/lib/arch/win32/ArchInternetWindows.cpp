/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2014-2016 Symless Ltd.
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "arch/win32/ArchInternetWindows.h"
#include "arch/win32/XArchWindows.h"
#include "arch/Arch.h"
#include "base/String.h"
#include "common/Version.h"

#include <sstream>
#include <vector>
#include <Wininet.h>
#include <Wincrypt.h>
#include <Shlwapi.h>

//
// helpers
//

// Looks a message up in a module's message table.  Returns an empty string if
// the module doesn't have a message for the code.
static std::string formatMessageFromModule(HMODULE module, DWORD error)
{
    char* buffer = NULL;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                  FORMAT_MESSAGE_IGNORE_INSERTS |
                  (module != NULL ? FORMAT_MESSAGE_FROM_HMODULE
                                  : FORMAT_MESSAGE_FROM_SYSTEM);
    DWORD length = FormatMessageA(flags, module, error,
                                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                  (LPSTR)&buffer, 0, NULL);
    if (length == 0 || buffer == NULL) {
        if (buffer != NULL) {
            LocalFree(buffer);
        }
        return std::string();
    }

    std::string message(buffer, length);
    LocalFree(buffer);
    while (!message.empty() &&
           (message.back() == '\r' || message.back() == '\n' ||
            message.back() == ' ')) {
        message.pop_back();
    }
    return message;
}

// WinINet error codes (12000+) are not part of the system message table, so
// they have to be looked up in wininet.dll.
static std::string wininetErrorString(DWORD error)
{
    HMODULE module = LoadLibraryA("wininet.dll");
    std::string message;
    if (module != NULL) {
        message = formatMessageFromModule(module, error);
        FreeLibrary(module);
    }
    if (message.empty()) {
        message = formatMessageFromModule(NULL, error);
    }
    if (message.empty()) {
        message = barrier::string::sprintf("error %u", (unsigned int)error);
    }
    return message;
}

// The error code must be captured before any other call, since the string
// lookup itself can clobber GetLastError().
static void throwHttpError(const std::string& message)
{
    DWORD error = GetLastError();
    throw XArch(message + ": " + wininetErrorString(error));
}

struct ParsedUrl {
    std::string host;
    std::string path;
    INTERNET_PORT port;
    DWORD flags;
};

// Parses the URL into the pieces InternetConnect()/HttpOpenRequest() need.
// InternetCrackUrl() handles explicit ports, IPv6 literals, queries and URLs
// without a path.
static ParsedUrl parseUrl(const std::string& url)
{
    URL_COMPONENTSA components;
    ZeroMemory(&components, sizeof(components));
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = (DWORD)-1;
    components.dwHostNameLength = (DWORD)-1;
    components.dwUrlPathLength = (DWORD)-1;
    components.dwExtraInfoLength = (DWORD)-1;

    if (!InternetCrackUrlA(url.c_str(), (DWORD)url.size(), 0, &components)) {
        throw XArch("invalid URL \"" + url + "\"");
    }

    if (components.nScheme != INTERNET_SCHEME_HTTP &&
        components.nScheme != INTERNET_SCHEME_HTTPS) {
        throw XArch("unsupported URL scheme in \"" + url + "\"");
    }

    ParsedUrl parsed;
    parsed.port = components.nPort;
    parsed.flags = 0;

    if (components.dwHostNameLength > 0 && components.lpszHostName != NULL) {
        parsed.host.assign(components.lpszHostName,
                           components.dwHostNameLength);
    }
    if (parsed.host.empty()) {
        throw XArch("no host name in URL \"" + url + "\"");
    }

    std::string path;
    if (components.dwUrlPathLength > 0 && components.lpszUrlPath != NULL) {
        path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    }
    else {
        path = "/";
    }
    if (components.dwExtraInfoLength > 0 && components.lpszExtraInfo != NULL) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    parsed.path = path;

    if (components.nScheme == INTERNET_SCHEME_HTTPS) {
        parsed.flags = INTERNET_FLAG_SECURE;
    }

    return parsed;
}

static std::string toLowerAscii(std::string value)
{
    for (std::string::size_type i = 0; i < value.size(); ++i) {
        if (value[i] >= 'A' && value[i] <= 'Z') {
            value[i] = (char)(value[i] - 'A' + 'a');
        }
    }
    return value;
}

// Returns the upper-case hex thumbprint, or an empty string if the value
// doesn't look like one.
static std::string normalizeThumbprint(const std::string& value)
{
    std::string result;
    for (std::string::size_type i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c == ' ' || c == '\t' || c == ':') {
            continue;
        }
        if (c >= 'a' && c <= 'f') {
            c = (char)(c - 'a' + 'A');
        }
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) {
            result.push_back(c);
        }
        else {
            return std::string();
        }
    }
    return result;
}

static std::string certificateThumbprint(PCCERT_CONTEXT cert)
{
    DWORD size = 0;
    if (!CertGetCertificateContextProperty(cert, CERT_HASH_PROP_ID, NULL,
                                           &size)) {
        return std::string();
    }
    std::vector<BYTE> hash(size);
    if (size == 0 ||
        !CertGetCertificateContextProperty(cert, CERT_HASH_PROP_ID, &hash[0],
                                           &size)) {
        return std::string();
    }

    static const char* digits = "0123456789ABCDEF";
    std::string result;
    result.reserve(size * 2);
    for (DWORD i = 0; i < size; ++i) {
        result.push_back(digits[hash[i] >> 4]);
        result.push_back(digits[hash[i] & 0x0F]);
    }
    return result;
}

static std::string certificateName(PCCERT_CONTEXT cert, DWORD nameType)
{
    DWORD size = CertGetNameStringA(cert, nameType, 0, NULL, NULL, 0);
    if (size <= 1) {
        return std::string();
    }
    std::vector<char> buffer(size);
    CertGetNameStringA(cert, nameType, 0, NULL, &buffer[0], size);
    return std::string(&buffer[0]);
}

static bool certificateHasPrivateKey(PCCERT_CONTEXT cert)
{
    DWORD size = 0;
    return CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID,
                                             NULL, &size) != FALSE;
}

static PCCERT_CONTEXT findClientCertificateInStore(DWORD location,
                                                   const std::string& selector)
{
    HCERTSTORE store = CertOpenStore(
        CERT_STORE_PROV_SYSTEM_A, 0, NULL,
        location | CERT_STORE_READONLY_FLAG, "MY");
    if (store == NULL) {
        return NULL;
    }

    std::string wantedThumbprint = normalizeThumbprint(selector);
    bool byThumbprint = wantedThumbprint.size() >= 16;
    bool any = (selector == "auto" || selector == "*");
    std::string wantedName = toLowerAscii(selector);

    PCCERT_CONTEXT found = NULL;
    PCCERT_CONTEXT cert = NULL;
    while ((cert = CertEnumCertificatesInStore(store, cert)) != NULL) {
        if (!certificateHasPrivateKey(cert)) {
            continue;
        }

        bool match = false;
        if (byThumbprint) {
            match = (certificateThumbprint(cert) == wantedThumbprint);
        }
        else if (any) {
            match = true;
        }
        else {
            std::string simple = toLowerAscii(
                certificateName(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE));
            std::string full = toLowerAscii(
                certificateName(cert, CERT_NAME_FULL_DISPLAY_TYPE));
            match = simple.find(wantedName) != std::string::npos ||
                    full.find(wantedName) != std::string::npos;
        }

        if (match) {
            found = CertDuplicateCertificateContext(cert);
            break;
        }
    }

    if (cert != NULL) {
        CertFreeCertificateContext(cert);
    }
    CertCloseStore(store, 0);
    return found;
}

static PCCERT_CONTEXT findClientCertificate(const std::string& selector)
{
    PCCERT_CONTEXT cert = findClientCertificateInStore(
        CERT_SYSTEM_STORE_CURRENT_USER, selector);
    if (cert == NULL) {
        // a server started as a service may only see the machine store
        cert = findClientCertificateInStore(CERT_SYSTEM_STORE_LOCAL_MACHINE,
                                            selector);
    }
    return cert;
}

class WinINetRequest {
public:
    WinINetRequest(const std::string& url, const std::string& clientCert,
                   bool insecure);
    ~WinINetRequest();

    std::string send();

private:
    void openSession();
    void connect();
    void openRequest();
    void attachClientCertificate();
    void checkStatus();

    HINTERNET m_session;
    HINTERNET m_connect;
    HINTERNET m_request;
    ParsedUrl m_url;
    std::string m_clientCert;
    bool m_insecure;
    bool m_used;
    PCCERT_CONTEXT m_cert;
};

//
// ArchInternetWindows
//

std::string ArchInternetWindows::get(const std::string& url)
{
    WinINetRequest request(url, m_clientCert, m_insecure);
    return request.send();
}

std::string ArchInternetWindows::urlEncode(const std::string& url)
{
    TCHAR buffer[1024];
    DWORD bufferSize = sizeof(buffer);

    if (UrlEscape(url.c_str(), buffer, &bufferSize, URL_ESCAPE_UNSAFE) != S_OK) {
        throw XArch(new XArchEvalWindows());
    }

    std::string result(buffer);

    // the win32 url encoding functions are pretty useless (to us) and only
    // escape "unsafe" chars, but not + or =, so we need to replace these
    // manually (and probably many other chars).
    barrier::string::findReplaceAll(result, "+", "%2B");
    barrier::string::findReplaceAll(result, "=", "%3D");

    return result;
}

//
// WinINetRequest
//

WinINetRequest::WinINetRequest(const std::string& url,
                               const std::string& clientCert, bool insecure) :
    m_session(NULL),
    m_connect(NULL),
    m_request(NULL),
    m_url(parseUrl(url)),
    m_clientCert(clientCert),
    m_insecure(insecure),
    m_used(false),
    m_cert(NULL)
{
}

WinINetRequest::~WinINetRequest()
{
    if (m_request != NULL) {
        InternetCloseHandle(m_request);
    }

    if (m_connect != NULL) {
        InternetCloseHandle(m_connect);
    }

    if (m_session != NULL) {
        InternetCloseHandle(m_session);
    }

    if (m_cert != NULL) {
        CertFreeCertificateContext(m_cert);
    }
}

std::string WinINetRequest::send()
{
    if (m_used) {
        throw XArch("class is one time use.");
    }
    m_used = true;

    openSession();
    connect();
    openRequest();

    std::string headers("Content-Type: text/html\r\n");
    if (!HttpSendRequestA(m_request, headers.c_str(), (DWORD)headers.size(),
                          NULL, 0)) {
        throwHttpError("HTTP request failed");
    }

    checkStatus();

    std::stringstream result;
    CHAR buffer[1025];
    DWORD read = 0;

    while (InternetReadFile(m_request, buffer, sizeof(buffer), &read) &&
           read != 0) {
        // write() rather than operator<< so NUL bytes in binary downloads
        // are preserved.
        result.write(buffer, read);
        read = 0;
    }

    return result.str();
}

void WinINetRequest::openSession()
{
    std::stringstream userAgent;
    userAgent << "Barrier ";
    userAgent << kVersion;

    m_session = InternetOpenA(userAgent.str().c_str(),
                              INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);

    if (m_session == NULL) {
        throwHttpError("cannot open internet session");
    }
}

void WinINetRequest::connect()
{
    m_connect = InternetConnectA(m_session, m_url.host.c_str(), m_url.port,
                                 NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);

    if (m_connect == NULL) {
        throwHttpError("cannot connect to " + m_url.host);
    }
}

void WinINetRequest::openRequest()
{
    DWORD flags = m_url.flags | INTERNET_FLAG_RELOAD |
                  INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI |
                  INTERNET_FLAG_KEEP_CONNECTION;

    m_request = HttpOpenRequestA(m_connect, "GET", m_url.path.c_str(), NULL,
                                 NULL, NULL, flags, 0);

    if (m_request == NULL) {
        throwHttpError("cannot create HTTP request");
    }

    if (m_insecure) {
        DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                              SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                              SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                              SECURITY_FLAG_IGNORE_WRONG_USAGE;
        InternetSetOptionA(m_request, INTERNET_OPTION_SECURITY_FLAGS,
                           &securityFlags, sizeof(securityFlags));
    }

    attachClientCertificate();
}

void WinINetRequest::attachClientCertificate()
{
    if (m_clientCert.empty()) {
        return;
    }

    m_cert = findClientCertificate(m_clientCert);
    if (m_cert == NULL) {
        throw XArch("client certificate not found: " + m_clientCert);
    }

    // INTERNET_OPTION_CLIENT_CERT_CONTEXT takes the CERT_CONTEXT itself (not
    // a pointer to it) and its size.
    if (!InternetSetOptionA(m_request, INTERNET_OPTION_CLIENT_CERT_CONTEXT,
                            (LPVOID)m_cert, sizeof(CERT_CONTEXT))) {
        throwHttpError("cannot select client certificate");
    }
}

void WinINetRequest::checkStatus()
{
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!HttpQueryInfoA(m_request,
                        HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                        &status, &size, NULL)) {
        throwHttpError("cannot read HTTP status");
    }

    if (status >= 400) {
        throw XArch(barrier::string::sprintf(
            "HTTP request failed with status %u", (unsigned int)status));
    }
}
