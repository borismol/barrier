#include "ipc/ExternalCommandClientProxy.h"

#include "io/IStream.h"
#include "ipc/ExternalCommandMessage.h"
#include "base/EventQueue.h"
#include "base/Log.h"
#include "base/TMethodEventJob.h"
#include "base/Event.h"

ExternalCommandClientProxy::ExternalCommandClientProxy(barrier::IStream &stream, IEventQueue *events)
    : m_stream(stream), m_events(events)
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

void ExternalCommandClientProxy::handleData(const Event &, void *)
{
    char buf[100];
    auto n = m_stream.read(buf, 100);
    while (n > 0)
    {
        std::string str(buf, n);

        size_t pos = 0;
        while (pos < n)
        {
            size_t start = pos;
            pos = str.find('|', pos);
            if (pos == std::string::npos) {
                // Do not expect partial data
                LOG((CLOG_ERR "Delimiter not found %s", str.substr(start).c_str()));
                break;
            }

            // don't delete with this event; the data is passed to a new event.
            Event e(m_events->forExternalCommandClientProxy().messageReceived(), this, NULL, Event::kDontFreeData);
            ExternalCommandMessage *m = new ExternalCommandMessage(str.substr(start, pos - start));
            e.setDataObject(m);
            m_events->addEvent(e);

            pos++;
        }

        n = m_stream.read(buf, 100);
    }
}

void ExternalCommandClientProxy::handleDisconnect(const Event &, void *) {
    LOG((CLOG_DEBUG "proxy disconnect"));
}
void ExternalCommandClientProxy::handleWriteError(const Event &, void *) {
    LOG((CLOG_DEBUG "proxy error"));
}