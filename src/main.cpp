#include "server.h"

#include <iostream>
#include <string>

int main(int argc, const char** argv)
{
    try
    {
        graft::GraftServer gserver;
        bool res = gserver.run(argc, argv);
        if(!res) return -2;
    } catch (const std::exception & e) {
        std::cerr << "Exception thrown: " << e.what() << std::endl;
        throw;
        return -1;
    } catch(...) {
        std::cerr << "Exception of unknown type!\n";
        throw;
        return -1;
    }

    return 0;
}
