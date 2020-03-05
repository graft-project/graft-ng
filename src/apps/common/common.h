// Copyright (c) 2019, The Graft Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include "lib/graft/serialize.h" // serialization macros
#include "lib/graft/inout.h"
#include "supernode/requests/common.h" // NodeAddress
#include "misc_log_ex.h"

#include <net/http_client.h>
#include <storages/http_abstract_invoke.h>


namespace graft::rta::apps {

GRAFT_DEFINE_IO_STRUCT_INITED(WalletDataQrCode,
(graft::supernode::request::NodeAddress, posAddress, graft::supernode::request::NodeAddress()), //
                              (uint64_t, blockNumber, 0),
                              (std::string, blockHash, std::string()),
                              (std::string, paymentId, std::string()),
                              (std::string, key, std::string()) // key so wallet can decrypt the Payment Data
                              );


template<class t_request, class t_error, class t_transport>
bool invoke_http_rest(const boost::string_ref uri, const t_request& req, std::string &response,
t_error& error_struct, t_transport& transport, int &status_code, std::chrono::milliseconds timeout = std::chrono::seconds(15),
const boost::string_ref method = "GET")
{
    graft::Output out;
    out.load(req);

    epee::net_utils::http::fields_list additional_params;
    additional_params.push_back(std::make_pair("Content-Type","application/json; charset=utf-8"));

    const epee::net_utils::http::http_response_info* pri = NULL;
    if(!transport.invoke(uri, method, out.data(), timeout, std::addressof(pri), std::move(additional_params)))
    {
	LOG_PRINT_L1("Failed to invoke http request to  " << uri);
	return false;
    }

    if(!pri)
    {
	LOG_PRINT_L1("Failed to invoke http request to  " << uri << ", internal error (null response ptr)");
	return false;
    }
    status_code = pri->m_response_code;
    response = pri->m_body;

    if(pri->m_response_code/100 != 2)
    {
	graft::Input in; in.load(response);
	LOG_PRINT_L1("Failed to invoke http request to  " << uri << ", wrong response code: " << pri->m_response_code);
	in.get(error_struct);
	return false;
    }

    return true;

}

template<class t_request, class t_response, class t_error, class t_transport>
bool invoke_http_rest(const boost::string_ref uri, const t_request& req, t_response& result_struct, t_error& error_struct, t_transport& transport, int &status_code, std::chrono::milliseconds timeout = std::chrono::seconds(15), const boost::string_ref method = "GET")
{
    std::string response_body;
    if (!invoke_http_rest(uri, req, response_body, error_struct, transport, status_code, timeout, method)) {
	MERROR("invoke_http_rest failed: " << response_body);
	return false;
    }
    graft::Input in; in.load(response_body);
    return in.get(result_struct);
}


} // namespace graft::rta::apps
