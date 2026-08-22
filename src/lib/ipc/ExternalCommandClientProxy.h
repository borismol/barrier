#pragma once

#include <mutex>
#include <string>

namespace barrier { class IStream; }
class IEventQueue;
class Event;

class ExternalCommandClientProxy
{
public:
    ExternalCommandClientProxy(barrier::IStream &stream, IEventQueue *events);
    ~ExternalCommandClientProxy();

private:
    void handleData(const Event &, void *);
    void handleDisconnect(const Event &, void *);
    void handleWriteError(const Event &, void *);
    void disconnect();

private:
    // commands are short; anything larger than this without a delimiter is a
    // misbehaving client, not a long command.
    static const size_t kMaxBufferSize = 4096;

    barrier::IStream &m_stream;
    // holds the tail of a message that hasn't been terminated by a delimiter
    // yet, so commands split across reads are reassembled.
    std::string m_buffer;
    bool m_disconnecting;
    std::mutex m_readMutex;
    std::mutex m_writeMutex;
    IEventQueue *m_events;
};
