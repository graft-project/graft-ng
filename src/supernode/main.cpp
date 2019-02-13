
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
        LOG_ERROR("The program is terminated because of error: ") << e.what();
        return -1;
    } catch (const std::exception & e) {
        LOG_ERROR("Exception thrown: ") << e.what();
        throw;
    } catch(...) {
        LOG_ERROR("Exception of unknown type!");
        throw;
    }

    return 0;
}

