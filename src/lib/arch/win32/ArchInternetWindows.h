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

#pragma once

#define ARCH_INTERNET ArchInternetWindows

#include <string>

class ArchInternetWindows {
public:
    std::string get(const std::string& url);
    std::string urlEncode(const std::string& url);

    //! Select the client certificate used for HTTPS requests.  \p selector is
    //! a thumbprint, a substring of the certificate name, or "auto" for the
    //! first certificate with a private key.
    void setClientCertificate(const std::string& selector) { m_clientCert = selector; }
    //! Not applicable on Windows; certificates live in the OS store.
    void setClientKey(const std::string&) { }
    //! Skip server certificate verification (self-signed endpoints only).
    void setInsecure(bool insecure) { m_insecure = insecure; }

private:
    std::string m_clientCert;
    bool m_insecure = false;
};
