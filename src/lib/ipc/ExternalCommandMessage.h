#pragma once

#include "base/EventTypes.h"
#include "base/Event.h"

class ExternalCommandMessage : public EventData
{
public:
    ExternalCommandMessage(std::string &&data)
        : m_data(data)
    {
    }

    std::string getData() const
    {
        return m_data;
    }

private:
    std::string m_data;
};