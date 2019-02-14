#include "walletnode/requests/balance_request.h"
#include "walletnode/requests/create_account_request.h"
#include "walletnode/requests/restore_account_request.h"
#include "walletnode/requests/prepare_transfer_request.h"
#include "walletnode/requests/transaction_history_request.h"
#include "supernode/requestdefines.h"
#include "walletnode/wallet_manager.h"

namespace
{

const char* WEBHOOK_CALLBACK_HEADER_TAG = "X-Callback";

std::string getCallbackString(const graft::Input& input)
{
    const std::vector<std::pair<std::string, std::string>>& headers = input.headers;

    for (const std::pair<std::string, std::string>& header : headers)
    {
        if (header.first != WEBHOOK_CALLBACK_HEADER_TAG)
            continue;

        const std::string& url = header.second;

        static const char* REMOTE_HOST_PATTERN = "0.0.0.0";

        size_t replacement_pos = url.find(REMOTE_HOST_PATTERN);

        if (replacement_pos == std::string::npos) //general case - remote host exists
            return url;

        std::string full_url = url;

        return full_url.replace(replacement_pos, strlen(REMOTE_HOST_PATTERN), input.host);
    }

    return std::string();
}

}

namespace graft {

namespace walletnode {

namespace request {

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

    output.load(out);

    return Status::Ok;
}

Status walletTransactionHistoryRequestHandler
 (const Router::vars_t& vars, 
  const graft::Input&   input,
  graft::Context&       context,
  WalletManager&        wallet_manager,
  graft::Output&        output)
{
    WalletTransactionHistoryRequestJsonRpc request;

    if (!input.get(request)) {
        return errorInvalidParams(output);
    }

    wallet_manager.requestTransactionHistory(context, request.params.WalletId, request.params.Account, request.params.Password, getCallbackString(input));

    WalletTransactionHistoryResponse out;

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
    registerWalletRequest(router, wallet_manager, "/api/transaction_history", METHOD_GET, walletTransactionHistoryRequestHandler);
}

}

}

}
