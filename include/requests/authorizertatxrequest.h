#ifndef AUTHORIZERTATXREQUEST_H
#define AUTHORIZERTATXREQUEST_H

#include "router.h"
#include "inout.h"
#include "jsonrpc.h"
#include "requests/sendrawtxrequest.h" // TransactionInfo structure

#include <string>


namespace graft {

// This request issued by a wallet. Served with REST insterface
GRAFT_DEFINE_IO_STRUCT_INITED(AuthorizeRtaTxRequest,
                              (TransactionInfo, tx_info, TransactionInfo()),
                              (std::string, supernode_addr, "")
                              );


GRAFT_DEFINE_IO_STRUCT_INITED(AuthorizeRtaTxResponse,
                              (int, Status, -1),
                              (std::string, tx_id, std::string()),
                              (std::string, message, std::string()),
                              (std::string, supernode_addr, std::string()),
                              // TODO: another fields (supernode_addr:signature pairs at least)
                              (std::string, signature, std::string())
                              );


void registerAuthorizeRtaTxRequest(graft::Router &router);

}

#endif // TXTOSIGNREQUEST_H
