#pragma once
#include "lib/graft/connection.h"
#include "lib/graft/routing.h"

namespace graft
{

class UpstreamRoutingManager
{
public:
    UpstreamRoutingManager(graftlet::GraftletLoader& graftletLoader)
        : m_graftletLoader(graftletLoader)
    { }

    void add(const std::string& name, RoutingFunction converter);
    bool route(const std::string& in, std::string& out);
private:
    std::map<std::string, RoutingFunction> m_converters;
    graftlet::GraftletLoader& m_graftletLoader;
};

class UpstreamManager
{
public:
    using OnDoneCallback = std::function<void(UpstreamSender& uss)>;

    UpstreamManager() = default;

    void init(graftlet::GraftletLoader& graftletLoader, const ConfigOpts& copts, mg_mgr* mgr, int http_callback_port, OnDoneCallback onDoneCallback);

    bool busy() const
    {
        return (m_cntUpstreamSender != m_cntUpstreamSenderDone);
    }

    void send(BaseTaskPtr& bt);
protected:
    //testGetUri for test only
    const std::string testGetUri(const Output& output);
private:
    class ConnItem
    {
    public:
        using ConnectionId = uint64_t;
        using IpPort = std::string;
        using Uri = std::string;

        struct Bunch
        {
            int m_connCnt = 0;
            std::map<mg_connection*, ConnectionId> m_idleConnections;
            std::map<ConnectionId, mg_connection*> m_activeConnections;
            UpstreamStub m_upstreamStub;
        };

        ConnItem() = default;
        ConnItem(int uriId, const std::string& uri, int maxConnections, bool keepAlive, double timeout)
            : m_uriId(uriId)
            , m_uri(uri)
            , m_maxConnections(maxConnections)
            , m_keepAlive(keepAlive)
            , m_timeout(timeout)
        {
        }
        ~ConnItem()
        {
            for(auto& it : m_bunches)
            {
                auto& b = it.second;
                assert(b.m_idleConnections.empty());
                assert(b.m_activeConnections.empty());
            }
        }

        std::pair<ConnectionId, mg_connection*> getConnection(const IpPort& ip_port);
        void releaseActive(ConnectionId connectionId, const IpPort& ip_port, mg_connection* client);
        void onCloseIdle(const IpPort& ip_port, mg_connection* client);
        Bunch& getBunch(const IpPort& ip_port, bool createIfNotExists = false);

        int m_uriId;
        std::string m_uri;
        double m_timeout;
        int m_maxConnections;
        std::deque< std::tuple<BaseTaskPtr,IpPort,Uri> > m_taskQueue;
        bool m_keepAlive = false;
        std::map<IpPort, Bunch> m_bunches;
    private:
        ConnectionId m_newId = 0;
    };

    void onDone(UpstreamSender& uss, ConnItem* connItem, const std::string& ip_port, ConnItem::ConnectionId connectionId, mg_connection* client);
    void createUpstreamSender(ConnItem* connItem, const std::string& ip_port, BaseTaskPtr bt, const std::string& uri);
    ConnItem* findConnItem(const Output& output, std::string& ip_port, std::string& result_uri);
    const std::string& getUri(ConnItem* connItem, const std::string& inputUri);
    void initUpstreamRoutingManager(graftlet::GraftletLoader& graftletLoader);

    using Uri2ConnItem = std::map<std::string, ConnItem>;

    OnDoneCallback m_onDoneCallback;
    uint64_t m_cntUpstreamSender = 0;
    uint64_t m_cntUpstreamSenderDone = 0;
    ConnItem m_default;
    Uri2ConnItem m_conn2item;
    mg_mgr* m_mgr = nullptr;
    int m_http_callback_port;
    std::unique_ptr<UpstreamRoutingManager> m_upstreamRoutingManager;
protected:
    std::unordered_map<std::string,std::string> m_resolveCache;
};

}//namespace graft

