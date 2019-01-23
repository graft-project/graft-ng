
#include "supernode/server.h"
#include "supernode/supernode.h"
#include "lib/graft/backtrace.h"
#include "lib/graft/graft_exception.h"

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

} //namespace graft


int main(int argc, const char** argv)
{
    graft::prev_terminate = std::set_terminate( graft::terminate );

    try
    {
        graft::snd::Supernode supernode;
        bool res = supernode.run(argc, argv);
        if(!res) return -2;
    } catch (const graft::exit_error& e) {
        std::ostringstream oss;
        oss << "The program is terminated because of error: " << e.what();
        LOG_PRINT_L0(oss.str());
        std::cerr << oss.str() << std::endl;
        return -1;
    } catch (const std::exception & e) {
        std::ostringstream oss;
        oss << "Exception thrown: " << e.what();
        LOG_PRINT_L0(oss.str());
        std::cerr << oss.str() << std::endl;
        throw;
        return -1;
    } catch(...) {
        std::ostringstream oss;
        oss << "Exception of unknown type!";
        LOG_PRINT_L0(oss.str());
        std::cerr << oss.str() << std::endl;
        throw;
        return -1;
    }

    return 0;
}

