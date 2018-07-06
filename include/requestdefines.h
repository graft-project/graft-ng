#ifndef REQUESTDEFINES_H
#define REQUESTDEFINES_H

#include "graft_constants.h"
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
#define ERROR_ADDRESS_INVALID               -32052
#define ERROR_SALE_REQUEST_FAILED           -32060
#define ERROR_RTA_COMPLETED                 -32070
#define ERROR_RTA_FAILED                    -32071

static const std::string MESSAGE_AMOUNT_INVALID("Amount is invalid.");
static const std::string MESSAGE_PAYMENT_ID_INVALID("Payment ID is invalid.");
static const std::string MESSAGE_SALE_REQUEST_FAILED("Sale request is failed.");
static const std::string MESSAGE_RTA_COMPLETED("Payment is already completed.");
static const std::string MESSAGE_RTA_FAILED("Payment is already failed.");
static const std::string MESSAGE_ADDRESS_INVALID("Address in invalid.");

//Context Keys
static const std::string CONTEXT_KEY_SALE_DETAILS(":saledetails");
static const std::string CONTEXT_KEY_SALE(":sale");
static const std::string CONTEXT_KEY_STATUS(":status");
static const std::string CONTEXT_KEY_PAY(":pay");
static const std::string CONTEXT_KEY_SUPERNODE("supernode");
static const std::string CONTEXT_KEY_FULLSUPERNODELIST("fsl");

namespace graft {

Status errorInvalidPaymentID(Output &output);
Status errorInvalidParams(Output &output);
Status errorInvalidAmount(Output &output);
bool errorFinishedPayment(int status, Output &output);

enum class RTAStatus : int
{
    None = 0,
    Waiting=1,
    InProgress=2,
    Success=3,
    Fail=4,
    RejectedByWallet=5,
    RejectedByPOS=6
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

struct PayData
{
    PayData(const std::string &address, const uint64_t &blockNumber,
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
