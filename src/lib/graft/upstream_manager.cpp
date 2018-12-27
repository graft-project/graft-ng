#include "lib/graft/upstream_manager.h"

namespace graft
{

std::pair<UpstreamManager::ConnItem::ConnectionId, mg_connection*> UpstreamManager::ConnItem::getConnection()
{
    assert(m_maxConnections == 0 || m_connCnt < m_maxConnections || m_connCnt == m_maxConnections && !m_idleConnections.empty());
    std::pair<ConnectionId, mg_connection*> res = std::make_pair(0,nullptr);
    if(!m_keepAlive)
    {
        ++m_connCnt;
        return res;
    }
    if(!m_idleConnections.empty())
    {
        auto it = m_idleConnections.begin();
        res = std::make_pair(it->second, it->first);
        m_idleConnections.erase(it);
    }
    else
    {
        ++m_connCnt;
        res.first = ++m_newId;
    }
    auto res1 = m_activeConnections.emplace(res);
    assert(res1.second);
    assert(m_connCnt == m_idleConnections.size() + m_activeConnections.size());
    return res;
}

void UpstreamManager::ConnItem::releaseActive(ConnectionId connectionId, mg_connection* client)
{
    assert(m_keepAlive || ((connectionId == 0) && (client == nullptr)));
    if(!m_keepAlive) return;
    auto it = m_activeConnections.find(connectionId);
    assert(it != m_activeConnections.end());
    assert(it->second == nullptr || client == nullptr || it->second == client);
    if(client != nullptr)
    {
        m_idleConnections.emplace(client, it->first);
        m_upstreamStub.setConnection(client);
    }
    else
    {
        --m_connCnt;
    }
    m_activeConnections.erase(it);
}

void UpstreamManager::ConnItem::onCloseIdle(mg_connection* client)
{
    assert(m_keepAlive);
    auto it = m_idleConnections.find(client);
    assert(it != m_idleConnections.end());
    --m_connCnt;
    m_idleConnections.erase(it);
}

void UpstreamManager::send(BaseTaskPtr bt)
{
    ConnItem* connItem = &m_default;
    {//find connItem
        const std::string& uri = bt->getOutput().uri;
        if(!uri.empty() && uri[0] == '$')
        {//substitutions
            auto it = m_conn2item.find(uri.substr(1));
            if(it == m_conn2item.end())
            {
                std::ostringstream oss;
                oss << "cannot find uri substitution '" << uri << "'";
                throw std::runtime_error(oss.str());
            }
            connItem = &it->second;
        }
    }
    if(connItem->m_maxConnections != 0 && connItem->m_idleConnections.empty() && connItem->m_connCnt == connItem->m_maxConnections)
    {
        connItem->m_taskQueue.push_back(bt);
        return;
    }

    createUpstreamSender(connItem, bt);
}


void UpstreamManager::onDone(UpstreamSender& uss, ConnItem* connItem, ConnItem::ConnectionId connectionId, mg_connection* client)
{
    ++m_cntUpstreamSenderDone;
    m_onDoneCallback(uss);
    connItem->releaseActive(connectionId, client);
    if(connItem->m_taskQueue.empty()) return;
    BaseTaskPtr bt = connItem->m_taskQueue.front(); connItem->m_taskQueue.pop_front();
    createUpstreamSender(connItem, bt);
}

void UpstreamManager::init(const ConfigOpts& copts, mg_mgr* mgr, int http_callback_port, OnDoneCallback onDoneCallback)
{
    m_mgr = mgr;
    m_http_callback_port = http_callback_port;
    m_onDoneCallback = onDoneCallback;

    int uriId = 0;
    m_default = ConnItem(uriId++, copts.cryptonode_rpc_address.c_str(), 0, false, copts.upstream_request_timeout);

    for(auto& subs : OutHttp::uri_substitutions)
    {
        double timeout = std::get<3>(subs.second);
        if(timeout < 1e-5) timeout = copts.upstream_request_timeout;
        auto res = m_conn2item.emplace(subs.first, ConnItem(uriId, std::get<0>(subs.second), std::get<1>(subs.second), std::get<2>(subs.second), timeout));
        assert(res.second);
        ConnItem* connItem = &res.first->second;
        connItem->m_upstreamStub.setCallback([connItem](mg_connection* client){ connItem->onCloseIdle(client); });
    }
}

void UpstreamManager::createUpstreamSender(ConnItem* connItem, BaseTaskPtr bt)
{
    auto onDoneAct = [this, connItem](UpstreamSender& uss, uint64_t connectionId, mg_connection* client)
    {
        onDone(uss, connItem, connectionId, client);
    };

    ++m_cntUpstreamSender;
    UpstreamSender::Ptr uss;
    if(connItem->m_keepAlive)
    {
        auto res = connItem->getConnection();
        uss = UpstreamSender::Create(bt, onDoneAct, res.first, res.second, connItem->m_timeout);
    }
    else
    {
        uss = UpstreamSender::Create(bt, onDoneAct, connItem->m_timeout);
    }

    const std::string& uri = (connItem != &m_default || bt->getOutput().uri.empty())? connItem->m_uri : bt->getOutput().uri;
    uss->send(m_mgr, m_http_callback_port, uri);
}

}//namespace graft

