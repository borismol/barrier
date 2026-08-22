#include "ipc/ExternalCommandClientProxy.h"

#include "io/IStream.h"
#include "ipc/ExternalCommandMessage.h"
#include "base/EventQueue.h"
#include "base/Log.h"
#include "base/TMethodEventJob.h"
#include "base/Event.h"

ExternalCommandClientProxy::ExternalCommandClientProxy(barrier::IStream &stream, IEventQueue *events)
    : m_stream(stream), m_disconnecting(false), m_events(events)
{
    m_events->adoptHandler(
        m_events->forIStream().inputReady(), stream.getEventTarget(),
        new TMethodEventJob<ExternalCommandClientProxy>(
            this, &ExternalCommandClientProxy::handleData));

    m_events->adoptHandler(
        m_events->forIStream().outputError(), stream.getEventTarget(),
        new TMethodEventJob<ExternalCommandClientProxy>(
            this, &ExternalCommandClientProxy::handleWriteError));

    m_events->adoptHandler(
        m_events->forIStream().inputShutdown(), stream.getEventTarget(),
        new TMethodEventJob<ExternalCommandClientProxy>(
            this, &ExternalCommandClientProxy::handleDisconnect));

    m_events->adoptHandler(
        m_events->forIStream().outputShutdown(), stream.getEventTarget(),
        new TMethodEventJob<ExternalCommandClientProxy>(
            this, &ExternalCommandClientProxy::handleWriteError));
}

ExternalCommandClientProxy::~ExternalCommandClientProxy()
{
    m_events->removeHandler(
        m_events->forIStream().inputReady(), m_stream.getEventTarget());
    m_events->removeHandler(
        m_events->forIStream().outputError(), m_stream.getEventTarget());
    m_events->removeHandler(
        m_events->forIStream().inputShutdown(), m_stream.getEventTarget());
    m_events->removeHandler(
        m_events->forIStream().outputShutdown(), m_stream.getEventTarget());

    // don't delete the stream while it's being used.
    {
        std::lock_guard<std::mutex> lock_read(m_readMutex);
        std::lock_guard<std::mutex> lock_write(m_writeMutex);
        delete &m_stream;
    }
}

void ExternalCommandClientProxy::handleData(const Event &, void *)
{
    // don't allow the dtor to destroy the stream while we're using it.
    std::lock_guard<std::mutex> lock(m_readMutex);

    char buf[100];
    UInt32 n = m_stream.read(buf, sizeof(buf));
    while (n > 0)
    {
        m_buffer.append(buf, n);

        // consume every complete, delimiter-terminated message; whatever
        // follows the last delimiter stays buffered for the next read.
        size_t pos;
        while ((pos = m_buffer.find('|')) != std::string::npos)
        {
            // don't delete with this event; the data is passed to a new event.
            Event e(m_events->forExternalCommandClientProxy().messageReceived(), this, NULL,
                    Event::kDontFreeData);
            e.setDataObject(new ExternalCommandMessage(m_buffer.substr(0, pos)));
            m_events->addEvent(e);

            m_buffer.erase(0, pos + 1);
        }

        if (m_buffer.size() > kMaxBufferSize) {
            LOG((CLOG_ERR "external command too long (%d bytes, no delimiter), disconnecting",
                 m_buffer.size()));
            m_buffer.clear();
            disconnect();
            return;
        }

        n = m_stream.read(buf, sizeof(buf));
    }
}

void ExternalCommandClientProxy::handleDisconnect(const Event &, void *)
{
    disconnect();
    LOG((CLOG_DEBUG "external command client disconnected"));
}

void ExternalCommandClientProxy::handleWriteError(const Event &, void *)
{
    disconnect();
    LOG((CLOG_DEBUG "external command client write error"));
}

void ExternalCommandClientProxy::disconnect()
{
    if (m_disconnecting) {
        return;
    }

    LOG((CLOG_DEBUG "external command disconnect, closing stream"));
    m_disconnecting = true;
    m_stream.close();
    m_events->addEvent(Event(m_events->forExternalCommandClientProxy().disconnected(), this));
}
