#ifndef SENDRAWTXREQUEST_H
#define SENDRAWTXREQUEST_H

#include "router.h"
#include "inout.h"
#include "jsonrpc.h"

#include <string>
#include <wallet/wallet2.h>


namespace graft {

// here we testing how supernode can proxy "sendrawtransaction" call to cryptonode

GRAFT_DEFINE_IO_STRUCT_INITED(TransactionInfo,
                              (uint64_t, amount, 0),
                              (uint64_t, fee, 0),
                              (std::string, dest_address, ""),
                              (std::string, id, ""),
                              (std::string, tx_blob, "")
                              );

GRAFT_DEFINE_IO_STRUCT_INITED(SendRawTxRequest,
                              (std::string, tx_as_hex, ""),
                              (bool, do_not_relay, false),
                              (TransactionInfo, tx_info, TransactionInfo())
                              );

GRAFT_DEFINE_IO_STRUCT_INITED(SendRawTxResponse,
                              (std::string, status, ""),
                              (std::string, reason, ""),
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

}

#endif // SENDRAWTXREQUEST_H
