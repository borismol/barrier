/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
 * Copyright (C) 2002 Chris Schoeneman
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

#include "barrier/ServerApp.h"

#include "server/Server.h"
#include "server/ClientListener.h"
#include "server/ClientProxy.h"
#include "server/PrimaryClient.h"
#include "barrier/ArgParser.h"
#include "barrier/Screen.h"
#include "barrier/XScreen.h"
#include "barrier/ServerTaskBarReceiver.h"
#include "barrier/ServerArgs.h"
#include "net/SocketMultiplexer.h"
#include "net/TCPSocketFactory.h"
#include "net/XSocket.h"
#include "arch/Arch.h"
#include "arch/XArch.h"
#include "base/EventQueue.h"
#include "base/log_outputters.h"
#include "base/FunctionEventJob.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "base/TMethodEventJob.h"
#include "common/Version.h"
#include "common/DataDirectories.h"
#include "io/filesystem.h"

#if SYSAPI_WIN32
#include "arch/win32/ArchMiscWindows.h"
#endif

#if WINAPI_MSWINDOWS
#include "platform/MSWindowsScreen.h"
#elif WINAPI_XWINDOWS
#include <unistd.h>
#include <signal.h>
#include "platform/XWindowsScreen.h"
#elif WINAPI_CARBON
#include "platform/OSXScreen.h"
#endif

#if defined(__APPLE__)
#include "platform/OSXDragSimulator.h"
#endif

#include <iostream>
#include <stdio.h>
#include <fstream>
#include <sstream>
#include <system_error>

//
// ServerApp
//

ServerApp::ServerApp(IEventQueue* events, CreateTaskBarReceiverFunc createTaskBarReceiver) :
    App(events, createTaskBarReceiver, new ServerArgs()),
    m_server(NULL),
    m_serverState(kUninitialized),
    m_serverScreen(NULL),
    m_primaryClient(NULL),
    m_listener(NULL),
    m_timer(NULL),
    m_barrierAddress(NULL)
{
}

ServerApp::~ServerApp()
{
}

void
ServerApp::parseArgs(int argc, const char* const* argv)
{
    ArgParser argParser(this);
    bool result = argParser.parseServerArgs(args(), argc, argv);

    if (!result || args().m_shouldExit) {
        m_bye(kExitArgs);
    }
    else {
        if (!args().m_barrierAddress.empty()) {
            try {
                *m_barrierAddress = NetworkAddress(args().m_barrierAddress,
                    kDefaultPort);
                m_barrierAddress->resolve();
            }
            catch (XSocketAddress& e) {
                LOG((CLOG_PRINT "%s: %s" BYE,
                    args().m_exename.c_str(), e.what(), args().m_exename.c_str()));
                m_bye(kExitArgs);
            }
        }
    }
}

void
ServerApp::help()
{
    // window api args (windows/x-windows/carbon)
#if WINAPI_XWINDOWS
#  define WINAPI_ARGS \
    " [--display <display>] [--no-xinitthreads]"
#  define WINAPI_INFO \
    "      --display <display>  connect to the X server at <display>\n" \
    "      --no-xinitthreads    do not call XInitThreads()\n" \
    "      --screen-change-script <path>\n" \
    "                           full path to script to run on screen change\n" \
    "                           first argument is the new screen name\n"
#else
#  define WINAPI_ARGS ""
#  define WINAPI_INFO ""
#endif

    // refer to custom profile directory even if not saved yet
    barrier::fs::path profile_path = argsBase().m_profileDirectory;
    if (profile_path.empty()) {
        profile_path = barrier::DataDirectories::profile();
    }

    auto usr_config_path = (profile_path / barrier::fs::u8path(USR_CONFIG_NAME)).u8string();
    auto sys_config_path = (barrier::DataDirectories::systemconfig() /
                            barrier::fs::u8path(SYS_CONFIG_NAME)).u8string();

    std::ostringstream buffer;
    buffer << "Start the barrier server component.\n"
           << "\n"
           << "Usage: " << args().m_exename
           << " [--address <address>]"
           << " [--config <pathname>]"
           << " [--config-url <url>]"
           << " [--download-url <url> --download-path <path>]"
           << WINAPI_ARGS << HELP_SYS_ARGS << HELP_COMMON_ARGS << "\n"
           << "\n"
           << "Options:\n"
           << "  -a, --address <address>  listen for clients on the given address.\n"
           << "  -c, --config <pathname>  use the named configuration file instead.\n"
           << "      --config-url <url>   fetch the configuration from <url> instead.\n"
           << "                           falls back to the local configuration on\n"
           << "                           failure.\n"
           << "      --download-url <url>  download <url> on every configuration\n"
           << "      --download-path <path>  reload and write it to <path>.  Both\n"
           << "                           options must be given together.\n"
           << HELP_COMMON_INFO_1
           << "      --disable-client-cert-checking disable client SSL certificate \n"
              "                                     checking (deprecated)\n"
           << "      --external-command-port <port>  port for the external command\n"
              "                                     channel on 127.0.0.1 (default 8080)\n"
              "                                     send \"reloadConfig|\" to reload the\n"
              "                                     configuration\n"
           << WINAPI_INFO << HELP_SYS_INFO << HELP_COMMON_INFO_2 << "\n"
           << "Default options are marked with a *\n"
           << "\n"
           << "The argument for --address is of the form: [<hostname>][:<port>].  The\n"
           << "hostname must be the address or hostname of an interface on the system.\n"
           << "Placing brackets around an IPv6 address is required when also specifying \n"
           << "a port number and optional otherwise. The default is to listen on all\n"
           << "interfaces using port number " << kDefaultPort << ".\n"
           << "\n"
           << "If no configuration file pathname is provided then the first of the\n"
           << "following to load successfully sets the configuration:\n"
           << "  " << usr_config_path << "\n"
           << "  " << sys_config_path << "\n";

    LOG((CLOG_PRINT "%s", buffer.str().c_str()));
}

void
ServerApp::reloadSignalHandler(Arch::ESignal, void*)
{
    IEventQueue* events = App::instance().getEvents();
    events->addEvent(Event(events->forServerApp().reloadConfig(),
        events->getSystemTarget()));
}

void
ServerApp::reloadConfig(const Event&, void*)
{
    LOG((CLOG_DEBUG "reload configuration"));

    // keep the auxiliary file in sync with the configuration on every reload
    if (!args().m_downloadUrl.empty() && !args().m_downloadPath.empty()) {
        if (!downloadFile(args().m_downloadUrl, args().m_downloadPath)) {
            LOG((CLOG_WARN "failed to download \"%s\" to \"%s\", continuing",
                args().m_downloadUrl.c_str(), args().m_downloadPath.c_str()));
        }
    }

    // parse into a temporary so a failed or rejected reload leaves the
    // running configuration untouched.
    Config config(m_events);

    bool loaded = false;

    // prefer the remote configuration if a URL was provided
    if (!args().m_configUrl.empty()) {
        loaded = loadConfigFromUrl(config);
    }

    // fall back to the local configuration file
    if (!loaded && !args().m_configFile.empty()) {
        loaded = loadConfigInto(args().m_configFile, config);
    }

    if (!loaded) {
        // keep the current configuration if nothing could be loaded
        LOG((CLOG_ERR "failed to reload configuration, keeping current configuration"));
        return;
    }

    if (m_server != NULL) {
        // validate before installing so a rejected reload cannot disturb
        // the running configuration; Server::setConfig() operates on the
        // configuration installed in args().
        if (!m_server->isConfigValid(config)) {
            LOG((CLOG_ERR "reloaded configuration rejected, keeping current configuration"));
            return;
        }
        *args().m_config = config;
        m_server->setConfig(config);
    }
    else {
        *args().m_config = config;
    }

    LOG((CLOG_NOTE "reloaded configuration"));
}

void
ServerApp::loadConfig()
{
    bool loaded = false;

    // prefer the remote configuration if a URL was provided
    if (!args().m_configUrl.empty()) {
        loaded = loadConfigFromUrl(*args().m_config);
        if (!loaded) {
            LOG((CLOG_WARN "cannot load configuration from \"%s\", "
                "falling back to local configuration", args().m_configUrl.c_str()));
        }
    }

    if (!loaded) {
        // load the config file, if specified
        if (!args().m_configFile.empty()) {
            loaded = loadConfigInto(args().m_configFile, *args().m_config);
        }

        // load the default configuration if no explicit file given
        else {
            auto path = barrier::DataDirectories::profile();
            if (!path.empty()) {
                // complete path
                path /= barrier::fs::u8path(USR_CONFIG_NAME);

                // now try loading the user's configuration
                if (loadConfigInto(path.u8string(), *args().m_config)) {
                    loaded            = true;
                    args().m_configFile = path.u8string();
                }
            }
            if (!loaded) {
                // try the system-wide config file
                path = barrier::DataDirectories::systemconfig();
                if (!path.empty()) {
                    path /= barrier::fs::u8path(SYS_CONFIG_NAME);
                    if (loadConfigInto(path.u8string(), *args().m_config)) {
                        loaded            = true;
                        args().m_configFile = path.u8string();
                    }
                }
            }
        }
    }

    if (!loaded) {
        LOG((CLOG_PRINT "%s: no configuration available", args().m_exename.c_str()));
        m_bye(kExitConfig);
    }
}

bool
ServerApp::loadConfigFromStream(std::istream& stream, const String& sourceName,
                                Config& config)
{
    try {
        // load configuration
        stream >> config;
        LOG((CLOG_DEBUG "configuration read successfully from \"%s\"",
            sourceName.c_str()));
        return true;
    }
    catch (XConfigRead& e) {
        // report error in configuration source
        LOG((CLOG_ERR "cannot read configuration from \"%s\": %s",
            sourceName.c_str(), e.what()));
    }
    return false;
}

bool
ServerApp::loadConfigFromUrl(Config& config)
{
    const String& url = args().m_configUrl;

    try {
        LOG((CLOG_DEBUG "fetching configuration from \"%s\"", url.c_str()));
        std::string contents = ARCH->internet().get(url);
        // an empty response would otherwise be accepted as an empty
        // configuration; treat it as a failure instead.
        if (contents.find_first_not_of(" \t\r\n") == std::string::npos) {
            LOG((CLOG_ERR "configuration fetched from \"%s\" is empty", url.c_str()));
            return false;
        }
        std::istringstream stream(contents);
        return loadConfigFromStream(stream, url, config);
    }
    catch (XArch& e) {
        LOG((CLOG_ERR "cannot fetch configuration from \"%s\": %s",
            url.c_str(), e.what()));
    }
    return false;
}

bool
ServerApp::loadConfig(const String& pathname)
{
    return loadConfigInto(pathname, *args().m_config);
}

bool
ServerApp::loadConfigInto(const String& pathname, Config& config)
{
    // load configuration
    LOG((CLOG_DEBUG "opening configuration \"%s\"", pathname.c_str()));
    std::ifstream configStream(pathname.c_str());
    if (!configStream.is_open()) {
        // report failure to open configuration as a debug message
        // since we try several paths and we expect some to be
        // missing.
        LOG((CLOG_DEBUG "cannot open configuration \"%s\"",
            pathname.c_str()));
        return false;
    }
    return loadConfigFromStream(configStream, pathname, config);
}

bool
ServerApp::downloadFile(const String& url, const String& pathname)
{
    LOG((CLOG_DEBUG "downloading \"%s\" to \"%s\"",
        url.c_str(), pathname.c_str()));

    try {
        std::string contents = ARCH->internet().get(url);
        if (contents.empty()) {
            LOG((CLOG_ERR "downloaded file from \"%s\" is empty", url.c_str()));
            return false;
        }

        auto path = barrier::fs::u8path(pathname);

        // make sure the destination directory exists
        auto dir = path.parent_path();
        if (!dir.empty()) {
            std::error_code ec;
            barrier::fs::create_directories(dir, ec);
            if (ec) {
                LOG((CLOG_ERR "cannot create directory \"%s\": %s",
                    dir.u8string().c_str(), ec.message().c_str()));
                return false;
            }
        }

        // write to a temporary file in the destination directory and rename it
        // into place so an interrupted write cannot corrupt the destination
        auto tmpPath = barrier::fs::u8path(pathname + ".part");

        std::ofstream out;
        barrier::open_utf8_path(out, tmpPath,
            std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
        if (!out.is_open()) {
            LOG((CLOG_ERR "cannot open \"%s\" for writing",
                tmpPath.u8string().c_str()));
            return false;
        }
        out.write(contents.data(), contents.size());
        out.close();
        if (!out) {
            LOG((CLOG_ERR "cannot write \"%s\"", tmpPath.u8string().c_str()));
            std::error_code ec;
            barrier::fs::remove(tmpPath, ec);
            return false;
        }

        std::error_code ec;
        barrier::fs::rename(tmpPath, path, ec);
        if (ec) {
            // some platforms refuse to replace an existing file
            barrier::fs::remove(path, ec);
            ec.clear();
            barrier::fs::rename(tmpPath, path, ec);
        }
        if (ec) {
            LOG((CLOG_ERR "cannot move \"%s\" to \"%s\": %s",
                tmpPath.u8string().c_str(), path.u8string().c_str(),
                ec.message().c_str()));
            barrier::fs::remove(tmpPath, ec);
            return false;
        }

        LOG((CLOG_NOTE "downloaded \"%s\" to \"%s\"",
            url.c_str(), pathname.c_str()));
        return true;
    }
    catch (XArch& e) {
        LOG((CLOG_ERR "cannot download \"%s\": %s", url.c_str(), e.what()));
    }
    return false;
}

void
ServerApp::forceReconnect(const Event&, void*)
{
    if (m_server != NULL) {
        m_server->disconnect();
    }
}

void
ServerApp::handleClientConnected(const Event&, void* vlistener)
{
    ClientListener* listener = static_cast<ClientListener*>(vlistener);
    ClientProxy* client = listener->getNextClient();
    if (client != NULL) {
        m_server->adoptClient(client);
        updateStatus();
    }
}

void
ServerApp::handleClientsDisconnected(const Event&, void*)
{
    m_events->addEvent(Event(Event::kQuit));
}

void
ServerApp::closeServer(Server* server)
{
    if (server == NULL) {
        return;
    }

    // tell all clients to disconnect
    server->disconnect();

    // wait for clients to disconnect for up to timeout seconds
    double timeout = 3.0;
    EventQueueTimer* timer = m_events->newOneShotTimer(timeout, NULL);
    m_events->adoptHandler(Event::kTimer, timer,
        new TMethodEventJob<ServerApp>(this, &ServerApp::handleClientsDisconnected));
    m_events->adoptHandler(m_events->forServer().disconnected(), server,
        new TMethodEventJob<ServerApp>(this, &ServerApp::handleClientsDisconnected));

    m_events->loop();

    m_events->removeHandler(Event::kTimer, timer);
    m_events->deleteTimer(timer);
    m_events->removeHandler(m_events->forServer().disconnected(), server);

    // done with server
    delete server;
}

void
ServerApp::stopRetryTimer()
{
    if (m_timer != NULL) {
        m_events->removeHandler(Event::kTimer, m_timer);
        m_events->deleteTimer(m_timer);
        m_timer = NULL;
    }
}

void
ServerApp::updateStatus()
{
    updateStatus("");
}

void ServerApp::updateStatus(const String& msg)
{
    if (m_taskBarReceiver)
    {
        m_taskBarReceiver->updateStatus(m_server, msg);
    }
}

void
ServerApp::closeClientListener(ClientListener* listen)
{
    if (listen != NULL) {
        m_events->removeHandler(m_events->forClientListener().connected(), listen);
        delete listen;
    }
}

void
ServerApp::stopServer()
{
    if (m_serverState == kStarted) {
        closeServer(m_server);
        closeClientListener(m_listener);
        m_server      = NULL;
        m_listener    = NULL;
        m_serverState = kInitialized;
    }
    else if (m_serverState == kStarting) {
        stopRetryTimer();
        m_serverState = kInitialized;
    }
    assert(m_server == NULL);
    assert(m_listener == NULL);
}

void
ServerApp::closePrimaryClient(PrimaryClient* primaryClient)
{
    delete primaryClient;
}

void
ServerApp::closeServerScreen(barrier::Screen* screen)
{
    if (screen != NULL) {
        m_events->removeHandler(m_events->forIScreen().error(),
            screen->getEventTarget());
        m_events->removeHandler(m_events->forIScreen().suspend(),
            screen->getEventTarget());
        m_events->removeHandler(m_events->forIScreen().resume(),
            screen->getEventTarget());
        delete screen;
    }
}

void ServerApp::cleanupServer()
{
    stopServer();
    if (m_serverState == kInitialized) {
        closePrimaryClient(m_primaryClient);
        closeServerScreen(m_serverScreen);
        m_primaryClient = NULL;
        m_serverScreen  = NULL;
        m_serverState   = kUninitialized;
    }
    else if (m_serverState == kInitializing ||
        m_serverState == kInitializingToStart) {
            stopRetryTimer();
            m_serverState = kUninitialized;
    }
    assert(m_primaryClient == NULL);
    assert(m_serverScreen == NULL);
    assert(m_serverState == kUninitialized);
}

void
ServerApp::retryHandler(const Event&, void*)
{
    // discard old timer
    assert(m_timer != NULL);
    stopRetryTimer();

    // try initializing/starting the server again
    switch (m_serverState) {
    case kUninitialized:
    case kInitialized:
    case kStarted:
        assert(0 && "bad internal server state");
        break;

    case kInitializing:
        LOG((CLOG_DEBUG1 "retry server initialization"));
        m_serverState = kUninitialized;
        if (!initServer()) {
            m_events->addEvent(Event(Event::kQuit));
        }
        break;

    case kInitializingToStart:
        LOG((CLOG_DEBUG1 "retry server initialization"));
        m_serverState = kUninitialized;
        if (!initServer()) {
            m_events->addEvent(Event(Event::kQuit));
        }
        else if (m_serverState == kInitialized) {
            LOG((CLOG_DEBUG1 "starting server"));
            if (!startServer()) {
                m_events->addEvent(Event(Event::kQuit));
            }
        }
        break;

    case kStarting:
        LOG((CLOG_DEBUG1 "retry starting server"));
        m_serverState = kInitialized;
        if (!startServer()) {
            m_events->addEvent(Event(Event::kQuit));
        }
        break;
    }
}

bool ServerApp::initServer()
{
    // skip if already initialized or initializing
    if (m_serverState != kUninitialized) {
        return true;
    }

    double retryTime;
    barrier::Screen* serverScreen         = NULL;
    PrimaryClient* primaryClient = NULL;
    try {
        String name    = args().m_config->getCanonicalName(args().m_name);
        serverScreen    = openServerScreen();
        primaryClient   = openPrimaryClient(name, serverScreen);
        m_serverScreen  = serverScreen;
        m_primaryClient = primaryClient;
        m_serverState   = kInitialized;
        updateStatus();
        return true;
    }
    catch (XScreenUnavailable& e) {
        LOG((CLOG_WARN "primary screen unavailable: %s", e.what()));
        closePrimaryClient(primaryClient);
        closeServerScreen(serverScreen);
        updateStatus(String("primary screen unavailable: ") + e.what());
        retryTime = e.getRetryTime();
    }
    catch (XScreenOpenFailure& e) {
        LOG((CLOG_CRIT "failed to start server: %s", e.what()));
        closePrimaryClient(primaryClient);
        closeServerScreen(serverScreen);
        return false;
    }
    catch (XBase& e) {
        LOG((CLOG_CRIT "failed to start server: %s", e.what()));
        closePrimaryClient(primaryClient);
        closeServerScreen(serverScreen);
        return false;
    }

    if (args().m_restartable) {
        // install a timer and handler to retry later
        assert(m_timer == NULL);
        LOG((CLOG_DEBUG "retry in %.0f seconds", retryTime));
        m_timer = m_events->newOneShotTimer(retryTime, NULL);
        m_events->adoptHandler(Event::kTimer, m_timer,
            new TMethodEventJob<ServerApp>(this, &ServerApp::retryHandler));
        m_serverState = kInitializing;
        return true;
    }
    else {
        // don't try again
        return false;
    }
}

barrier::Screen*
ServerApp::openServerScreen()
{
    barrier::Screen* screen = createScreen();
    if (!argsBase().m_dropTarget.empty()) {
        screen->setDropTarget(argsBase().m_dropTarget);
    }
    screen->setEnableDragDrop(argsBase().m_enableDragDrop);
    m_events->adoptHandler(m_events->forIScreen().error(),
        screen->getEventTarget(),
        new TMethodEventJob<ServerApp>(
        this, &ServerApp::handleScreenError));
    m_events->adoptHandler(m_events->forIScreen().suspend(),
        screen->getEventTarget(),
        new TMethodEventJob<ServerApp>(
        this, &ServerApp::handleSuspend));
    m_events->adoptHandler(m_events->forIScreen().resume(),
        screen->getEventTarget(),
        new TMethodEventJob<ServerApp>(
        this, &ServerApp::handleResume));
    return screen;
}

static const char* const family_string(IArchNetwork::EAddressFamily family)
{
    if (family == IArchNetwork::kINET)
        return "IPv4";
    if (family == IArchNetwork::kINET6)
        // assume IPv6 sockets are setup to support IPv4 traffic as well
        return "IPv4/IPv6";
    return "Unknown";
}

bool
ServerApp::startServer()
{
    // skip if already started or starting
    if (m_serverState == kStarting || m_serverState == kStarted) {
        return true;
    }

    // initialize if necessary
    if (m_serverState != kInitialized) {
        if (!initServer()) {
            // hard initialization failure
            return false;
        }
        if (m_serverState == kInitializing) {
            // not ready to start
            m_serverState = kInitializingToStart;
            return true;
        }
        assert(m_serverState == kInitialized);
    }

    double retryTime;
    ClientListener* listener = NULL;
    try {
        auto listenAddress = args().m_config->getBarrierAddress();
        auto family = family_string(ARCH->getAddrFamily(listenAddress.getAddress()));
        listener   = openClientListener(listenAddress);
        m_server   = openServer(*args().m_config, m_primaryClient);
        listener->setServer(m_server);
        m_server->setListener(listener);
        m_listener = listener;
        updateStatus();

        // using CLOG_PRINT here allows the GUI to see that the server is started
        // regardless of which log level is set
        LOG((CLOG_PRINT "started server (%s), waiting for clients", family));
        m_serverState = kStarted;
        return true;
    }
    catch (XSocketAddressInUse& e) {
        LOG((CLOG_ERR "cannot listen for clients: %s", e.what()));
        closeClientListener(listener);
        updateStatus(String("cannot listen for clients: ") + e.what());
        retryTime = 1.0;
    }
    catch (XBase& e) {
        LOG((CLOG_CRIT "failed to start server: %s", e.what()));
        closeClientListener(listener);
        return false;
    }

    if (args().m_restartable) {
        // install a timer and handler to retry later
        assert(m_timer == NULL);
        LOG((CLOG_DEBUG "retry in %.0f seconds", retryTime));
        m_timer = m_events->newOneShotTimer(retryTime, NULL);
        m_events->adoptHandler(Event::kTimer, m_timer,
            new TMethodEventJob<ServerApp>(this, &ServerApp::retryHandler));
        m_serverState = kStarting;
        return true;
    }
    else {
        // don't try again
        return false;
    }
}

barrier::Screen*
ServerApp::createScreen()
{
#if WINAPI_MSWINDOWS
    return new barrier::Screen(new MSWindowsScreen(
        true, args().m_noHooks, args().m_stopOnDeskSwitch, m_events), m_events);
#elif WINAPI_XWINDOWS
    return new barrier::Screen(new XWindowsScreen(
        new XWindowsImpl(),
        args().m_display, true, args().m_disableXInitThreads, 0, m_events), m_events);
#elif WINAPI_CARBON
    return new barrier::Screen(new OSXScreen(m_events, true), m_events);
#endif
}

PrimaryClient*
ServerApp::openPrimaryClient(const String& name, barrier::Screen* screen)
{
    LOG((CLOG_DEBUG1 "creating primary screen"));
    return new PrimaryClient(name, screen);

}

void
ServerApp::handleScreenError(const Event&, void*)
{
    LOG((CLOG_CRIT "error on screen"));
    m_events->addEvent(Event(Event::kQuit));
}

void
ServerApp::handleSuspend(const Event&, void*)
{
    if (!m_suspended) {
        LOG((CLOG_INFO "suspend"));
        stopServer();
        m_suspended = true;
    }
}

void
ServerApp::handleResume(const Event&, void*)
{
    if (m_suspended) {
        LOG((CLOG_INFO "resume"));
        startServer();
        m_suspended = false;
    }
}

ClientListener*
ServerApp::openClientListener(const NetworkAddress& address)
{
    auto security_level = ConnectionSecurityLevel::PLAINTEXT;
    if (args().m_enableCrypto) {
        security_level = ConnectionSecurityLevel::ENCRYPTED;
        if (args().check_client_certificates) {
            security_level = ConnectionSecurityLevel::ENCRYPTED_AUTHENTICATED;
        }
    }

    ClientListener* listen = new ClientListener(
        address,
        new TCPSocketFactory(m_events, getSocketMultiplexer()),
        m_events, security_level);

    m_events->adoptHandler(
        m_events->forClientListener().connected(), listen,
        new TMethodEventJob<ServerApp>(
            this, &ServerApp::handleClientConnected, listen));

    return listen;
}

Server*
ServerApp::openServer(Config& config, PrimaryClient* primaryClient)
{
    Server* server = new Server(config, primaryClient, m_serverScreen, m_events,
                                getSocketMultiplexer(), args());
    try {
        m_events->adoptHandler(
            m_events->forServer().disconnected(), server,
            new TMethodEventJob<ServerApp>(this, &ServerApp::handleNoClients));

        m_events->adoptHandler(
            m_events->forServer().screenSwitched(), server,
            new TMethodEventJob<ServerApp>(this, &ServerApp::handleScreenSwitched));

    } catch (std::bad_alloc &ba) {
        delete server;
        throw ba;
    }

    return server;
}

void
ServerApp::handleNoClients(const Event&, void*)
{
    updateStatus();
}

void
ServerApp::handleScreenSwitched(const Event& e, void*)
{
    #ifdef WINAPI_XWINDOWS
        Server::SwitchToScreenInfo* info = (Server::SwitchToScreenInfo*)(e.getData());

        if (!args().m_screenChangeScript.empty()) {
            LOG((CLOG_INFO "Running shell script for screen \"%s\"", info->m_screen));

            signal(SIGCHLD, SIG_IGN);

            if (!access(args().m_screenChangeScript.c_str(), X_OK)) {
                pid_t pid = fork();
                if (pid == 0) {
                    execl(args().m_screenChangeScript.c_str(),args().m_screenChangeScript.c_str(),info->m_screen,NULL);
                    exit(0);
                } else if (pid < 0) {
                    LOG((CLOG_ERR "Script forking error"));
                    exit(1);
                }
            } else {
                LOG((CLOG_ERR "Script not accessible \"%s\"", args().m_screenChangeScript.c_str()));
            }
        }
    #endif
}

int
ServerApp::mainLoop()
{
    // create socket multiplexer.  this must happen after daemonization
    // on unix because threads evaporate across a fork().
    setSocketMultiplexer(std::make_unique<SocketMultiplexer>());

    // if configuration has no screens then add this system
    // as the default
    if (args().m_config->begin() == args().m_config->end()) {
        args().m_config->addScreen(args().m_name);
    }

    // set the contact address, if provided, in the config.
    // otherwise, if the config doesn't have an address, use
    // the default.
    if (m_barrierAddress->isValid()) {
        args().m_config->setBarrierAddress(*m_barrierAddress);
    }
    else if (!args().m_config->getBarrierAddress().isValid()) {
        args().m_config->setBarrierAddress(NetworkAddress(kDefaultPort));
    }

    // canonicalize the primary screen name
    String primaryName = args().m_config->getCanonicalName(args().m_name);
    if (primaryName.empty()) {
        LOG((CLOG_CRIT "unknown screen name `%s'", args().m_name.c_str()));
        return kExitFailed;
    }

    // start server, etc
    appUtil().startNode();

    // init ipc client after node start, since create a new screen wipes out
    // the event queue (the screen ctors call adoptBuffer).
    if (argsBase().m_enableIpc) {
        initIpcClient();
    }

    // handle hangup signal by reloading the server's configuration
    ARCH->setSignalHandler(Arch::kHANGUP, &reloadSignalHandler, NULL);
    m_events->adoptHandler(m_events->forServerApp().reloadConfig(),
        m_events->getSystemTarget(),
        new TMethodEventJob<ServerApp>(this, &ServerApp::reloadConfig));

    // handle force reconnect event by disconnecting clients.  they'll
    // reconnect automatically.
    m_events->adoptHandler(m_events->forServerApp().forceReconnect(),
        m_events->getSystemTarget(),
        new TMethodEventJob<ServerApp>(this, &ServerApp::forceReconnect));

    // to work around the sticky meta keys problem, we'll give users
    // the option to reset the state of barriers
    m_events->adoptHandler(m_events->forServerApp().resetServer(),
        m_events->getSystemTarget(),
        new TMethodEventJob<ServerApp>(this, &ServerApp::resetServer));

    // run event loop.  if startServer() failed we're supposed to retry
    // later.  the timer installed by startServer() will take care of
    // that.
    DAEMON_RUNNING(true);

#if defined(MAC_OS_X_VERSION_10_7)

    Thread thread([this](){ run_events_loop(); });

    // wait until carbon loop is ready
    OSXScreen* screen = dynamic_cast<OSXScreen*>(
        m_serverScreen->getPlatformScreen());
    screen->waitForCarbonLoop();

    runCocoaApp();
#else
    m_events->loop();
#endif

    DAEMON_RUNNING(false);

    // close down
    LOG((CLOG_DEBUG1 "stopping server"));
    m_events->removeHandler(m_events->forServerApp().forceReconnect(),
        m_events->getSystemTarget());
    m_events->removeHandler(m_events->forServerApp().reloadConfig(),
        m_events->getSystemTarget());
    cleanupServer();
    updateStatus();
    LOG((CLOG_NOTE "stopped server"));

    if (argsBase().m_enableIpc) {
        cleanupIpcClient();
    }

    return kExitSuccess;
}

void ServerApp::resetServer(const Event&, void*)
{
    LOG((CLOG_DEBUG1 "resetting server"));
    stopServer();
    cleanupServer();
    startServer();
}

int
ServerApp::runInner(int argc, char** argv, ILogOutputter* outputter, StartupFunc startup)
{
    // general initialization
    m_barrierAddress = new NetworkAddress;
    args().m_config         = new Config(m_events);
    args().m_exename = ArgParser::parse_exename(argv[0]);

    // install caller's output filter
    if (outputter != NULL) {
        CLOG->insert(outputter);
    }

    // run
    int result = startup(argc, argv);

    if (m_taskBarReceiver)
    {
        // done with task bar receiver
        delete m_taskBarReceiver;
    }

    delete args().m_config;
    delete m_barrierAddress;
    return result;
}

int daemonMainLoopStatic(int argc, const char** argv) {
    return ServerApp::instance().daemonMainLoop(argc, argv);
}

int
ServerApp::standardStartup(int argc, char** argv)
{
    initApp(argc, argv);

    // daemonize if requested
    if (args().m_daemon) {
        return ARCH->daemonize(daemonName(), daemonMainLoopStatic);
    }
    else {
        return mainLoop();
    }
}

int
ServerApp::foregroundStartup(int argc, char** argv)
{
    initApp(argc, argv);

    // never daemonize
    return mainLoop();
}

const char*
ServerApp::daemonName() const
{
#if SYSAPI_WIN32
    return "Barrier Server";
#elif SYSAPI_UNIX
    return "barriers";
#endif
}

const char*
ServerApp::daemonInfo() const
{
#if SYSAPI_WIN32
    return "Shares this computers mouse and keyboard with other computers.";
#elif SYSAPI_UNIX
    return "";
#endif
}

void
ServerApp::startNode()
{
    // start the server.  if this return false then we've failed and
    // we shouldn't retry.
    LOG((CLOG_DEBUG1 "starting server"));
    if (!startServer()) {
        m_bye(kExitFailed);
    }
}
