#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

struct FamilyTraffic
{
    std::uint64_t today_bytes{};
};

/** Samples TCP extended statistics and keeps independent per-day family totals. */
class TrafficSampler
{
public:
    ~TrafficSampler();
    void Sample();
    void SetConfigDir(const wchar_t* config_dir);
    FamilyTraffic IPv4() const { return m_ipv4; }
    FamilyTraffic IPv6() const { return m_ipv6; }

private:
    struct ByteDelta
    {
        std::uint64_t in{};
        std::uint64_t out{};
    };
    struct Previous
    {
        std::uint64_t in{};
        std::uint64_t out{};
    };

    using CounterMap = std::unordered_map<std::wstring, Previous>;

    ByteDelta SampleV4(CounterMap& current_connections);
    ByteDelta SampleV6(CounterMap& current_connections);
    void LoadTotals();
    void SaveTotals(bool force = false);

    CounterMap m_v4_previous;
    CounterMap m_v6_previous;
    FamilyTraffic m_ipv4;
    FamilyTraffic m_ipv6;
    std::wstring m_config_dir;
    std::wstring m_day;
    bool m_dirty{};
    std::uint64_t m_last_save_attempt{};
};
