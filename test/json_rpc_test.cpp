#include <string>
#include <gtest/gtest.h>
#include <inout.h>
#include <jsonrpc.h>

GRAFT_DEFINE_IO_STRUCT(Payment,
     (uint64, amount),
     (uint32, block_height),
     (std::string, payment_id),
     (std::string, tx_hash),
     (uint32, unlock_time)
 );




TEST(JsonRPCFormat, common)
{
    using namespace graft;
    Payment p;
    p.amount = 1;
    p.block_height = 1;
    p.payment_id = "123";
    std::vector<Payment> params = {p};

    GRAFT_DEFINE_JSON_RPC_REQUEST(JsonRPCRequest, Payment);
    JsonRPCRequest jreq;
    initJsonRpcRequest(jreq, 1, "hello", params);
    std::cout << jreq.toJson().GetString() << std::endl;

    GRAFT_DEFINE_JSON_RPC_RESPONSE(JsonRPCResponse, Payment);

    std::string json_rpc_error     = " {\"json\":\"\",\"id\":3355185,\"error\":{\"code\":123,\"message\":\"Error Message\"}}";
    std::string json_rpc_response  = " {\"json\":\"\",\"id\":3355185,\"result\":{\"amount\":0 'aaaa',\"block_height\":3581286912,\"payment_id\":\"\",\"tx_hash\":\"\",\"unlock_time\":1217885840}}";
    // JsonRPCResponse jresp;
    // std::cout << jresp.toJson().GetString() << std::endl;
    Input in_err; in_err.load(json_rpc_error);
    Input in_result; in_result.load(json_rpc_response);

//    try {
//        std::cout << "Parsing: " << json_rpc_error << std::endl;
//        JsonRPCResponse jr_err = in_err.get<JsonRPCResponse>();
//        std::cout << "jr_err: result.unlock_time:  " <<  jr_err.result.unlock_time << std::endl;
//        std::cout << "jr_err: " << jr_err.error.message << std::endl;
//    } catch (std::exception &e) {
//        std::cerr << e.what() << std::endl;
//    }

    try {
        std::cout << "Trying to parse: " << json_rpc_response << std::endl;
        JsonRPCResponse jr_result = in_result.get<JsonRPCResponse>();
        std::cout << "jr_result: error_code:  " <<   jr_result.error.code << std::endl;
//        std::cout << "jr_result: " <<   jr_result.toJson().GetString() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception thrown...\n";
    }

}


