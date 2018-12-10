#include <gtest/gtest.h>
#include "lib/graft/blacklist.h"
#include <chrono>
#include <thread>
#include "fixture.h"

TEST(Blacklist, common)
{
    auto bl = graft::BlackListTest::Create(100, 5, 120);

    EXPECT_EQ( bl->find("1.1.1.1"), std::make_pair(false, true));
    EXPECT_EQ( bl->find("1.1.1.2"), std::make_pair(false, true));

    std::istringstream iss(" allow 1.1.1.0/24 ;; aaa ;; aaa \n deny\t 1.1.2.0/24 ;; bla-bla allow \n \n ;;ththth\r\n;;thth;;th \r\n\r\n allow 1.1.3.3 \n deny all");
    bl->readRules(iss);

    EXPECT_EQ( bl->find("1.1.1.1"), std::make_pair(true, true));
    EXPECT_EQ( bl->find("1.1.1.2"), std::make_pair(true, true));
    EXPECT_EQ( bl->find("1.1.2.0"), std::make_pair(true, false));
    EXPECT_EQ( bl->find("1.1.2.2"), std::make_pair(true, false));
    EXPECT_EQ( bl->find("1.1.3.3"), std::make_pair(true, true));
    EXPECT_EQ( bl->find("1.1.3.4"), std::make_pair(false, false));

    bl->addEntry("10.16.10.1", 32, true);
    bl->addEntry("10.16.10.0", 24, false); //deny
    bl->addEntry("10.16.0.0",  16, true);
    bl->addEntry("10.16.12.7", 32, true); //allow
    bl->addEntry("10.16.12.6", 31, false); //deny
    bl->addEntry("10.16.12.4", 30, true); //allow

    //something not used
    bl->addEntry("172.18.0.0", 16, true);
    bl->addEntry("172.19.0.0", 16, false);
    bl->addEntry("192.168.1.0", 24, true);
    bl->addEntry("192.168.2.0", 24, false);
    bl->addEntry("192.168.3.0", 24, true);
    bl->addEntry("192.168.4.0", 24, false);


    EXPECT_EQ( bl->find("10.16.10.1"), std::make_pair(true, true));
    EXPECT_EQ( bl->find("10.16.10.2"), std::make_pair(true, false));
    EXPECT_EQ( bl->find("10.16.11.3"), std::make_pair(true, true));

    EXPECT_EQ( bl->find("10.16.12.7"), std::make_pair(true, true));
    EXPECT_EQ( bl->find("10.16.12.6"), std::make_pair(true, false));
    EXPECT_EQ( bl->find("10.16.12.5"), std::make_pair(true, true));
    EXPECT_EQ( bl->find("10.16.12.4"), std::make_pair(true, true));
    EXPECT_EQ( bl->find("10.16.12.3"), std::make_pair(true, true));
    EXPECT_EQ( bl->find("10.16.12.2"), std::make_pair(true, true));
    EXPECT_EQ( bl->find("10.16.12.1"), std::make_pair(true, true));
    EXPECT_EQ( bl->find("10.16.12.0"), std::make_pair(true, true));

    //single /32 entries
    EXPECT_EQ( bl->find("10.16.17.1"), std::make_pair(true, true));
    bl->addEntry("10.16.17.1", 32, false);
    EXPECT_EQ( bl->find("10.16.17.1"), std::make_pair(true, false));
    bl->removeEntry("10.16.17.1");
    EXPECT_EQ( bl->find("10.16.17.1"), std::make_pair(true, true));

    EXPECT_EQ( bl->find("10.16.12.5"), std::make_pair(true, true));
    bl->addEntry("10.16.12.5", 32, false);
    EXPECT_EQ( bl->find("10.16.12.5"), std::make_pair(true, false));
    bl->removeEntry("10.16.12.5");
    EXPECT_EQ( bl->find("10.16.12.5"), std::make_pair(true, true));
}

TEST(Blacklist, activity)
{
    //returns triggered, seconds, active calls count
    auto act_per_sec = [](graft::BlackListTest& bl, in_addr_t addr, std::vector<int>& vec) -> std::tuple<bool,int,int>
    {
        auto start = std::chrono::steady_clock::now();
        bool triggered = false;
        int cnt = 0, seconds = 0;
        for(auto& v : vec)
        {
            for(int i = 0; i < v; ++i)
            {
                ++cnt;
                triggered = bl.active(addr);
                if(triggered) break;
            }
            ++seconds;
            if(triggered) break;
            start += std::chrono::seconds(1);
            while(std::chrono::steady_clock::now() < start)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        return std::make_tuple(triggered, seconds, cnt);
    };

    auto bl = graft::BlackListTest::Create(100, 5, 120);
    //random IPs
    for(int i = 0; i < 20; ++i)
    {
        in_addr_t addr = 100+i*10;
        bl->active(addr);
    }

    {
        std::vector<int> vec{501,501,501,501,101,101,101,101,101,101};
        bool triggered; int seconds, cnt;
        std::tie(triggered, seconds, cnt) = act_per_sec(*bl, 1, vec);

        EXPECT_EQ(triggered, true);
        EXPECT_EQ(seconds, 1);
        EXPECT_EQ(cnt, 501);
        EXPECT_EQ(20, bl->activeCnt());
    }

    {
        //                   400 460 470 480 490 500  501
        std::vector<int> vec{400, 60, 10, 10, 10, 10, 101,1,1,1};
        bool triggered; int seconds, cnt;
        std::tie(triggered, seconds, cnt) = act_per_sec(*bl, 1, vec);

        EXPECT_EQ(triggered, true);
        EXPECT_EQ(seconds, 7);
        EXPECT_EQ(cnt, 601);
        EXPECT_EQ(20, bl->activeCnt());
    }

    {
        //                   400 460 470 480 490 500 410 320 230 140  50  10  501
        std::vector<int> vec{400, 60, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 491, 1,1,1};
        bool triggered; int seconds, cnt;
        std::tie(triggered, seconds, cnt) = act_per_sec(*bl, 1, vec);

        EXPECT_EQ(triggered, true);
        EXPECT_EQ(seconds, 13);
        EXPECT_EQ(cnt, 1051);
        EXPECT_EQ(0, bl->activeCnt()); //random IPs become old on 10th second
    }

    {
        //                   10 110 210 310 400 500 500 501
        std::vector<int> vec{10,100,100,100, 90,100,100,101,1,1};
        bool triggered; int seconds, cnt;
        std::tie(triggered, seconds, cnt) = act_per_sec(*bl, 1, vec);

        EXPECT_EQ(triggered, true);
        EXPECT_EQ(seconds, 8);
        EXPECT_EQ(cnt, 701);
        EXPECT_EQ(0, bl->activeCnt());
    }
}

TEST_F(GraftServerTest, ban)
{
    m_copts.ipfilter.requests_per_sec = 3;
    m_copts.ipfilter.window_size_sec = 1;
    m_copts.ipfilter.ban_ip_sec = 3;

    run();

    std::string json_data = "something";
    for(int i = 0; i < 10; ++i)
    {
        GraftServerTestBase::Client client;
        client.serve("http://127.0.0.1:28690/URI/test/123", "", json_data);
        if(i<3)
        {
            EXPECT_EQ(client.get_closed(), false);
            std::string res1 = client.get_body();
            EXPECT_EQ(res1, json_data+"123");
            continue;
        }
        if(i<6)
        {
            EXPECT_EQ(client.get_closed(), true);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        EXPECT_EQ(client.get_closed(), false);
        std::string res1 = client.get_body();
        EXPECT_EQ(res1, json_data+"123");
        if(i == 6) std::this_thread::sleep_for(std::chrono::seconds(1));
        if(6 <= i) std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }

    stop_and_wait_for();
}
