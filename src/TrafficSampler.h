#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

struct FamilyRate
{
    std::uint64_t in_bytes_per_second{};
    std::uint64_t out_bytes_per_second{};
};

/** Samples TCP extended statistics and keeps IPv4/IPv6 state independent. */
class TrafficSampler
{
public:
    void Sample();
    FamilyRate IPv4() const { return m_ipv4; }
    FamilyRate IPv6() const { return m_ipv6; }

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

    CounterMap m_v4_previous;
    CounterMap m_v6_previous;
    FamilyRate m_ipv4;
    FamilyRate m_ipv6;
    std::chrono::steady_clock::time_point m_last_sample{};
};
