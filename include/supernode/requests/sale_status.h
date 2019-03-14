
#pragma once

#include "lib/graft/router.h"
#include "lib/graft/jsonrpc.h"
#include "rta/supernode.h"

namespace graft::supernode::request {

// returns sale status by sale id
GRAFT_DEFINE_IO_STRUCT(SaleStatusRequest,
    (std::string, PaymentID)
);

// message to be broadcasted
GRAFT_DEFINE_IO_STRUCT_INITED(UpdateSaleStatusBroadcast,
                       (std::string, PaymentID, std::string()),
                       (int, Status, 0),
                       (std::string, id_key, std::string()),   // address who updates the status
                       (std::string, signature, std::string())  // signature who updates the status
                       );

GRAFT_DEFINE_IO_STRUCT_INITED(SaleStatusResponse,
    (int, Status, 0)
);

/*!
 * \brief signSaleStatusUpdate - signs sale status message
 * \param payment_id
 * \param status
 * \param supernode
 * \return
 */
std::string signSaleStatusUpdate(const std::string &payment_id, int status, const SupernodePtr &supernode);

/*!
 * \brief checkSaleStatusUpdateSignature
 * \param payment_id
 * \param status
 * \param address
 * \param signature
 * \return
 */
bool checkSaleStatusUpdateSignature(const std::string &payment_id, int status, const std::string &address,
                                    const std::string &signature, const SupernodePtr &supernode);


void registerSaleStatusRequest(graft::Router &router);

}

