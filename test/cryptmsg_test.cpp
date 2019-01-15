#include <gtest/gtest.h>
#include "lib/graft/cryptmsg.h"

TEST(Cryptmsg, common)
{
    using namespace crypto;

    std::vector<public_key> vec_B;
    std::vector<secret_key> vec_b;
    for(int i = 0; i < 1; ++i)
    {
        public_key B; secret_key b;
        generate_keys(B,b);
        vec_B.emplace_back(std::move(B)); vec_b.emplace_back(std::move(b));
    }

    std::string data = "12345qwertasdfgzxcvb";
    std::string message;
    graft::cryptmsg::encryptMessage(data, &vec_B[0], vec_B.size(), message);

    for(const auto& b : vec_b)
    {
        std::string plain;
        bool res = graft::cryptmsg::decryptMessage(message, b, plain);
        EXPECT_EQ(res, true);
        EXPECT_EQ(plain, data);
    }
}
