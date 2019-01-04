#include "lib/graft/upstream_manager.h"

namespace graft
{

UpstreamManager::ConnItem::Bunch& UpstreamManager::ConnItem::getBunch(const IpPort& ip_port, bool createIfNotExists)
{
    assert(m_keepAlive || ip_port.empty());
    auto b_it = m_bunches.find(ip_port);
    assert(b_it != m_bunches.end() || createIfNotExists);
    if(b_it == m_bunches.end())
    {
        auto res = m_bunches.emplace(std::make_pair(ip_port, Bunch()));
        assert(res.second);
        b_it = res.first;

        Bunch& bunch = b_it->second;
        bunch.m_upstreamStub.setCallback([this, ip_port](mg_connection* client){ onCloseIdle(ip_port, client); });
/*
        ConnItem::Bunch* pbunch = &b_it->second;
        pbunch->m_upstreamStub.setCallback([pbunch](mg_connection* client){ pbunch->onCloseIdle(client); });
*/
    }
    return b_it->second;
}


std::pair<UpstreamManager::ConnItem::ConnectionId, mg_connection*> UpstreamManager::ConnItem::getConnection(const IpPort& ip_port)
{
/*
    auto b_it = m_bunches.find(ip_port);
    if(b_it == m_bunches.end())
    {
        auto res = m_bunches.emplace(std::make_pair(ip_port, Bunch()));
        assert(res.second);
        b_it = res.first;

        Bunch& bunch = b_it->second;
        bunch.m_upstreamStub.setCallback([this, ip_port](mg_connection* client){ onCloseIdle(ip_port, client); });
/ *
        ConnItem::Bunch* pbunch = &b_it->second;
        pbunch->m_upstreamStub.setCallback([pbunch](mg_connection* client){ pbunch->onCloseIdle(client); });
* /
    }

    Bunch& bunch = b_it->second;
*/
    Bunch& bunch = getBunch(ip_port, true);

    assert(m_maxConnections == 0 || bunch.m_connCnt < m_maxConnections || bunch.m_connCnt == m_maxConnections && !bunch.m_idleConnections.empty());
    std::pair<ConnectionId, mg_connection*> res = std::make_pair(0,nullptr);
    if(!m_keepAlive)
    {
        ++bunch.m_connCnt;
        return res;
    }
    if(!bunch.m_idleConnections.empty())
    {
        auto it = bunch.m_idleConnections.begin();
        res = std::make_pair(it->second, it->first);
        bunch.m_idleConnections.erase(it);
    }
    else
    {
        ++bunch.m_connCnt;
        res.first = ++m_newId;
    }
    auto res1 = bunch.m_activeConnections.emplace(res);
    assert(res1.second);
    assert(bunch.m_connCnt == bunch.m_idleConnections.size() + bunch.m_activeConnections.size());
    return res;
}

void UpstreamManager::ConnItem::releaseActive(ConnectionId connectionId, const IpPort& ip_port, mg_connection* client)
{
/*
    auto b_it = m_bunches.find(ip_port);
    assert(b_it != m_bunches.end());
    Bunch& bunch = b_it->second;
*/
    Bunch& bunch = getBunch(ip_port);

    assert(m_keepAlive || ((connectionId == 0) && (client == nullptr)));
    if(!m_keepAlive) return;
    auto it = bunch.m_activeConnections.find(connectionId);
    assert(it != bunch.m_activeConnections.end());
    assert(it->second == nullptr || client == nullptr || it->second == client);
    if(client != nullptr)
    {
        bunch.m_idleConnections.emplace(client, it->first);
        bunch.m_upstreamStub.setConnection(client);
    }
    else
    {
        --bunch.m_connCnt;
    }
    bunch.m_activeConnections.erase(it);
}

void UpstreamManager::ConnItem::onCloseIdle(const IpPort& ip_port, mg_connection* client)
{
/*
    auto b_it = m_bunches.find(ip_port);
    assert(b_it != m_bunches.end());
    Bunch& bunch = b_it->second;
*/
    Bunch& bunch = getBunch(ip_port);

    assert(m_keepAlive);
    auto it = bunch.m_idleConnections.find(client);
    assert(it != bunch.m_idleConnections.end());
    --bunch.m_connCnt;
    bunch.m_idleConnections.erase(it);

    assert(bunch.m_connCnt == bunch.m_idleConnections.size() + bunch.m_activeConnections.size());
    if(bunch.m_connCnt == 0)
    {
//        m_bunches.erase(b_it);
        m_bunches.erase(ip_port);
    }
}

//UpstreamManager::ConnItem* UpstreamManager::findConnItem(const std::string& inputUri)
UpstreamManager::ConnItem* UpstreamManager::findConnItem(const Output& output, std::string& ip_port, std::string& result_uri)
{
//    const Output& output = bt->getOutput();

    ConnItem* connItem = &m_default;
    {//find connItem
        const std::string& uri = output.uri;
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
    //host:port
//    unsigned int port;
//    std::string ip_port, uri;
//    output.makeUri(connItem->m_uri, ip_port, result_uri);
    output.makeUri( getUri(connItem, output.uri), ip_port, result_uri);
    if(!connItem->m_keepAlive) ip_port.clear();

    return connItem;
}

void UpstreamManager::send(BaseTaskPtr& bt)
{
//    const Output& output = bt->getOutput();

//    ConnItem* connItem = findConnItem(bt->getOutput().uri);
    std::string ip_port, uri;
    ConnItem* connItem = findConnItem(bt->getOutput(), ip_port, uri);

/*
    auto b_it = connItem->m_bunches.find(ip_port);
    assert(b_it != connItem->m_bunches.end());
    ConnItem::Bunch& bunch = b_it->second;
*/
    ConnItem::Bunch& bunch = connItem->getBunch(ip_port, true);

    if(connItem->m_maxConnections != 0 && bunch.m_idleConnections.empty() && bunch.m_connCnt == connItem->m_maxConnections)
    {
//        connItem->m_taskQueue.push_back(bt);
        connItem->m_taskQueue.push_back( std::make_tuple(bt, ip_port, uri ) );
        return;
    }

    createUpstreamSender(connItem, ip_port, bt, uri);
}


void UpstreamManager::onDone(UpstreamSender& uss, ConnItem* connItem, const std::string& ip_port, ConnItem::ConnectionId connectionId, mg_connection* client)
{
    ++m_cntUpstreamSenderDone;
    m_onDoneCallback(uss);
    connItem->releaseActive(connectionId, ip_port, client);
    if(connItem->m_taskQueue.empty()) return;
//    BaseTaskPtr bt = connItem->m_taskQueue.front(); connItem->m_taskQueue.pop_front();
    std::string ip_port_v, uri; BaseTaskPtr bt;
    std::tie(bt,ip_port_v,uri) = connItem->m_taskQueue.front(); connItem->m_taskQueue.pop_front();
    createUpstreamSender(connItem, ip_port_v, bt, uri);
}

void UpstreamManager::init(const ConfigOpts& copts, mg_mgr* mgr, int http_callback_port, OnDoneCallback onDoneCallback)
{
    m_mgr = mgr;
    m_http_callback_port = http_callback_port;
    m_onDoneCallback = onDoneCallback;

    int uriId = 0;
    m_default = ConnItem(uriId++, copts.cryptonode_rpc_address.c_str(), 0, false, copts.upstream_request_timeout);

    for(auto& subs : copts.uri_substitutions)
    {
        double timeout = std::get<3>(subs.second);
        if(timeout < 1e-5) timeout = copts.upstream_request_timeout;
        auto res = m_conn2item.emplace(subs.first, ConnItem(uriId, std::get<0>(subs.second), std::get<1>(subs.second), std::get<2>(subs.second), timeout));
        assert(res.second);
/*
        ConnItem* connItem = &res.first->second;
        connItem->m_upstreamStub.setCallback([connItem](mg_connection* client){ connItem->onCloseIdle(client); });
*/
    }
}

const std::string& UpstreamManager::getUri(ConnItem* connItem, const std::string& inputUri)
{
    const std::string& uri = (connItem != &m_default || inputUri.empty())? connItem->m_uri : inputUri;
    return uri;
}

void UpstreamManager::createUpstreamSender(ConnItem* connItem, const std::string& ip_port, BaseTaskPtr bt, const std::string& uri)
{
    std::string ip_port_v = ip_port;
    auto onDoneAct = [this, connItem, ip_port_v](UpstreamSender& uss, uint64_t connectionId, mg_connection* client)
    {
        onDone(uss, connItem, ip_port_v, connectionId, client);
    };

    ++m_cntUpstreamSender;
    UpstreamSender::Ptr uss;
    if(connItem->m_keepAlive)
    {
        auto res = connItem->getConnection(ip_port);
        uss = UpstreamSender::Create(bt, onDoneAct, res.first, res.second, connItem->m_timeout);
    }
    else
    {
        uss = UpstreamSender::Create(bt, onDoneAct, connItem->m_timeout);
    }

//    const std::string& uri = getUri(connItem, bt->getOutput().uri);
    uss->send(m_mgr, m_http_callback_port, uri);
}

const std::string UpstreamManager::getUri(const std::string& inputUri)
{
    Output output; output.uri = inputUri;
//    ConnItem* connItem = findConnItem(inputUri);
    std::string ip_port, uri;
    ConnItem* connItem = findConnItem(output, ip_port, uri);
    return uri;
//    return getUri(connItem, inputUri);
}

}//namespace graft

