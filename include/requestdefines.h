#ifndef REQUESTDEFINES_H
#define REQUESTDEFINES_H

#include "graft_constants.h"
#include "inout.h"
#include "rta/supernode.h"
#include "router.h"

#include <chrono>

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
#define ERROR_RTA_SIGNATURE_FAILED          -32080
#define ERROR_TRANSACTION_INVALID           -32090

static const std::string MESSAGE_AMOUNT_INVALID("Amount is invalid.");
static const std::string MESSAGE_PAYMENT_ID_INVALID("Payment ID is invalid.");
static const std::string MESSAGE_SALE_REQUEST_FAILED("Sale request is failed.");
static const std::string MESSAGE_RTA_COMPLETED("Payment is already completed.");
static const std::string MESSAGE_RTA_FAILED("Payment is already failed.");
static const std::string MESSAGE_ADDRESS_INVALID("Address in invalid.");
static const std::string MESSAGE_RTA_CANT_BUILD_AUTH_SAMPLE("Can't build auth sample.");
static const std::string MESSAGE_INVALID_TRANSACTION("Can't parse transaction");

//Context Keys
static const std::string CONTEXT_KEY_SALE_DETAILS(":saledetails");
static const std::string CONTEXT_KEY_SALE(":sale");
static const std::string CONTEXT_KEY_STATUS(":status");
static const std::string CONTEXT_KEY_PAY(":pay");
static const std::string CONTEXT_KEY_SUPERNODE("supernode");
static const std::string CONTEXT_KEY_FULLSUPERNODELIST("fsl");
// key to maintain auth responses from supernodes for given tx id
static const std::string CONTEXT_KEY_AUTH_RESULT_BY_TXID(":tx_id_to_auth_resp");
// key to map tx_id -> payment_id
static const std::string CONTEXT_KEY_PAYMENT_ID_BY_TXID(":tx_id_to_payment_id");
// key to map tx_id -> tx
static const std::string CONTEXT_KEY_TX_BY_TXID(":tx_id_to_tx");
// key to store tx id in local context
static const std::string CONTEXT_TX_ID("tx_id");
// key to map tx_id -> amount
static const std::string CONTEXT_KEY_AMOUNT_BY_TX_ID(":tx_id_to_amount");

// key to store sale_details response coming from callback
static const std::string CONTEXT_SALE_DETAILS_RESULT(":sale_details");

static const double AUTHSAMPLE_FEE_PERCENTAGE = 0.5;


namespace graft {

static const std::chrono::seconds RTA_TX_TTL(60);


class Context;

Status errorInvalidPaymentID(Output &output);
Status errorInvalidParams(Output &output);
Status errorInvalidAmount(Output &output);
Status errorInvalidAddress(Output &output);
Status errorBuildAuthSample(Output &output);
Status errorInvalidTransaction(const std::string &tx_data, Output &output);
Status errorInternalError(const std::string &message, Output &output);
Status errorCustomError(const std::string &message, int code, Output &output);

Status sendOkResponseToCryptonode(Output &output);


bool errorFinishedPayment(int status, Output &output);
/*!
 * \brief cleanPaySaleData - cleans any data associated with given payment id
 * \param payment_id       - payment-id
 * \param ctx              - context
 */
void cleanPaySaleData(const std::string &payment_id, graft::Context &ctx);


enum class RTAStatus : int
{
    None = 0,
    Waiting = 1,
    InProgress = 2,
    Success = 3,
    Fail = 4,
    RejectedByWallet = 5,
    RejectedByPOS = 6
};

/*!
 * \brief isFiniteRtaStatus - checks if given rta status is finite, i.e. no transition possible for given status
 * \param status            - input status
 * \return                  - true if status finite
 */
bool isFiniteRtaStatus(RTAStatus status);

/*!
 * \brief The RTAAuthResult enum - result of RTA TX verification
 */
enum class RTAAuthResult : int
{
    Approved = 0,
    Rejected = 1,
    Invalid  = 3
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
    SaleData() = default;

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

/*!
 * \brief broadcastSaleStatus -  sale (pay) status helper
 * \return
 */
void buildBroadcastSaleStatusOutput(const std::string &payment_id, int status, const SupernodePtr &supernode, Output &output);


template<typename Response>
Status storeRequestAndReplyOk(const Router::vars_t& vars, const graft::Input& input,
                            graft::Context& ctx, graft::Output& output) noexcept
{
    // store input in local ctx.
    ctx.local["request"] = input.data();

    Response response;
    response.result.Status = STATUS_OK;
    output.load(response);
    return Status::Again;
}



}

#endif // REQUESTDEFINES_H
