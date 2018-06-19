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


#include <misc_log_ex.h>
#include <gtest/gtest.h>
#include <boost/scoped_ptr.hpp>


// cryptonode includes

#include <rta/supernode.h>
#include <rta/fullsupernodelist.h>
#include <misc_log_ex.h>

using namespace graft;

using namespace std::chrono_literals;

struct SupernodeTest : public ::testing::Test
{
    SupernodeTest()
    {
        mlog_configure("", true);
        mlog_set_log_level(1);
    }
};




TEST_F(SupernodeTest, open)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    const std::string wallet_path = "test_supernode1";
    const std::string daemon_addr = "localhost:28881";
    const bool testnet = true;


    Supernode sn1(wallet_path, "", daemon_addr, testnet);
    sn1.refresh();
    EXPECT_TRUE(sn1.walletAddress() == "F9bkNUPVg1JEX81HRrRVaP6MUnpzxXaqtEBjEMFwQu1DZwgg4mVtCW2C35FhYNXed858MaMB7Z531PxUthMGmPS13kmkv1K");
    EXPECT_TRUE(sn1.stakeAmount() > 0);
}

TEST_F(SupernodeTest, watchOnly)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    const std::string wallet_path = "test_supernode1";
    const std::string daemon_addr = "localhost:28881";
    const bool testnet = true;

    Supernode sn1(wallet_path, "", daemon_addr, testnet);
    sn1.refresh();

    // create view only supernode
    crypto::secret_key viewkey = sn1.exportViewkey();
    std::vector<Supernode::KeyImage> key_images;
    sn1.exportKeyImages(key_images);

    boost::filesystem::path temp_path = boost::filesystem::unique_path();
    Supernode * sn2 = Supernode::createFromViewOnlyWallet(temp_path.native(), sn1.walletAddress(), viewkey, testnet);
    LOG_PRINT_L0("temp wallet path: " << temp_path.native());
    ASSERT_TRUE(sn2 != nullptr);

    sn2->setDaemonAddress(daemon_addr);
    sn2->refresh();
    sn2->importKeyImages(key_images);


    EXPECT_TRUE(sn2->walletAddress() == sn1.walletAddress());
    EXPECT_TRUE(sn2->stakeAmount() == sn1.stakeAmount());


    Supernode sn3(temp_path.native(), "", daemon_addr, testnet);

    // watch-only wallet
    std::string msg = "123";
    crypto::signature sign;
    EXPECT_FALSE(sn3.signMessage(msg, sign));

}


TEST_F(SupernodeTest, signAndVerify)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    const std::string wallet_path1 = "test_supernode1";
    const std::string wallet_path2 = "test_supernode2";
    const std::string daemon_addr = "localhost:28881";
    const bool testnet = true;
    const std::string msg1 = "TEST TEST TEST TEST";
    const std::string msg2 = "TEST TEST TEST TEST !!!";

    Supernode sn1(wallet_path1, "", daemon_addr, testnet);
    sn1.refresh();

    Supernode sn2(wallet_path2, "", daemon_addr, testnet);
    sn2.refresh();

    crypto::signature sign1, sign2;
    sn1.signMessage(msg1, sign1);
    sn1.signMessage(msg2, sign2);

    EXPECT_TRUE(sn2.verifySignature(msg1, sn1.walletAddress(), sign1));
    EXPECT_FALSE(sn2.verifySignature(msg1, sn1.walletAddress(), sign2));
    EXPECT_FALSE(sn2.verifySignature(msg2, sn1.walletAddress(), sign1));
    EXPECT_TRUE(sn2.verifySignature(msg2, sn1.walletAddress(), sign2));
}

struct FullSupernodeListTest : public ::testing::Test
{
    FullSupernodeListTest()
    {
        mlog_configure("", true);
        mlog_set_log_level(1);
    }
};


TEST_F(FullSupernodeListTest, basic)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    const std::string wallet_path1 = "test_supernode1";
    const std::string wallet_path2 = "test_supernode2";
    const std::string daemon_addr = "localhost:28881";
    const bool testnet = true;

    FullSupernodeList sn_list;

    Supernode sn1(wallet_path1, "", daemon_addr, testnet);
    sn1.refresh();

    // create view only supernode
    crypto::secret_key viewkey = sn1.exportViewkey();
    std::vector<Supernode::KeyImage> key_images;
    sn1.exportKeyImages(key_images);

    boost::filesystem::path temp_path = boost::filesystem::unique_path();
    Supernode * sn1_viewonly = Supernode::createFromViewOnlyWallet(temp_path.native(), sn1.walletAddress(), viewkey, testnet);
    LOG_PRINT_L0("temp wallet path: " << temp_path.native());

    sn1_viewonly->setDaemonAddress(daemon_addr);
    sn1_viewonly->refresh();

    EXPECT_TRUE(sn_list.add(sn1_viewonly));
    EXPECT_EQ(sn_list.size(), 1);

    EXPECT_TRUE(sn_list.exists(sn1_viewonly->walletAddress()));
    FullSupernodeList::SupernodePtr sn1_viewonly_ptr = sn_list.get(sn1_viewonly->walletAddress());

    EXPECT_TRUE(sn1_viewonly_ptr.get() != nullptr);
    EXPECT_FALSE(sn_list.exists("123213123123"));

    EXPECT_TRUE(sn_list.update(sn1.walletAddress(), key_images));
    EXPECT_TRUE(sn1_viewonly_ptr->stakeAmount() == sn1.stakeAmount());

    EXPECT_FALSE(sn_list.remove("123213"));
    EXPECT_TRUE(sn_list.remove(sn1.walletAddress()));

    EXPECT_TRUE(sn_list.size() == 0);
}
