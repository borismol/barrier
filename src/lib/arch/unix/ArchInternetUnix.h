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

#define ARCH_INTERNET ArchInternetUnix

#include <string>

class ArchInternetUnix {
public:
    std::string get(const std::string& url);
    std::string urlEncode(const std::string& url);

    //! Path to a PEM/PFX client certificate used for HTTPS requests.
    void setClientCertificate(const std::string& path) { m_clientCert = path; }
    //! Path to the private key for the client certificate, if separate.
    void setClientKey(const std::string& path) { m_clientKey = path; }
    //! Skip server certificate verification (self-signed endpoints only).
    void setInsecure(bool insecure) { m_insecure = insecure; }

private:
    std::string m_clientCert;
    std::string m_clientKey;
    bool m_insecure = false;
};
