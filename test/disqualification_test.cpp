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

#include <gtest/gtest.h>

#include "supernode/requests/disqualificator.h"
#include "utils/sample_generator.h"

#include <wallet/wallet2.h>

#include <thread>
#include <mutex>
#include <deque>

template<typename T>
struct less_mem
{
    static_assert(std::is_trivially_copyable<T>::value);
    bool operator() (const T& l, const T& r)
    {
        int res = std::memcmp(&l, &r, sizeof(T));
        return (res<0);
    }
};


TEST(Disqualificator, BBL)
{
    using namespace graft::supernode::request;

    struct cmd_queue
    {
        std::mutex mutex;
        std::deque<BBLDisqualificatorBase::command> deque;
    };

    //N - count or running SNs, DB - dead (never runnign) SNs that in BBQS, DQ - dead SNs that in QCL
    const int N = 14, DB = 2, DQ = 3;
    crypto::public_key pubs[N+DB+DQ];
    crypto::secret_key secs[N+DB+DQ];
    cmd_queue queues[N+DB+DQ];
    std::map<crypto::public_key, int, less_mem<crypto::public_key>> pub2idx;
    for(int i=0; i<N+DB+DQ; ++i)
    {
        crypto::generate_keys(pubs[i], secs[i]);
        pub2idx[pubs[i]] = i;
    }


        // DESIRED_BBQS_SIZE = 8
    std::vector<crypto::public_key> BBQS;
    std::copy(&pubs[3], &pubs[3+8-DB], std::back_inserter(BBQS));
    std::copy(&pubs[N], &pubs[N+DB], std::back_inserter(BBQS));
    std::vector<crypto::public_key> QCL;
    std::copy(&pubs[3+8], &pubs[3+8-DQ], std::back_inserter(QCL));
    std::copy(&pubs[N+DB], &pubs[N+DB+DQ], std::back_inserter(QCL));

    const int BLKS = 20;
    crypto::hash block_hashes[BLKS];
    for(int i = 0; i < BLKS; ++i)
    {
        block_hashes[i] = crypto::cn_fast_hash(&i, sizeof(i));
    }

    auto getBBQSandQCL = [&](uint64_t& block_height, crypto::hash& block_hash, std::vector<crypto::public_key>& bbqs, std::vector<crypto::public_key>& qcl)
    {
        block_height -= 10; //BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT
        block_hash = block_hashes[block_height];
        bbqs = BBQS;
        qcl = QCL;
    };


    std::mutex mut_ptx;
    std::vector<tools::wallet2::pending_tx> txs;
    auto collectTxs = [&](void* ptxs)
    {
        auto& txes = *reinterpret_cast< std::vector<tools::wallet2::pending_tx>* >(ptxs);
        std::lock_guard<std::mutex> lk(mut_ptx);
        txs.insert(txs.end(), txes.begin(), txes.end());
    };

    auto run = [&](int i)->void
    {
        auto getMyKey= [&](crypto::public_key& pub, crypto::secret_key& sec)->void
        {
            pub = pubs[i]; sec = secs[i];
        };

        auto disq = BBLDisqualificatorBase::createTestBBLDisqualificator(getMyKey, getBBQSandQCL, collectTxs);
        while(true)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            BBLDisqualificatorBase::command cmd;
            {
                auto& cq = queues[i];
                std::lock_guard<std::mutex> lk(cq.mutex);
                if(cq.deque.empty()) continue;
                cmd = cq.deque.front(); cq.deque.pop_front();
            }
            if(cmd.uri == "stop") return;

            std::vector<crypto::public_key> forward;
            std::string body, callback_uri;
            disq->process_command(cmd, forward, body, callback_uri);
            for(auto& id : forward)
            {
                BBLDisqualificatorBase::command cmd(callback_uri, body);
                auto& cq = queues[pub2idx[id]];
                std::lock_guard<std::mutex> lk(cq.mutex);
                cq.deque.push_back(std::move(cmd));
            }
        }
    };

    std::thread ths[N];
    for(int i=0; i < N; ++i)
    {
        ths[i] = std::thread(run, i);
    }

    for(int b_height = 10; b_height < BLKS; ++b_height) //BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        BBLDisqualificatorBase::command cmd(b_height, block_hashes[b_height]);
        for(int i=0; i < N; ++i)
        {
            auto& cq = queues[i];
            std::lock_guard<std::mutex> lk(cq.mutex);
            cq.deque.push_back(std::move(cmd));
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    for(int i = 0; i < N; ++i)
    {
        BBLDisqualificatorBase::command cmd; cmd.uri = "stop";
        auto& cq = queues[i];
        std::lock_guard<std::mutex> lk(cq.mutex);
        cq.deque.emplace_back(std::move(cmd));
    }

    for(auto& th : ths)
    {
        th.join();
    }

    {
        auto& map = BBLDisqualificatorBase::get_errors();
        if(!map.empty())
        {
            int total = 0; for(auto& v : map){ total+=v.second; }
            std::cout << "\n" << total << " total errors count \n";
            for(auto& v : map)
            {
                std::cout << v.first << "->" << v.second << "\n";
            }
            EXPECT_EQ(map.find(0), map.end());
            BBLDisqualificatorBase::clear_errors();
        }
    }

    std::cout << txs.size() << " disqualified txs \n";
    EXPECT_EQ(txs.empty(), false);
}

TEST(Disqualificator, AuthS)
{
    using namespace graft::supernode::request;

    struct cmd_queue
    {
        std::mutex mutex;
        std::deque<BBLDisqualificatorBase::command> deque;
    };

    //N - count or running SNs, DA - dead (never runnign) SNs that in auth sample
    constexpr int N = 6, DA = 8-N;
    static_assert(N+DA == 8); //8 == AUTH_SAMPLE_SIZE
    crypto::public_key pubs[N+DA];
    crypto::secret_key secs[N+DA];
    cmd_queue queues[N+DA];
    std::map<crypto::public_key, int, less_mem<crypto::public_key>> pub2idx;
    for(int i=0; i<N+DA; ++i)
    {
        crypto::generate_keys(pubs[i], secs[i]);
        pub2idx[pubs[i]] = i;
    }

    std::vector<crypto::public_key> authS;
    std::copy(&pubs[0], &pubs[N+DA], std::back_inserter(authS));

    const int BLKS = 20;
    crypto::hash block_hashes[BLKS];
    for(int i = 0; i < BLKS; ++i)
    {
        block_hashes[i] = crypto::cn_fast_hash(&i, sizeof(i));
    }

    auto getAuthS = [&](uint64_t& block_height, const std::string& payment_id, crypto::hash& block_hash, std::vector<crypto::public_key>& auths)
    {
        block_height -= 10; //BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT
        block_hash = block_hashes[block_height];
        auths = authS;
    };

    std::mutex mut_ptx;
    std::vector<tools::wallet2::pending_tx> txs;
    auto collectTxs = [&](void* ptxs)
    {
        auto& txes = *reinterpret_cast< std::vector<tools::wallet2::pending_tx>* >(ptxs);
        std::lock_guard<std::mutex> lk(mut_ptx);
        txs.insert(txs.end(), txes.begin(), txes.end());
    };

    auto run = [&](int i)->void
    {
        auto getMyKey= [&](crypto::public_key& pub, crypto::secret_key& sec)->void
        {
            pub = pubs[i]; sec = secs[i];
        };

        auto disq = BBLDisqualificatorBase::createTestAuthSDisqualificator(getMyKey, getAuthS, collectTxs, std::chrono::milliseconds(2000));
        while(true)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            BBLDisqualificatorBase::command cmd_in;
            {
                auto& cq = queues[i];
                std::lock_guard<std::mutex> lk(cq.mutex);
                if(cq.deque.empty()) continue;
                cmd_in = cq.deque.front(); cq.deque.pop_front();
            }
            if(cmd_in.uri == "stop") return;

            std::vector<crypto::public_key> forward;
            std::string body, callback_uri;
            disq->process_command(cmd_in, forward, body, callback_uri);
            for(auto& id : forward)
            {
                BBLDisqualificatorBase::command cmd(callback_uri, body);
                cmd.payment_id = cmd_in.payment_id;
                auto& cq = queues[pub2idx[id]];
                std::lock_guard<std::mutex> lk(cq.mutex);
                cq.deque.push_back(std::move(cmd));
            }
        }
    };

    std::thread ths[N];
    for(int i=0; i < N; ++i)
    {
        ths[i] = std::thread(run, i);
    }

    const std::string payment_id = "a value of payment";
    {// run initDisqualify
        BBLDisqualificatorBase::command cmd( payment_id, {}, 0, block_hashes[0]);
        for(int i=0; i < N; ++i)
        {
            auto& cq = queues[i];
            std::lock_guard<std::mutex> lk(cq.mutex);
            cq.deque.push_back(cmd);
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    {// run startDisqualify
        std::vector<crypto::public_key> ids;
        ids.insert(ids.end(), &pubs[N], &pubs[N+DA]);
        const int b_height = 10; //BLOCKCHAIN_BASED_LIST_DELAY_BLOCK_COUNT)
        BBLDisqualificatorBase::command cmd( payment_id, ids, b_height, block_hashes[b_height]);
        for(int i=0; i < N; ++i)
        {
            auto& cq = queues[i];
            std::lock_guard<std::mutex> lk(cq.mutex);
            cq.deque.push_back(cmd);
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    BBLDisqualificatorBase::waitForTestAuthSDisqualificator();

    for(int i = 0; i < N; ++i)
    {
        BBLDisqualificatorBase::command cmd; cmd.uri = "stop";
        auto& cq = queues[i];
        std::lock_guard<std::mutex> lk(cq.mutex);
        cq.deque.emplace_back(std::move(cmd));
    }

    for(auto& th : ths)
    {
        th.join();
    }

    {
        auto& map = BBLDisqualificatorBase::get_errors();
        if(!map.empty())
        {
            int total = 0; for(auto& v : map){ total+=v.second; }
            std::cout << "\n" << total << " total errors count \n";
            for(auto& v : map)
            {
                std::cout << v.first << "->" << v.second << "\n";
            }
            EXPECT_EQ(map.find(0), map.end());
            BBLDisqualificatorBase::clear_errors();
        }
    }

    std::cout << txs.size() << " disqualified txs \n";
    EXPECT_EQ(txs.size(), N);
}
