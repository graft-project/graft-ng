
#include "server.h"
#include "supernode.h"
#include "backtrace.h"
#include "graft_exception.h"

#include "supernode/node.h"
#include "supernode/config.h"
#include "supernode/config_loader.h"

namespace graft
{
int main_1(int argc, const char** argv);
int main_2(int argc, const char** argv);
}

int main(int argc, const char** argv)
{
  return graft::main_1(argc, argv);
}

namespace graft
{

std::terminate_handler prev_terminate = nullptr;

// Unhandled exception in a handler with noexcept specifier causes
// termination of the program, stack backtrace is created upon the termination.
// The exception in a handler with no noexcept specifier doesn't effect
// the workflow, the error propagates back to the client.
void terminate()
{
    std::cerr << "\nTerminate called, dump stack:\n";
    graft_bt();

    //dump exception info
    std::exception_ptr eptr = std::current_exception();
    if(eptr)
    {
        try
        {
             std::rethrow_exception(eptr);
        }
        catch(std::exception& ex)
        {
            std::cerr << "\nTerminate caused by exception : '" << ex.what() << "'\n";
        }
        catch(...)
        {
            std::cerr << "\nTerminate caused by unknown exception.\n";
        }
    }

    prev_terminate();
}

int main_1(int argc, const char** argv)
{
    graft::prev_terminate = std::set_terminate( graft::terminate );

    try
    {
        graft::snd::Supernode supernode;
        bool res = supernode.run(argc, argv);
        if(!res) return -2;
    } catch (const graft::exit_error& e) {
        std::cerr << "The program is terminated because of error: " << e.what() << std::endl;
        return -1;
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


int main_2(int argc, const char** argv)
{
    graft::prev_terminate = std::set_terminate(graft::terminate);

    using graft::supernode::Config;
    using graft::supernode::ConfigLoader;
    try
    {
        Config cfg;
        if(ConfigLoader().load(argc, argv, cfg) && !Node().run(cfg))
            return -2;
    }
    catch(const graft::exit_error& e)
    {
        std::cerr << "The program is terminated because of error: " << e.what() << std::endl;
        return -1;
    }
    catch(const std::exception& e)
    {
        std::cerr << "Exception thrown: " << e.what() << std::endl;
        throw;
        return -1;
    }
    catch(...)
    {
        std::cerr << "Exception of unknown type!\n";
        throw;
        return -1;
    }

    return 0;
}

}





