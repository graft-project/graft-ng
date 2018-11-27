#pragma once

#include "lib/graft/router.h"
#include "lib/graft/jsonrpc.h"

namespace graft {

class Context;

namespace walletnode::request {

GRAFT_DEFINE_IO_STRUCT(WalletTransactionHistoryRequest,
    (std::string, WalletId),
    (std::string, Account),
    (std::string, Password)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletTransactionHistoryRequestJsonRpc, WalletTransactionHistoryRequest)

GRAFT_DEFINE_IO_STRUCT_INITED(WalletTransactionHistoryResponse,
    (int, Result, 0)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletTransactionHistoryResponseJsonRpc, WalletTransactionHistoryResponse)

GRAFT_DEFINE_IO_STRUCT_INITED(Transfer,
  (uint64_t,    Amount,   0),
  (std::string, Address,  std::string())
);

GRAFT_DEFINE_IO_STRUCT_INITED(TransactionInfo,
  (bool,                  DirectionOut, false),
  (bool,                  Pending,       false),
  (bool,                  Failed,        false),
  (uint64_t,              Amount,        0),
  (uint64_t,              Fee,           0),
  (uint64_t,              BlockHeight,   0),
  (std::string,           Hash,          std::string()),
  (std::time_t,           Timestamp,     0),
  (std::string,           PaymentId,     std::string()),
  (std::vector<Transfer>, Transfers,     std::vector<Transfer>()),
  (uint64_t,              Confirmations, 0),
  (uint64_t,              UnlockTime,   0)
);

GRAFT_DEFINE_IO_STRUCT(TransactionHistory,
  (std::vector<TransactionInfo>, Transactions)
);

GRAFT_DEFINE_IO_STRUCT(WalletTransactionHistoryCallbackRequest,
    (int,                Result),
    (TransactionHistory, History)
);

GRAFT_DEFINE_JSON_RPC_REQUEST(WalletTransactionHistoryCallbackRequestJsonRpc, WalletTransactionHistoryCallbackRequest)

}

}
