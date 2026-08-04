#pragma once

#include "net/NetworkAddress.h"
#include <list>
#include <mutex>

class ExternalCommandClientProxy;
class TCPListenSocket;
class SocketMultiplexer;

class ExternalCommandServer
{
public:
    ExternalCommandServer(IEventQueue *events, SocketMultiplexer *socketMultiplexer, int port);
    ~ExternalCommandServer();

    void listen();

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
};