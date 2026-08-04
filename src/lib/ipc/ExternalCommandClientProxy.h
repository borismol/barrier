#pragma once

#include <mutex>

namespace barrier { class IStream; }
class IEventQueue;
class Event;

class ExternalCommandClientProxy
{
public:
    ExternalCommandClientProxy(barrier::IStream &stream, IEventQueue *events);

private:
    // void                send(const IpcMessage& message);
    void handleData(const Event &, void *);
    void handleDisconnect(const Event &, void *);
    void handleWriteError(const Event &, void *);
    // IpcHelloMessage*    parseHello();
    // IpcCommandMessage*    parseCommand();
    // void                disconnect();

private:
    barrier::IStream &m_stream;
    // EIpcClientType        m_clientType;
    bool m_disconnecting;
    std::mutex m_readMutex;
    std::mutex m_writeMutex;
    IEventQueue *m_events;
};
