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

#pragma once

#include "lib/graft/inout.h"

#include <utils/cryptmsg.h>

#include <functional>
#include <map>

namespace graft::supernode::request
{

//The class is used for tests
class BBLDisqualificatorBase
{
public:
    using GetSupernodeKeys = std::function<void (crypto::public_key& pub, crypto::secret_key& sec)>;
    using GetBBQSandQCL = std::function<void (uint64_t& block_height, crypto::hash& block_hash, std::vector<crypto::public_key>& bbqs, std::vector<crypto::public_key>& qcl)>;
    using CollectTxs = std::function<void (void* ptxs)>;

    static std::unique_ptr<BBLDisqualificatorBase> createTestBBLDisqualificator(
            GetSupernodeKeys fnGetSupernodeKeys,
            GetBBQSandQCL fnGetBBQSandQCL,
            CollectTxs fnCollectTxs
        );

    struct command
    {
        std::string uri; //process if empty
            // uri.empty()
        uint64 block_height;
        crypto::hash block_hash;
            // !uri.empty()
        std::string body;

        command() = default;
        command(uint64 block_height, const crypto::hash& block_hash) : block_height(block_height), block_hash(block_hash) { }
        command(const std::string& uri, const std::string& body) : uri(uri), body(body) { }
    };

    virtual void process_command(const command& cmd, std::vector<crypto::public_key>& forward, std::string& body, std::string& callback_uri)
    {
        assert(false);
    }

    static void count_errors(const std::string& msg, int code=0);
    static const std::map<int, int>& get_errors();
    static void clear_errors();
protected:
    GetSupernodeKeys fnGetSupernodeKeys;
    GetBBQSandQCL fnGetBBQSandQCL;
    CollectTxs fnCollectTxs;
};


} //namespace graft::supernode::request
