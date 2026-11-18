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

#include "barrier/ArgsBase.h"

class NetworkAddress;
class Config;

class ServerArgs : public ArgsBase {
public:
    ServerArgs();

public:
    String                m_configFile;
    //! URL to fetch the configuration from, if non-empty.
    String                m_configUrl;
    //! URL of an auxiliary file to download when the configuration is
    //! reloaded, if non-empty.
    String                m_downloadUrl;
    //! Destination path for the downloaded auxiliary file.
    String                m_downloadPath;
    //! Client certificate for HTTPS fetches.  On Windows this is a thumbprint,
    //! a name substring or "auto"; elsewhere it is a certificate file path.
    String                m_httpClientCert;
    //! Private key file for the client certificate (non-Windows only).
    String                m_httpClientKey;
    //! Skip server certificate verification (self-signed endpoints only).
    bool                  m_httpInsecure = false;
    Config*                m_config;
    String                m_screenChangeScript;
    bool check_client_certificates = true;
    //! Port for the external command channel, bound on 127.0.0.1.
    int m_externalCommandPort = 8080;
};
