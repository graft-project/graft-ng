#ifndef SENDSUPERNODEANNOUNCEREQUEST_H
#define SENDSUPERNODEANNOUNCEREQUEST_H

#include "router.h"
#include "inout.h"
#include "jsonrpc.h"


namespace graft {

GRAFT_DEFINE_IO_STRUCT(SignedKeyImageStr,
                      (std::string, key_image),
                      (std::string, signature)
                      );

GRAFT_DEFINE_IO_STRUCT_INITED(SupernodeAnnounce,
                              (std::vector<SignedKeyImageStr>, signed_key_images, std::vector<SignedKeyImageStr>()),
                              (uint64_t, timestamp, 0),
                              (std::string, address, std::string()),
                              (uint64_t, stake_amount, 0),
                              (uint64_t, height, 0),
                              (std::string, secret_viewkey, std::string()),
                              (std::string, network_address, std::string())
                       );


GRAFT_DEFINE_IO_STRUCT_INITED(SendSupernodeAnnounceResponse,
                              (int, Status, 0)
                              );

GRAFT_DEFINE_JSON_RPC_REQUEST(SendSupernodeAnnounceJsonRpcRequest, SupernodeAnnounce);
GRAFT_DEFINE_JSON_RPC_RESPONSE_RESULT(SendSupernodeAnnounceJsonRpcResponse, SendSupernodeAnnounceResponse);


void registerSendSupernodeAnnounceRequest(graft::Router &router);

}

#endif // SENDSUPERNODEANNOUNCEREQUEST_H

