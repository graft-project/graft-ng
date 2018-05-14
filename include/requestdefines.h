#ifndef REQUESTDEFINES_H
#define REQUESTDEFINES_H

#include "inout.h"

GRAFT_DEFINE_IO_STRUCT(ErrorResponse,
    (int64, code),
    (std::string, message)
);

#define ERROR_MESSAGE(message) std::string(__FUNCTION__) + std::string(": ") + message
#define EXTENDED_ERROR_MESSAGE(message, reason) \
    std::string(__FUNCTION__) + std::string(": ") + message + std::string(" Reason: ") + reason

#define STATUS_OK                           0

//Standart JSON-RPC 2.0 Errors
#define ERROR_PARSE_ERROR                   -32700
#define ERROR_INVALID_REQUEST               -32600
#define ERROR_METHOD_NOT_FOUND              -32601
#define ERROR_INVALID_PARAMS                -32602
#define ERROR_INTERNAL_ERROR                -32603

static const std::string MESSAGE_INVALID_PARAMS("The request parameters are invalid.");
static const std::string MESSAGE_INTERNAL_ERROR("Internal server error.");

//RTA DAPI Errors
#define ERROR_AMOUNT_INVALID                -32050
#define ERROR_PAYMENT_ID_INVALID            -32051
#define ERROR_SALE_REQUEST_FAILED           -32060

static const std::string MESSAGE_AMOUNT_INVALID("Amount is invalid.");
static const std::string MESSAGE_PAYMENT_ID_INVALID("Payment ID is invalid.");
static const std::string MESSAGE_SALE_REQUEST_FAILED("Sale request is failed.");

//Context Keys
static const std::string CONTEXT_KEY_SALE_DETAILS(":saledetails");
static const std::string CONTEXT_KEY_SALE(":sale");
static const std::string CONTEXT_KEY_SALE_STATUS(":salestatus");

namespace graft {

enum class RTAStatus : int
{
    None = 0,
    InProgress=1,
    Success=2,
    Fail=3,
    RejectedByWallet=4,
    RejectedByPOS=5
};

struct SaleData
{
    SaleData(const std::string &address, const uint64_t &blockNumber,
             const uint64_t &amount)
        : Address(address)
        ,BlockNumber(blockNumber)
        ,Amount(amount)
    {
    }

    std::string Address;
    uint64_t BlockNumber;
    uint64_t Amount;
};

}

#endif // REQUESTDEFINES_H
