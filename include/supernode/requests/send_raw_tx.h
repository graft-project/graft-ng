
#pragma once

#include "lib/graft/router.h"
#include "lib/graft/inout.h"
#include "lib/graft/jsonrpc.h"

#include <string>
#include <wallet/wallet2.h>

namespace graft::supernode::request {

// here we testing how supernode can proxy "sendrawtransaction" call to cryptonode

GRAFT_DEFINE_IO_STRUCT_INITED(TransactionInfo,
                              (uint64_t, amount, 0),
                              (uint64_t, fee, 0),
                              (std::string, dest_address, std::string()),
                              (std::string, id, std::string()),
                              (std::string, tx_blob, std::string())
                              );

GRAFT_DEFINE_IO_STRUCT_INITED(SendRawTxRequest,
                              (std::string, tx_as_hex, std::string()),
                              (bool, do_not_relay, false)
                              );

GRAFT_DEFINE_IO_STRUCT_INITED(SendRawTxResponse,
                              (std::string, status, std::string()),
                              (std::string, reason, std::string()),
                              (bool, not_relayed, true),
                              (bool, low_mixin, true),
                              (bool, double_spend, true),
                              (bool, invalid_input, true),
                              (bool, invalid_output, true),
                              (bool, too_big, true),
                              (bool, overspend, true),
                              (bool, fee_too_low, true),
                              (bool, not_rct, true)
                              );


void registerSendRawTxRequest(graft::Router &router);
bool createSendRawTxRequest(const tools::wallet2::pending_tx &ptx, SendRawTxRequest &request);
bool createSendRawTxRequest(const cryptonote::transaction &tx, SendRawTxRequest &request);

}

