#include <gtest/gtest.h>
#include "lib/graft/blacklist.h"

TEST(Blacklist, common)
{
    auto bl = graft::BlackList::Create();

    EXPECT_EQ( bl->find("1.1.1.1").first, false);
    EXPECT_EQ( bl->find("1.1.1.1").second, true);
    EXPECT_EQ( bl->find("1.1.1.2").first, false);
    EXPECT_EQ( bl->find("1.1.1.2").second, true);

    std::istringstream iss(" allow 1.1.1.0/24 ;; aaa ;; aaa \n deny\t 1.1.2.0/24 ;; bla-bla allow \n \n ;;ththth\r\n;;thth;;th \r\n\r\n allow 1.1.3.3 \n deny all");
    bl->readRules(iss);

    EXPECT_EQ( bl->find("1.1.1.1").first, true);
    EXPECT_EQ( bl->find("1.1.1.1").second, true);
    EXPECT_EQ( bl->find("1.1.1.2").first, true);
    EXPECT_EQ( bl->find("1.1.1.2").second, true);
    EXPECT_EQ( bl->find("1.1.2.0").first, true);
    EXPECT_EQ( bl->find("1.1.2.0").second, false);
    EXPECT_EQ( bl->find("1.1.2.2").first, true);
    EXPECT_EQ( bl->find("1.1.2.2").second, false);
    EXPECT_EQ( bl->find("1.1.3.3").first, true);
    EXPECT_EQ( bl->find("1.1.3.3").second, true);
    EXPECT_EQ( bl->find("1.1.3.4").first, false);
    EXPECT_EQ( bl->find("1.1.3.4").second, false);

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


    EXPECT_EQ( bl->find("10.16.10.1").first, true);
    EXPECT_EQ( bl->find("10.16.10.1").second, true);
    EXPECT_EQ( bl->find("10.16.10.2").first, true);
    EXPECT_EQ( bl->find("10.16.10.2").second, false);
    EXPECT_EQ( bl->find("10.16.11.3").first, true);
    EXPECT_EQ( bl->find("10.16.11.3").second, true);

    EXPECT_EQ( bl->find("10.16.12.7").first, true);
    EXPECT_EQ( bl->find("10.16.12.7").second, true);
    EXPECT_EQ( bl->find("10.16.12.6").first, true);
    EXPECT_EQ( bl->find("10.16.12.6").second, false);
    EXPECT_EQ( bl->find("10.16.12.5").first, true);
    EXPECT_EQ( bl->find("10.16.12.5").second, true);
    EXPECT_EQ( bl->find("10.16.12.4").first, true);
    EXPECT_EQ( bl->find("10.16.12.4").second, true);
    EXPECT_EQ( bl->find("10.16.12.3").first, true);
    EXPECT_EQ( bl->find("10.16.12.3").second, true);
    EXPECT_EQ( bl->find("10.16.12.2").first, true);
    EXPECT_EQ( bl->find("10.16.12.2").second, true);
    EXPECT_EQ( bl->find("10.16.12.1").first, true);
    EXPECT_EQ( bl->find("10.16.12.1").second, true);
    EXPECT_EQ( bl->find("10.16.12.0").first, true);
    EXPECT_EQ( bl->find("10.16.12.0").second, true);

}
