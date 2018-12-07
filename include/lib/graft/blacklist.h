#pragma once

#include <utility>
#include <cstdint>
#include <functional>
#include <memory>

typedef uint32_t in_addr_t;

namespace graft {

class BlackList
{
public:
    using Allow = bool;

    //requests_per_sec == 0, means disable ban
    //ban_ip_sec == 0, means ban forever
    static std::unique_ptr<BlackList> Create(int requests_per_sec, int window_size_sec, int ban_ip_sec);

    virtual ~BlackList() = default;

    virtual void readRules(const char* filepath) = 0;
    virtual void readRules(std::istream& is) = 0;
    virtual std::string getWarnings() = 0;

    virtual bool processIp(in_addr_t addr, bool networkOrder = true) = 0;
protected:
    BlackList(const BlackList&) = delete;
    BlackList& operator = (const BlackList&) = delete;
    BlackList() = default;
};

class BlackListTest : public BlackList
{
public:
    static std::unique_ptr<BlackListTest> Create(int requests_per_sec, int window_size_sec, int ban_ip_sec);

    virtual void addEntry(const char* ip, int len = 32, Allow allow = false) = 0;
    virtual void addEntry(in_addr_t addr, bool networkOrder = true, Allow allow = false, int len = 32) = 0;
    virtual void removeEntry(const char* ip) = 0;
    virtual void removeEntry(in_addr_t addr, bool networkOrder = true) = 0;
    //returns a pair, first is true if a corresponding entry found, second is true if the addr is effectively allowed
    virtual std::pair<bool, Allow> find(in_addr_t addr, bool networkOrder = true) = 0;
    virtual std::pair<bool, Allow> find(const char* ip) = 0;
    virtual bool active(in_addr_t addr) = 0;
    virtual size_t activeCnt() = 0;
};

} //namespace graft
