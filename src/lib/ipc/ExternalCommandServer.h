#pragma once

#include "net/NetworkAddress.h"
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <string>

class ExternalCommandClientProxy;
class TCPListenSocket;
class SocketMultiplexer;
class IEventQueue;
class Event;

//! External command that asks the server to reload its configuration.
#define EXTERNAL_COMMAND_RELOAD_CONFIG "reloadConfig"

class ExternalCommandServer
{
public:
    ExternalCommandServer(IEventQueue *events, SocketMultiplexer *socketMultiplexer, int port);
    ~ExternalCommandServer();

    //! Bind the listen socket. Throws XSocket if the port is unavailable.
    void listen();

    //! Register a callback for a reserved command. Reserved commands are
    //! consumed internally and are not forwarded to the input filter.
    void setCommandHandler(const std::string &command, std::function<void()> handler);

private:
    using ClientList = std::list<ExternalCommandClientProxy *>;
    void init();
    void handleClientConnecting(const Event &, void *);
    void handleClientDisconnected(const Event &, void *);
    void handleMessageReceived(const Event &, void *);
    void deleteClient(ExternalCommandClientProxy *proxy);
    IEventQueue *m_events;
    SocketMultiplexer *m_socketMultiplexer;
    TCPListenSocket *m_socket;
    NetworkAddress m_address;
    ClientList m_clients;
    mutable std::mutex m_clientsMutex;
    std::map<std::string, std::function<void()>> m_commandHandlers;
};
