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
                              (std::string, tx_as_hex, ""),
                              (TransactionInfo, tx_info, TransactionInfo())
                              );



GRAFT_DEFINE_IO_STRUCT_INITED(AuthorizeRtaTxResponse,
                              (std::string, status, ""),
                              // TODO: another fields (supernode_addr:signature pairs at least)
                              (std::vector<std::string>, signatures, std::vector<std::string>())
                              );


void registerAuthorizeRtaTxRequest(graft::Router &router);

}

#endif // TXTOSIGNREQUEST_H
