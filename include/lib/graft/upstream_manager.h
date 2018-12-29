#pragma once
#include "lib/graft/connection.h"


namespace graft
{

class UpstreamManager
{
public:
    using OnDoneCallback = std::function<void(UpstreamSender& uss)>;
    UpstreamManager() = default;

    void init(const ConfigOpts& copts, mg_mgr* mgr, int http_callback_port, OnDoneCallback onDoneCallback);

    bool busy() const
    {
        return (m_cntUpstreamSender != m_cntUpstreamSenderDone);
    }

    void send(BaseTaskPtr bt);
protected:
    const std::string getUri(const std::string& inputUri);
private:
    class ConnItem
    {
    public:
        using ConnectionId = uint64_t;

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
            assert(m_idleConnections.empty());
            assert(m_activeConnections.empty());
        }

        std::pair<ConnectionId, mg_connection*> getConnection();
        void releaseActive(ConnectionId connectionId, mg_connection* client);
        void onCloseIdle(mg_connection* client);

        int m_connCnt = 0;
        int m_uriId;
        std::string m_uri;
        double m_timeout;
        //assert(m_upstreamQueue.empty() || 0 < m_maxConnections);
        int m_maxConnections;
        std::deque<BaseTaskPtr> m_taskQueue;
        bool m_keepAlive = false;
        std::map<mg_connection*, ConnectionId> m_idleConnections;
        std::map<ConnectionId, mg_connection*> m_activeConnections;
        UpstreamStub m_upstreamStub;
    private:
        ConnectionId m_newId = 0;
    };

    void onDone(UpstreamSender& uss, ConnItem* connItem, ConnItem::ConnectionId connectionId, mg_connection* client);
    void createUpstreamSender(ConnItem* connItem, BaseTaskPtr bt);
    ConnItem* findConnItem(const std::string& inputUri);
    const std::string& getUri(ConnItem* connItem, const std::string& inputUri);

    using Uri2ConnItem = std::map<std::string, ConnItem>;

    OnDoneCallback m_onDoneCallback;

    uint64_t m_cntUpstreamSender = 0;
    uint64_t m_cntUpstreamSenderDone = 0;
    ConnItem m_default;
    Uri2ConnItem m_conn2item;
    mg_mgr* m_mgr = nullptr;
    int m_http_callback_port;
};

}//namespace graft

