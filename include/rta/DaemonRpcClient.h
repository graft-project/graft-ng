// Copyright (c) 2018, The Graft Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#ifndef DAEMON_RPC_CLIENT_H
#define DAEMON_RPC_CLIENT_H

#include <string>
#include <vector>
#include <chrono>
#include <boost/optional.hpp>

#include <net/http_client.h>
#include <net/http_auth.h>
#include <cryptonote_basic/cryptonote_basic.h>


namespace graft {

class DaemonRpcClient
{
public:
    DaemonRpcClient(const std::string &daemon_addr, const std::string &daemon_login, const std::string &daemon_pass);
    virtual ~DaemonRpcClient();
    bool get_tx_from_pool(const std::string &hash_str, cryptonote::transaction &out_tx);
    bool get_tx(const std::string &hash_str, cryptonote::transaction &out_tx, uint64_t &block_num, bool &mined);
    bool get_height(uint64_t &height);
    bool get_block_hash(uint64_t height, std::string &hash);

protected:
    bool init(const std::string &daemon_address, boost::optional<epee::net_utils::http::login> daemon_login);


private:
    epee::net_utils::http::http_simple_client m_http_client;
    std::chrono::seconds m_rpc_timeout;

};

}

#endif // DAEMON_RPC_CLIENT_H
