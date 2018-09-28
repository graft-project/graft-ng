#ifndef AUTHORIZERTATXREQUEST_H
#define AUTHORIZERTATXREQUEST_H

#include "router.h"
#include "inout.h"


namespace graft {

GRAFT_DEFINE_IO_STRUCT(AuthorizeRtaTxRequest,
                       (std::string, tx_hex),
                       (std::string, payment_id), // TODO: this should be put to tx.extra and removed from here
                       (uint64_t, amount)        // in case of amounts smaller than 100GRFT - we require only 50% confirmations of auth sample size
                                                 // TODO: Amount needs to be protected with signature
                       );

void registerAuthorizeRtaTxRequests(graft::Router &router);

}

#endif // AUTHORIZERTATXREQUEST_H
