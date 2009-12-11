//----------------------------------------------------------------------------
/** @file WolveMain.cpp
 */
//----------------------------------------------------------------------------

#include "SgSystem.h"

#include "config.h"
#include "HexProgram.hpp"
#include "WolveEngine.hpp"
#include "WolvePlayer.hpp"
#include "SwapCheck.hpp"

using namespace benzene;

//----------------------------------------------------------------------------

/** @page wolvemainpage Wolve

    @section overview Overview

    Wolve is a traditional Six-like Hex player. It uses a truncated
    iterative deepening alpha-beta search with an electric ciruit
    evaluation function.

    Because of how fillin added to the board by ICEngine affects the
    resistance-based evaluation function, Wolve actually uses two
    copies of the board, one with fillin to detect wins/losses, and
    the other without for use with the evaluation function.  It is
    possible to use multi-threading to reduce the overhead caused by
    this if multiple processors are available.

    @section search Search
    - WolveEngine
    - WolvePlayer
    - HexAbSearch
    - Resistance

    @section htpcommands HTP Commands
    - @ref hexhtpenginecommands
    - @ref benzenehtpenginecommands

    @todo Add more documentation about Wolve!
*/

//----------------------------------------------------------------------------

namespace {

const char* build_date = __DATE__;

}

//----------------------------------------------------------------------------

int main(int argc, char** argv)
{
    HexProgram& program = HexProgram::Get();
    program.SetInfo("Wolve", VERSION, build_date);
    program.PrintStartupMessage();
    program.Initialize(argc, argv);
    WolvePlayer player;
    try
    {
        GtpInputStream gin(std::cin);
        GtpOutputStream gout(std::cout);
        WolveEngine gh(gin, gout, program.BoardSize(), player);
    
        std::string config = program.ConfigFileToExecute();
        if (config != "")
            gh.ExecuteFile(config);
        gh.MainLoop();
    
        program.Shutdown();
    }
    catch (const GtpFailure& f)
    {
        std::cerr << f.Response() << std::endl;
        return 1;
    }
    return 0;
}

//----------------------------------------------------------------------------
