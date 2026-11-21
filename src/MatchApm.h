#pragma once

class MatchApm
{
public:
    constexpr void Reset()
    {
        m_first = 0;
        m_count = 0;
    }

    constexpr void Record(unsigned long long now)
    {
        Count(now);
        if (m_count == Capacity)
        {
            m_first = (m_first + 1) % Capacity;
            --m_count;
        }
        m_actions[(m_first + m_count) % Capacity] = now;
        ++m_count;
    }

    constexpr unsigned Count(unsigned long long now)
    {
        while (m_count && now - m_actions[m_first] >= 60000)
        {
            m_first = (m_first + 1) % Capacity;
            --m_count;
        }
        return m_count;
    }

private:
    static constexpr unsigned Capacity = 4096;
    unsigned long long m_actions[Capacity]{};
    unsigned m_first = 0;
    unsigned m_count = 0;
};
