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
#include "lib/graft/thread_pool/thread_pool.hpp"


// cryptonode includes

#include "supernode/requests/send_supernode_announce.h"
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
        mlog_set_log_level(2);
    }
};

#if 0
TEST_F(SupernodeTest, open)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    const std::string wallet_path = "supernode_tier1_1";
    const std::string daemon_addr = "localhost:28881";
    const bool testnet = true;


    Supernode sn1(wallet_path, "", daemon_addr, testnet);
    sn1.refresh();
    EXPECT_TRUE(sn1.walletAddress() == "F4xWex5prppRooAqBZ7aBxTCRsPrzvKhGhWiy41Zt4DVX6iTk1HJqZiPNcgW4NkhE77mF7gRkYLRQhGKEG1rAs8NSp7aU93");
    EXPECT_TRUE(sn1.stakeAmount() > 0);
}

TEST_F(SupernodeTest, busy)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    const std::string wallet_path = "supernode_tier1_1";
    const std::string daemon_addr = "localhost:28881";
    const bool testnet = true;
    Supernode sn1(wallet_path, "", daemon_addr, testnet);
    EXPECT_FALSE(sn1.busy());

    std::future<bool> f = std::async(std::launch::async, [&] () {
        return sn1.refresh();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(sn1.busy());
    f.wait();
    EXPECT_FALSE(sn1.busy());
}


TEST_F(SupernodeTest, watchOnly)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    const std::string wallet_path = "supernode_tier1_1";
    const std::string daemon_addr = "localhost:28881";
    const bool testnet = true;

    Supernode sn1(wallet_path, "", daemon_addr, testnet);
    sn1.refresh();

    // create view only supernode
    crypto::secret_key viewkey = sn1.exportViewkey();
    std::vector<Supernode::SignedKeyImage> key_images;
    sn1.exportKeyImages(key_images);

    boost::filesystem::path temp_path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    Supernode * sn2 = Supernode::createFromViewOnlyWallet(temp_path.native(), sn1.walletAddress(), viewkey, testnet);
    LOG_PRINT_L0("temp wallet path: " << temp_path.native());
    ASSERT_TRUE(sn2 != nullptr);

    sn2->setDaemonAddress(daemon_addr);
    sn2->refresh();
    uint64_t height = 0;
    sn2->importKeyImages(key_images, height);


    EXPECT_TRUE(sn2->walletAddress() == sn1.walletAddress());
    EXPECT_TRUE(sn2->stakeAmount() == sn1.stakeAmount());

    delete sn2;

    // watch-only wallet
    Supernode sn3(temp_path.native(), "", daemon_addr, testnet);


    std::string msg = "123";
    crypto::signature sign;
    EXPECT_FALSE(sn3.signMessage(msg, sign));

}


TEST_F(SupernodeTest, signAndVerify)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    const std::string wallet_path1 = "supernode_tier1_1";
    const std::string wallet_path2 = "supernode_tier1_2";
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
        mlog_set_log_level(2);
    }
};


TEST_F(FullSupernodeListTest, basic)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    const std::string wallet_path1 = "supernode_tier1_1";
    const std::string wallet_path2 = "supernode_tier1_2";

    const std::string daemon_addr = "localhost:28881";
    const bool testnet = true;

    FullSupernodeList sn_list(daemon_addr, true);

    Supernode sn1(wallet_path1, "", daemon_addr, testnet);
    sn1.refresh();

    // create view only supernode
    crypto::secret_key viewkey = sn1.exportViewkey();
    std::vector<Supernode::SignedKeyImage> key_images;
    sn1.exportKeyImages(key_images);

    boost::filesystem::path temp_path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    Supernode * sn1_viewonly = Supernode::createFromViewOnlyWallet(temp_path.native(), sn1.walletAddress(), viewkey, testnet);
    LOG_PRINT_L0("temp wallet path: " << temp_path.native());

    sn1_viewonly->setDaemonAddress(daemon_addr);
    sn1_viewonly->refresh();

    EXPECT_TRUE(sn_list.add(sn1_viewonly));
    EXPECT_EQ(sn_list.size(), 1);

    EXPECT_TRUE(sn_list.exists(sn1_viewonly->walletAddress()));
    SupernodePtr sn1_viewonly_ptr = sn_list.get(sn1_viewonly->walletAddress());

    EXPECT_TRUE(sn1_viewonly_ptr.get() != nullptr);
    EXPECT_FALSE(sn_list.exists("123213123123"));

    EXPECT_TRUE(sn_list.update(sn1.walletAddress(), key_images));
    EXPECT_TRUE(sn1_viewonly_ptr->stakeAmount() == sn1.stakeAmount());

    EXPECT_FALSE(sn_list.remove("123213"));
    EXPECT_TRUE(sn_list.remove(sn1.walletAddress()));

    EXPECT_TRUE(sn_list.size() == 0);
}


TEST_F(FullSupernodeListTest, loadFromDir)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    const std::string daemon_addr = "localhost:28881";
    const bool testnet = true;

    FullSupernodeList sn_list(daemon_addr, testnet);
    size_t loadedItems = sn_list.loadFromDir(".");
    EXPECT_EQ(loadedItems, sn_list.items().size());
    LOG_PRINT_L0("loaded: " << loadedItems << " items");
}

TEST_F(FullSupernodeListTest, loadFromDir2)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    const std::string daemon_addr = "localhost:28881";
    const bool testnet = true;
    size_t found_wallets = 0;
    FullSupernodeList sn_list(daemon_addr, testnet);
    size_t loadedItems = sn_list.loadFromDirThreaded(".", found_wallets);
    LOG_PRINT_L0(" !!! loaded: " << loadedItems << " items");
    EXPECT_EQ(loadedItems, sn_list.items().size());
    EXPECT_EQ(found_wallets, loadedItems);
}

TEST_F(FullSupernodeListTest, refreshAsync)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    const std::string daemon_addr = "localhost:28881";
    const bool testnet = true;
    size_t found_wallets = 0;
    FullSupernodeList sn_list(daemon_addr, testnet);
    size_t loadedItems = sn_list.loadFromDirThreaded(".", found_wallets);
    LOG_PRINT_L0(" !!! loaded: " << loadedItems << " items");
    MGINFO_YELLOW("*** ALL WALLETS LOADED, REFRESHING ***");

    std::future<void> f = sn_list.refreshAsync();
    MGINFO_YELLOW("*** REFRESH STARTED, WAITING ***");
    f.wait();
    MGINFO_YELLOW("*** ALL WALLETS REFRESHED ***");
    EXPECT_EQ(loadedItems, sn_list.refreshedItems());
}

template<typename T>
void print_container(std::ostream& os, const T& container, const std::string& delimiter)
{
    std::copy(std::begin(container),
              std::end(container),
              std::ostream_iterator<typename T::value_type>(os, delimiter.c_str()));
    os << std::endl;
}


TEST_F(FullSupernodeListTest, buildAuthSample)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    // currently test works against public testnet
    const std::string daemon_addr = "localhost:28881";
    const bool testnet = true;

    FullSupernodeList sn_list(daemon_addr, testnet);
    size_t foundItems = 0;
    size_t loadedItems = sn_list.loadFromDirThreaded(".", foundItems);

    ASSERT_TRUE(loadedItems > 0);

    // update lastUpdated
    for (const std::string &addr : sn_list.items()) {
      sn_list.get(addr)->setLastUpdateTime(std::time(nullptr));
    }

    std::string hash_str;
    sn_list.getBlockHash(2, hash_str);

    std::vector<std::string> tier1_addresses = {
        "F4xWex5prppRooAqBZ7aBxTCRsPrzvKhGhWiy41Zt4DVX6iTk1HJqZiPNcgW4NkhE77mF7gRkYLRQhGKEG1rAs8NSp7aU93",
        "FBHdqDnz8YNU3TscB1X7eH25XLKaHmba9Vws2xwDtLKJWS7vZrii1vHVKAMGgRVz6WVcd2jN7qC1YEB4ZALUag2d9h1nnEu",
        "F5pbAMAzbRbhQSpEQ39nHXQr8QXMHXMmbWbMHcCh9HLrez7Y3aAckkH3PeG1Lctyr24ZZex72DKqgR5EFXJeukoo3mxvXZh",
        "F8fNpx9sz6o3dxT5wDPReKP2LDA14J85jD9wUKdVEzwJPbpZwB8eZvYd97iwNfa4epNAPZYDngcNBZcNHxgoGMdXSwi8n7a",
    };

    std::vector<std::string> tier2_addresses = {
        "F3vUsEKRUiTTpVrAAoTmSm61JVUR3P858FAzAFavH7McNa5jKbjBveHDroH33bb4N3Nu6z42n8Y9fQXfiNPvT2Yn8gAaLtv",
        "F5CfDKxtW5ciHCkqK4p6cK2CymvtVq9Wce8fpo1u5WZLbLbvMFHauvsdCj2xeUTqiE4J4cpaEdsn29fA4RpWJssRV7P6ZUo",
        "FC8kKPRQx3uCYN9UrdUee9AjF5ctSSFdefefWxcsFAXhjN1Gq8r8EDPhVoMfUU29JvbqRzhEKmfAfgRiwjcmwYHbTy7u3Th",
        "F4H2PxxHkxj9HDs3fMdk3k4EBDSSBJJdzhsAKTeGrjzSinVHdiTFWgg36wGzQmGYagRtpP76EYGkWN4VV8o6XhJtFABs4eE"
    };

    std::vector<std::string> tier3_addresses = {
        "FAyX1kJwtUSdhJ72A43newLeuPk7Rm75sCvdwhnJJMug9x2CzuGhAjB1nXtfgUWZWhTWebJir4T2Ffb1NPJFctXVGBzzsph",
        "F9XxcuGqBuej38dxBcdhzwHkRJM1xdpgnbDyEsAxjELmL4ucM7Kcxub7Ytn6cHzvPe5tcf9FpPPji9UFeN3NVCczSi6B2mK",
        "FBJjByj4JznQNhMLEaF4A2FsJj4snnPkkfhxbLBhPuyoGKcigSiycWpfiGtF6gPrrLZN9uQPYCRaC9Yu43mZXntxE6K1hvn",
        "F8BBj21k8425DNS2bMKfogSxT16Jxw9dPCoMHjNzQFoiYgeahufZ1mARRHBP8crtng4sNa2cB1i54KE4Xhs65WufAM8fTRp"
    };

    std::vector<std::string> tier4_addresses = {
        "FAemK2QsWwsDAxgCKsTJUbhk1XwAu1eg4eVkYNbYSkQzLP8wobvgG7ia1tXcpSY6k4f7rFmypq6wHKT4fYJJ3XFL1KRgNrj",
        "F865TCzRThmY9KMGsDGq7ZMKa5QubVm4ah9UocfiVS5CfcXCM9cRu6m9rgubfsTMsSHLmX98zNzW5TUF5tcBCcWeNKrTZPq",
        "FCz3wRQnmKBFU2fjH2PPuhiTd8Ys77uE2h1RPLUybwi7MPCHkTEz1ep3NnQQz146LGUejkf9L32Y29YUwxkyUsDiMsseqTh",
        "FBjcwvA2NBeYVbWLR6dJ9i442JrP9GeJkFCQaV7mZnA7KCWtnWPTZK1Gj616pPLxnHCgseqFHge144pEqaMjdXrcG2J1jNZ"
    };

    std::sort(tier1_addresses.begin(), tier1_addresses.end());
    std::sort(tier2_addresses.begin(), tier2_addresses.end());
    std::sort(tier3_addresses.begin(), tier3_addresses.end());
    std::sort(tier4_addresses.begin(), tier4_addresses.end());




    crypto::hash h;
    epee::string_tools::hex_to_pod(hash_str, h);

    std::vector<SupernodePtr> auth_sample;
    std::vector<SupernodePtr> auth_sample2;


    // test if result is reproducable
    for (size_t i = 0; i < 100; ++i) {

        sn_list.buildAuthSample(10000 + i, "aabbccddeeff", auth_sample);
        sn_list.buildAuthSample(10000 + i, "aabbccddeeff", auth_sample2);
        ASSERT_EQ(auth_sample.size(), FullSupernodeList::AUTH_SAMPLE_SIZE);
        ASSERT_EQ(auth_sample2.size(), FullSupernodeList::AUTH_SAMPLE_SIZE);

        std::vector<std::string> auth_sample1_addresses, auth_sample2_addresses;

        for (const auto & it: auth_sample) {
            auth_sample1_addresses.push_back(it->walletAddress());
        }
        for (const auto & it: auth_sample2) {
            auth_sample2_addresses.push_back(it->walletAddress());
        }

        EXPECT_TRUE(auth_sample1_addresses == auth_sample2_addresses);

        std::set<std::string> s1(auth_sample1_addresses.begin(), auth_sample1_addresses.end());
        EXPECT_TRUE(s1.size() == FullSupernodeList::AUTH_SAMPLE_SIZE);
    }



    std::vector<std::string> tier1_as_addresses, tier2_as_addresses, tier3_as_addresses, tier4_as_addresses;

    for (const auto & it: auth_sample) {
        if (it->stakeAmount() >= Supernode::TIER1_STAKE_AMOUNT && it->stakeAmount() < Supernode::TIER2_STAKE_AMOUNT)
            tier1_as_addresses.push_back(it->walletAddress());
        else if (it->stakeAmount() >= Supernode::TIER2_STAKE_AMOUNT && it->stakeAmount() < Supernode::TIER3_STAKE_AMOUNT)
            tier2_as_addresses.push_back(it->walletAddress());
        else if (it->stakeAmount() >= Supernode::TIER3_STAKE_AMOUNT && it->stakeAmount() < Supernode::TIER4_STAKE_AMOUNT)
            tier3_as_addresses.push_back(it->walletAddress());
        else if (it->stakeAmount() >= Supernode::TIER4_STAKE_AMOUNT)
            tier4_as_addresses.push_back(it->walletAddress());
    }

    EXPECT_EQ(tier1_as_addresses.size(), FullSupernodeList::ITEMS_PER_TIER);
    EXPECT_EQ(tier2_as_addresses.size(), FullSupernodeList::ITEMS_PER_TIER);
    EXPECT_EQ(tier3_as_addresses.size(), FullSupernodeList::ITEMS_PER_TIER);
    EXPECT_EQ(tier4_as_addresses.size(), FullSupernodeList::ITEMS_PER_TIER);

    // make sure we have correct addresses in auth sample;
    std::sort(tier1_as_addresses.begin(), tier1_as_addresses.end());
    std::sort(tier2_as_addresses.begin(), tier2_as_addresses.end());
    std::sort(tier3_as_addresses.begin(), tier3_as_addresses.end());
    std::sort(tier4_as_addresses.begin(), tier4_as_addresses.end());

    std::vector<std::string> tier1_intersection, tier2_intersection, tier3_intersection, tier4_intersection;

    std::set_intersection(tier1_addresses.begin(), tier1_addresses.end(),
                          tier1_as_addresses.begin(), tier1_as_addresses.end(),
                          std::back_inserter(tier1_intersection));
//  std::cout << "tier1 supernodes: " << std::endl;
//  print_container(std::cout, tier1_intersection, ", \n");

    EXPECT_TRUE(tier1_as_addresses == tier1_intersection);

    std::set_intersection(tier2_addresses.begin(), tier2_addresses.end(),
                          tier2_as_addresses.begin(), tier2_as_addresses.end(),
                          std::back_inserter(tier2_intersection));
//    std::cout << "tier2 supernodes: " << std::endl;
//    print_container(std::cout, tier2_intersection, ", \n");

    EXPECT_TRUE(tier2_as_addresses == tier2_intersection);



    std::set_intersection(tier3_addresses.begin(), tier3_addresses.end(),
                          tier3_as_addresses.begin(), tier3_as_addresses.end(),
                          std::back_inserter(tier3_intersection));
//    std::cout << "tier3 supernodes: " << std::endl;
//    print_container(std::cout, tier3_intersection, ", \n");
    EXPECT_TRUE(tier3_as_addresses == tier3_intersection);

    std::set_intersection(tier4_addresses.begin(), tier4_addresses.end(),
                          tier4_as_addresses.begin(), tier4_as_addresses.end(),
                          std::back_inserter(tier4_intersection));
//    std::cout << "tier4 supernodes: " << std::endl;
//    print_container(std::cout, tier4_as_addresses, ", \n");
//    print_container(std::cout, tier4_intersection, ", \n");
//    print_container(std::cout, tier4_addresses, ", \n");

    EXPECT_TRUE(tier4_as_addresses == tier4_intersection);
    EXPECT_FALSE(tier4_as_addresses == tier2_intersection);
 }


TEST_F(FullSupernodeListTest, noFalseBalance)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    const std::string daemon_addr = "localhost:28881";
    constexpr bool testnet = true;

    // This wallet has a tiny initial incoming transaction (0.85 GRFT) follows by an incoming
    // transaction of the required stake (250K), followed by a withdrawal of most of that stake,
    // leaving 1.7761855320 (the unspent 0.85 plus some dust from the transferred-out 250K) as the
    // actual wallet balance.  A view-only wallet without key images or with only the key image for
    // the unspent 0.85 will see a balance of 250001.7761855320 instead.
    const std::string wallet_path{"supernode_fake_tier4_1"};

    FullSupernodeList sn_list(daemon_addr, testnet);

    Supernode sn(wallet_path, "", daemon_addr, testnet);
    sn.refresh();
    EXPECT_EQ(sn.stakeAmount(), 17761855320ull);

    // create view only supernode using just the first key image
    crypto::secret_key viewkey = sn.exportViewkey();
    std::vector<Supernode::SignedKeyImage> key_images;
    sn.exportKeyImages(key_images);
    EXPECT_EQ(key_images.size(), 3);
    std::vector<Supernode::SignedKeyImage> first_key_image(1, key_images.front());

    boost::filesystem::path temp_path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    Supernode * sn_viewonly = Supernode::createFromViewOnlyWallet(temp_path.native(), sn.walletAddress(), viewkey, testnet);
    LOG_PRINT_L0("temp wallet path: " << temp_path.native());

    sn_viewonly->setDaemonAddress(daemon_addr);
    sn_viewonly->refresh();

    EXPECT_TRUE(sn_list.add(sn_viewonly));
    EXPECT_EQ(sn_list.size(), 1);

    EXPECT_TRUE(sn_list.update(sn.walletAddress(), first_key_image));
    EXPECT_EQ(sn_viewonly->walletBalance(), 2500017761855320ull);
    EXPECT_EQ(sn_viewonly->stakeAmount(), 8500000000ull);

    // Now go ahead with the full set of key images, which should make the watch-only wallet
    // accurate:
    EXPECT_TRUE(sn_list.update(sn.walletAddress(), key_images));
    EXPECT_EQ(sn_viewonly->walletBalance(), sn_viewonly->stakeAmount());
    EXPECT_EQ(sn_viewonly->stakeAmount(), sn.stakeAmount());
}


TEST_F(FullSupernodeListTest, getBlockHash)
{
    MGINFO_YELLOW("*** This test requires running cryptonode RPC on localhost:28881. If not running, test will fail ***");

    const std::string daemon_addr = "localhost:28881";
    const bool testnet = true;

    FullSupernodeList sn_list(daemon_addr, testnet);

    std::string hash;

    sn_list.getBlockHash(2, hash);

    EXPECT_EQ(hash, "49ba02f1a31f33ed707d5cbbb706aa02851c68d795cc916058abe5ea3f4afcbf");

}



TEST_F(FullSupernodeListTest, threadPool)
{
    tp::ThreadPool thread_pool;

    for (int i = 0; i < 10; ++i) {
        thread_pool.post([]() {
            LOG_PRINT_L0("worker thread starting") ;
            boost::this_thread::sleep(
                        boost::posix_time::milliseconds(1000)
                        );
            LOG_PRINT_L0("worker thread done");
        });
    }
}


TEST_F(FullSupernodeListTest, announce1)
{

    const std::string daemon_addr = "localhost:28881";
    const bool testnet = true;
    // create Supernode instance from existing wallet
    Supernode sn ("supernode_tier1_1", "", daemon_addr, testnet);

    sn.refresh();

    ASSERT_TRUE(sn.stakeAmount() > 0);


    boost::filesystem::path temp_path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();

    FullSupernodeList fsl(daemon_addr, testnet);

    graft::supernode::request::SupernodeAnnounce announce;
    ASSERT_TRUE(sn.prepareAnnounce(announce));


    SupernodePtr watch_only_sn1 {Supernode::createFromAnnounce(temp_path.string(), announce,
                                                                                 daemon_addr, testnet)};
    ASSERT_TRUE(watch_only_sn1.get() != nullptr);
    watch_only_sn1->refresh();
    EXPECT_EQ(watch_only_sn1->stakeAmount(), sn.stakeAmount());

    temp_path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();

    announce.secret_viewkey = "";

    SupernodePtr watch_only_sn2 {Supernode::createFromAnnounce(temp_path.string(), announce,
                                                                                 daemon_addr, testnet)};
    ASSERT_TRUE(watch_only_sn2.get() == nullptr);

}
#endif

