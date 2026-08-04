#include "ipc/ExternalCommandServer.h"
#include "ipc/ExternalCommandClientProxy.h"
#include "ipc/ExternalCommandMessage.h"

#include "net/SocketMultiplexer.h"
#include "net/TCPListenSocket.h"
#include "net/IDataSocket.h"
#include "io/IStream.h"
#include "base/IEventQueue.h"
#include "base/TMethodEventJob.h"
#include "base/Event.h"
#include "base/Log.h"

ExternalCommandServer::ExternalCommandServer(IEventQueue *events, SocketMultiplexer *socketMultiplexer, int port)
    : m_events(events), m_socketMultiplexer(socketMultiplexer), m_address("127.0.0.1", port)
{
    init();
}

void ExternalCommandServer::init()
{
    m_socket = new TCPListenSocket(m_events, m_socketMultiplexer, IArchNetwork::kINET);

    m_address.resolve();

    m_events->adoptHandler(
        m_events->forIListenSocket().connecting(), m_socket,
        new TMethodEventJob<ExternalCommandServer>(
            this, &ExternalCommandServer::handleClientConnecting));
}

void ExternalCommandServer::listen() {
    m_socket->bind(m_address);
}

void ExternalCommandServer::handleClientConnecting(const Event &, void *)
{
    barrier::IStream *stream = m_socket->accept();
    LOG((CLOG_DEBUG "accepted ExternalCommand client connection"));
    if (stream == NULL)
    {
        return;
    }

    LOG((CLOG_DEBUG "accepted ExternalCommand client connection"));

    ExternalCommandClientProxy *proxy = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        proxy = new ExternalCommandClientProxy(*stream, m_events);
        m_clients.push_back(proxy);
    }

    m_events->adoptHandler(
        m_events->forExternalCommandClientProxy().disconnected(), proxy,
        new TMethodEventJob<ExternalCommandServer>(
            this, &ExternalCommandServer::handleClientDisconnected));

    m_events->adoptHandler(
        m_events->forExternalCommandClientProxy().messageReceived(), proxy,
        new TMethodEventJob<ExternalCommandServer>(
            this, &ExternalCommandServer::handleMessageReceived));

    m_events->addEvent(Event(
        m_events->forExternalCommandServer().clientConnected(), this, proxy, Event::kDontFreeData));
}

void ExternalCommandServer::handleClientDisconnected(const Event &e, void *)
{
    auto *proxy = static_cast<ExternalCommandClientProxy *>(e.getTarget());

    std::lock_guard<std::mutex> lock(m_clientsMutex);
    m_clients.remove(proxy);
    deleteClient(proxy);

    LOG((CLOG_DEBUG "external command client proxy removed, connected=%d", m_clients.size()));
}

void ExternalCommandServer::handleMessageReceived(const Event &e, void *)
{
    auto *m = static_cast<ExternalCommandMessage *>(e.getDataObject());
    LOG((CLOG_DEBUG "External command %s", m->getData().c_str()));
    Event event(m_events->forExternalCommandServer().messageReceived(), this);
    event.setDataObject(e.getDataObject());
    m_events->addEvent(event);
}

ExternalCommandServer::~ExternalCommandServer()
{
    if (m_socket != nullptr)
    {
        delete m_socket;
    }

    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        ClientList::iterator it;
        for (it = m_clients.begin(); it != m_clients.end(); it++)
        {
            deleteClient(*it);
        }
        m_clients.clear();
    }

    m_events->removeHandler(m_events->forIListenSocket().connecting(), m_socket);
}

void ExternalCommandServer::deleteClient(ExternalCommandClientProxy *proxy)
{
    m_events->removeHandler(m_events->forExternalCommandClientProxy().messageReceived(), proxy);
    m_events->removeHandler(m_events->forExternalCommandClientProxy().disconnected(), proxy);
    delete proxy;
}