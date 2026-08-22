#pragma once

#include "base/EventTypes.h"
#include "base/Event.h"

#include <string>
#include <utility>

class ExternalCommandMessage : public EventData
{
public:
    ExternalCommandMessage(std::string data)
        : m_data(std::move(data))
    {
    }

    const std::string& getData() const
    {
        return m_data;
    }

private:
    std::string m_data;
};
