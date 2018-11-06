#include "requests/walletbalancerequest.h"
#include "requests/walletcreateaccountrequest.h"
#include "requests/walletrestoreaccountrequest.h"
#include "requests/walletpreparetransferrequest.h"
#include "requestdefines.h"
#include "wallet_manager.h"

namespace
{

const char* WEBHOOK_CALLBACK_HEADER_TAG = "Callback";

std::string getCallbackString(const graft::Input& input)
{
    const std::vector<std::pair<std::string, std::string>>& headers = input.headers;

    for (const std::pair<std::string, std::string>& header : headers)
    {
        if (header.first != WEBHOOK_CALLBACK_HEADER_TAG)
            continue;

        const std::string& url = header.second;

        if (url.find("http") == 0) //general case - remote host exists
            return url;

        std::string full_url = "http://" + input.remote_host + ":" + url;

        return full_url;
    }

    return std::string();
}

}

namespace graft {

Status walletCreateAccountRequestHandler
 (const Router::vars_t& vars, 
  const graft::Input&   input,
  graft::Context&       context,
  WalletManager&        wallet_manager,
  graft::Output&        output)
{
    WalletCreateAccountRequestJsonRpc request;

    if (!input.get(request)) {
        return errorInvalidParams(output);
    }

    wallet_manager.createAccount(context, request.params.Password, request.params.Language, getCallbackString(input));

    WalletCreateAccountResponse out;

    out.Result = 0;

    output.load(out);

    return Status::Ok;
}

Status walletRestoreAccountRequestHandler
 (const Router::vars_t& vars, 
  const graft::Input&   input,
  graft::Context&       context,
  WalletManager&        wallet_manager,
  graft::Output&        output)
{
    WalletRestoreAccountRequestJsonRpc request;

    if (!input.get(request)) {
        return errorInvalidParams(output);
    }

    wallet_manager.restoreAccount(context, request.params.Password, request.params.Seed, getCallbackString(input));

    WalletRestoreAccountResponse out;

    out.Result = 0;

    output.load(out);

    return Status::Ok;
}

Status walletBalanceRequestHandler
 (const Router::vars_t& vars, 
  const graft::Input&   input,
  graft::Context&       context,
  WalletManager&        wallet_manager,
  graft::Output&        output)
{
    WalletBalanceRequestJsonRpc request;

    if (!input.get(request)) {
        return errorInvalidParams(output);
    }

    wallet_manager.requestBalance(context, request.params.WalletId, request.params.Account, request.params.Password, getCallbackString(input));

    WalletBalanceResponse out;

    out.Result = 0;

    output.load(out);

    return Status::Ok;
}

Status walletPrepareTransferRequestHandler
 (const Router::vars_t& vars, 
  const graft::Input&   input,
  graft::Context&       context,
  WalletManager&        wallet_manager,
  graft::Output&        output)
{
    WalletPrepareTransferRequestJsonRpc request;

    if (!input.get(request)) {
        return errorInvalidParams(output);
    }

    WalletManager::TransferDestinationArray destinations;

    destinations.reserve(request.params.Destinations.size());

    for (const auto& dest : request.params.Destinations)
        destinations.push_back(WalletManager::TransferDestination(dest.Address, std::stoull(dest.Amount)));

    wallet_manager.prepareTransfer(context, request.params.WalletId, request.params.Account, request.params.Password, destinations, getCallbackString(input));

    WalletPrepareTransferResponse out;

    out.Result = 0;

    output.load(out);

    return Status::Ok;
}

namespace
{

typedef std::function<Status (const Router::vars_t&, const Input&, Context&, WalletManager&, Output&)> RequestHandler;

Router::Handler makeRequestHandler(WalletManager& wallet_manager, const RequestHandler& fn)
{
    WalletManager* manager = &wallet_manager;
    return Router::Handler([fn, manager](const Router::vars_t& vars, const Input& in, Context& context, Output& out) { return fn(vars, in, context, *manager, out); });
}

void registerWalletRequest(Router& router, WalletManager& wallet_manager, const char* path, int method, const RequestHandler& handler)
{
    Router::Handler3 h3(nullptr, makeRequestHandler(wallet_manager, handler), nullptr);

    router.addRoute(path, method, h3);

    LOG_PRINT_L0("route " << path << " registered");
}

}

void registerWalletRequests(graft::Router& router, WalletManager& wallet_manager)
{
    registerWalletRequest(router, wallet_manager, "/api/create_account", METHOD_GET, walletCreateAccountRequestHandler);
    registerWalletRequest(router, wallet_manager, "/api/restore_account", METHOD_GET, walletRestoreAccountRequestHandler);
    registerWalletRequest(router, wallet_manager, "/api/wallet_balance", METHOD_GET, walletBalanceRequestHandler);
    registerWalletRequest(router, wallet_manager, "/api/prepare_transfer", METHOD_GET, walletPrepareTransferRequestHandler);
}

}
