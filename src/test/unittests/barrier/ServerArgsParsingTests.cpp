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

#include "barrier/ArgParser.h"
#include "barrier/ServerArgs.h"
#include "test/mock/barrier/MockArgParser.h"

#include "test/global/gtest.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

bool
server_stubParseGenericArgs(int, const char* const*, int&)
{
    return false;
}

bool
server_stubCheckUnexpectedArgs()
{
    return false;
}

TEST(ServerArgsParsingTests, parseServerArgs_addressArg_setBarrierAddress)
{
    NiceMock<MockArgParser> argParser;
    ON_CALL(argParser, parseGenericArgs(_, _, _)).WillByDefault(Invoke(server_stubParseGenericArgs));
    ON_CALL(argParser, checkUnexpectedArgs()).WillByDefault(Invoke(server_stubCheckUnexpectedArgs));
    ServerArgs serverArgs;
    const int argc = 3;
    const char* kAddressCmd[argc] = { "stub", "--address", "mock_address" };

    argParser.parseServerArgs(serverArgs, argc, kAddressCmd);

    EXPECT_EQ("mock_address", serverArgs.m_barrierAddress);
}

TEST(ServerArgsParsingTests, parseServerArgs_configArg_setConfigFile)
{
    NiceMock<MockArgParser> argParser;
    ON_CALL(argParser, parseGenericArgs(_, _, _)).WillByDefault(Invoke(server_stubParseGenericArgs));
    ON_CALL(argParser, checkUnexpectedArgs()).WillByDefault(Invoke(server_stubCheckUnexpectedArgs));
    ServerArgs serverArgs;
    const int argc = 3;
    const char* kConfigCmd[argc] = { "stub", "--config", "mock_configFile" };

    argParser.parseServerArgs(serverArgs, argc, kConfigCmd);

    EXPECT_EQ("mock_configFile", serverArgs.m_configFile);
}

TEST(ServerArgsParsingTests, parseServerArgs_configUrlArg_setConfigUrl)
{
    NiceMock<MockArgParser> argParser;
    ON_CALL(argParser, parseGenericArgs(_, _, _)).WillByDefault(Invoke(server_stubParseGenericArgs));
    ON_CALL(argParser, checkUnexpectedArgs()).WillByDefault(Invoke(server_stubCheckUnexpectedArgs));
    ServerArgs serverArgs;
    const int argc = 3;
    const char* kConfigUrlCmd[argc] = { "stub", "--config-url", "https://example.com/barrier.conf" };

    argParser.parseServerArgs(serverArgs, argc, kConfigUrlCmd);

    EXPECT_EQ("https://example.com/barrier.conf", serverArgs.m_configUrl);
}

TEST(ServerArgsParsingTests, parseServerArgs_configAndConfigUrl_setBoth)
{
    NiceMock<MockArgParser> argParser;
    ON_CALL(argParser, parseGenericArgs(_, _, _)).WillByDefault(Invoke(server_stubParseGenericArgs));
    ON_CALL(argParser, checkUnexpectedArgs()).WillByDefault(Invoke(server_stubCheckUnexpectedArgs));
    ServerArgs serverArgs;
    const int argc = 5;
    const char* kConfigCmd[argc] = {
        "stub", "--config", "mock_configFile",
        "--config-url", "https://example.com/barrier.conf"
    };

    argParser.parseServerArgs(serverArgs, argc, kConfigCmd);

    EXPECT_EQ("mock_configFile", serverArgs.m_configFile);
    EXPECT_EQ("https://example.com/barrier.conf", serverArgs.m_configUrl);
}

TEST(ServerArgsParsingTests, parseServerArgs_downloadArgs_setBoth)
{
    NiceMock<MockArgParser> argParser;
    ON_CALL(argParser, parseGenericArgs(_, _, _)).WillByDefault(Invoke(server_stubParseGenericArgs));
    ON_CALL(argParser, checkUnexpectedArgs()).WillByDefault(Invoke(server_stubCheckUnexpectedArgs));
    ServerArgs serverArgs;
    const int argc = 5;
    const char* kDownloadCmd[argc] = {
        "stub", "--download-url", "https://example.com/asset.bin",
        "--download-path", "/tmp/asset.bin"
    };

    bool result = argParser.parseServerArgs(serverArgs, argc, kDownloadCmd);

    EXPECT_TRUE(result);
    EXPECT_EQ("https://example.com/asset.bin", serverArgs.m_downloadUrl);
    EXPECT_EQ("/tmp/asset.bin", serverArgs.m_downloadPath);
}

TEST(ServerArgsParsingTests, parseServerArgs_downloadUrlWithoutPath_returnFalse)
{
    NiceMock<MockArgParser> argParser;
    ON_CALL(argParser, parseGenericArgs(_, _, _)).WillByDefault(Invoke(server_stubParseGenericArgs));
    ON_CALL(argParser, checkUnexpectedArgs()).WillByDefault(Invoke(server_stubCheckUnexpectedArgs));
    ServerArgs serverArgs;
    const int argc = 3;
    const char* kDownloadCmd[argc] = { "stub", "--download-url", "https://example.com/asset.bin" };

    bool result = argParser.parseServerArgs(serverArgs, argc, kDownloadCmd);

    EXPECT_FALSE(result);
}

TEST(ServerArgsParsingTests, parseServerArgs_downloadPathWithoutUrl_returnFalse)
{
    NiceMock<MockArgParser> argParser;
    ON_CALL(argParser, parseGenericArgs(_, _, _)).WillByDefault(Invoke(server_stubParseGenericArgs));
    ON_CALL(argParser, checkUnexpectedArgs()).WillByDefault(Invoke(server_stubCheckUnexpectedArgs));
    ServerArgs serverArgs;
    const int argc = 3;
    const char* kDownloadCmd[argc] = { "stub", "--download-path", "/tmp/asset.bin" };

    bool result = argParser.parseServerArgs(serverArgs, argc, kDownloadCmd);

    EXPECT_FALSE(result);
}

TEST(ServerArgsParsingTests, parseServerArgs_httpClientCertArgs_setValues)
{
    NiceMock<MockArgParser> argParser;
    ON_CALL(argParser, parseGenericArgs(_, _, _)).WillByDefault(Invoke(server_stubParseGenericArgs));
    ON_CALL(argParser, checkUnexpectedArgs()).WillByDefault(Invoke(server_stubCheckUnexpectedArgs));
    ServerArgs serverArgs;
    const int argc = 5;
    const char* kHttpCmd[argc] = {
        "stub", "--http-client-cert", "auto",
        "--http-client-key", "/tmp/client.key"
    };

    bool result = argParser.parseServerArgs(serverArgs, argc, kHttpCmd);

    EXPECT_TRUE(result);
    EXPECT_EQ("auto", serverArgs.m_httpClientCert);
    EXPECT_EQ("/tmp/client.key", serverArgs.m_httpClientKey);
}

TEST(ServerArgsParsingTests, parseServerArgs_httpInsecure_setsFlag)
{
    NiceMock<MockArgParser> argParser;
    ON_CALL(argParser, parseGenericArgs(_, _, _)).WillByDefault(Invoke(server_stubParseGenericArgs));
    ON_CALL(argParser, checkUnexpectedArgs()).WillByDefault(Invoke(server_stubCheckUnexpectedArgs));
    ServerArgs serverArgs;
    const int argc = 2;
    const char* kHttpCmd[argc] = { "stub", "--http-insecure" };

    bool result = argParser.parseServerArgs(serverArgs, argc, kHttpCmd);

    EXPECT_TRUE(result);
    EXPECT_TRUE(serverArgs.m_httpInsecure);
}
