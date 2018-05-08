#include <string>
#include <gtest/gtest.h>
#include <inout.h>

GRAFT_DEFINE_IO_STRUCT(Payment,
     (uint64, amount),
     (uint32, block_height),
     (std::string, payment_id),
     (std::string, tx_hash),
     (uint32, unlock_time)
 );

// TODO: how to initialize method name and id from macro

#define GRAFT_DEFINE_JSON_RPC_REQUEST(Name, Param, Method) \
    GRAFT_DEFINE_IO_STRUCT(Name,          \
        (std::string,         json),      \
        (std::string,         method),    \
        (uint64_t,            id),        \
        (std::vector<Param>,  params)     \
    );

struct JsonError
{
    uint64_t code;
    std::string message;
};

// TODO: how to have optional error and result in 'template' way
#define GRAFT_DEFINE_JSON_RPC_RESPONSE(Name, Result, Method) \
    GRAFT_DEFINE_IO_STRUCT(Name,          \
        (std::string,         json),      \
        (uint64_t,            id),        \
        (JsonError,           error),     \
        (Result,              result)     \
    );


template <typename T>
struct JsonRPCRequest : public ReflectiveRapidJSON::JsonSerializable<JsonRPCRequest<T>>
{
    std::string method;
    const std::string json = "2.0";
    std::vector<T> params;
    uint64_t id;
};

#define GRAFT_DEFINE_JSON_RPC_REQUEST2(Name, Param, Method) \
    using Name = JsonRPCRequest<Param>; \
    BOOST_HANA_ADAPT_STRUCT(Name,       \
        method,                         \
        json,                           \
        params,                         \
        id);


//using JsonRPCPayment = JsonRPCRequest<Payment>;
//BOOST_HANA_ADAPT_STRUCT(JsonRPCPayment,
//                        method,
//                        json,
//                        params);

GRAFT_DEFINE_JSON_RPC_REQUEST2(JsonRPCPayment2, Payment, "")

TEST(JsonRPCFormat, common)
{
    using namespace graft;
    GRAFT_DEFINE_JSON_RPC_REQUEST(JsonRPCPayment, Payment, "Dummy");

    Payment p;
    p.amount = 1;
    p.block_height = 1;
    p.payment_id = "123";
    //std::cout << p.toJson().GetString() << std::endl;

    JsonRPCPayment jp;
    jp.params.push_back(p);
    JsonRPCPayment2 jp2;
    jp2.params.push_back(p);

    std::cout << jp.toJson().GetString() << std::endl;
    std::cout << jp2.toJson().GetString() << std::endl;

}


