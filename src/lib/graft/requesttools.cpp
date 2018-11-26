#include "lib/graft/requesttools.h"
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid.hpp>
#include <sstream>

namespace graft {

std::string generatePaymentID()
{
    boost::uuids::random_generator gen;
    boost::uuids::uuid id = gen();
    return boost::uuids::to_string(id);
}

uint64_t convertAmount(const std::string &amount)
{
    uint64_t value;
    std::istringstream iss(amount);
    iss >> value;
    return value;
}

}
