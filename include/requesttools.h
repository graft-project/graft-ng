#ifndef REQUESTTOOLS_H
#define REQUESTTOOLS_H

#include <string>

namespace graft {

std::string generatePaymentID();

uint64_t convertAmount(const std::string &amount);

}

#endif // REQUESTTOOLS_H
