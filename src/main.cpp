#include "server.h"
#include "requests.h"
#include "backtrace.h"

#include "log.h"
#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>
// #include <boost/tokenizer.hpp>
#include <boost/filesystem.hpp>
#include <csignal>

namespace po = boost::program_options;
using namespace std;

int main(int argc, const char** argv)
{
    try
    {
        graft::GraftServer gserver;
        bool res = gserver.init(argc, argv);
        if(!res) return -1;
        gserver.serve();
    } catch (const std::exception & e) {
        std::cerr << "Exception thrown: " << e.what() << std::endl;
        return -1;
    }
    catch(...) {
        std::cerr << "Exception of unknown type!\n";
        return -1;
    }

    return 0;
}
