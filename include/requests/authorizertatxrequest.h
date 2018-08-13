#ifndef AUTHORIZERTATXREQUEST_H
#define AUTHORIZERTATXREQUEST_H

#include "router.h"
#include "inout.h"


namespace graft {

GRAFT_DEFINE_IO_STRUCT(AuthorizeRtaTxRequest,
                       (std::string, tx_hex),
                       (std::string, payment_id) // TODO: this should be put to tx.extra and removed from here
                       );

void registerAuthorizeRtaTxRequests(graft::Router &router);

}

#endif // AUTHORIZERTATXREQUEST_H
