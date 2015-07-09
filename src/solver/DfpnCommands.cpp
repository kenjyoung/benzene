//----------------------------------------------------------------------------
/** @file DfpnCommands.cpp */
//----------------------------------------------------------------------------

#include "BitsetIterator.hpp"
#include "EndgameUtil.hpp"
#include "DfpnCommands.hpp"
#include "BoardUtil.hpp"
#include <queue>

using namespace benzene;
using namespace boost::posix_time;

//----------------------------------------------------------------------------

DfpnCommands::DfpnCommands(Game& game, HexEnvironment& env,
                           DfpnSolver& solver,
                           boost::scoped_ptr<DfpnHashTable>& tt,
                           boost::scoped_ptr<DfpnDB>& db,
                           DfpnStates& positions)
    : m_game(game), 
      m_env(env),
      m_solver(solver),
      m_tt(tt),
      m_db(db),
      m_positions(positions)
{
}

void DfpnCommands::Register(GtpEngine& e)
{
    Register(e, "param_dfpn", &DfpnCommands::CmdParam);
    Register(e, "param_dfpn_db", &DfpnCommands::CmdParamSolverDB);
    Register(e, "dfpn-clear-tt", &DfpnCommands::CmdClearTT);
    Register(e, "dfpn-get-bounds", &DfpnCommands::CmdGetBounds);
    Register(e, "dfpn-get-state", &DfpnCommands::CmdGetState);    
    Register(e, "dfpn-get-work", &DfpnCommands::CmdGetWork);
    Register(e, "dfpn-get-pv", &DfpnCommands::CmdGetPV);
    Register(e, "dfpn-solve-state", &DfpnCommands::CmdSolveState);
    Register(e, "dfpn-solver-find-winning", &DfpnCommands::CmdFindWinning);
    Register(e, "dfpn-open-db", &DfpnCommands::CmdOpenDB);
    Register(e, "dfpn-close-db", &DfpnCommands::CmdCloseDB);
    Register(e, "dfpn-merge-db", &DfpnCommands::CmdMergeDB);
    Register(e, "dfpn-dump-db", &DfpnCommands::CmdDumpDB);
    Register(e, "dfpn-restore-db", &DfpnCommands::CmdRestoreDB);
    Register(e, "dfpn-dump-tt", &DfpnCommands::CmdDumpTT);
    Register(e, "dfpn-restore-tt", &DfpnCommands::CmdRestoreTT);
    Register(e, "dfpn-db-stat", &DfpnCommands::CmdDBStat);
    Register(e, "dfpn-evaluation-info", &DfpnCommands::CmdEvaluationInfo);
    Register(e, "dfpn-find-puzzles", &DfpnCommands::CmdFindPuzzles);
}

void DfpnCommands::Register(GtpEngine& engine, const std::string& command,
                            GtpCallback<DfpnCommands>::Method method)
{
    engine.Register(command, new GtpCallback<DfpnCommands>(this, method));
}

//----------------------------------------------------------------------------

void DfpnCommands::AddAnalyzeCommands(HtpCommand& cmd)
{
    cmd <<
        "param/DFPN Param/param_dfpn\n"
        "param/DFPN DB Param/param_dfpn_db\n"
        "none/DFPN Clear TT/dfpn-clear-tt\n"
        "string/DFPN Get Bounds/dfpn-get-bounds %m\n"
        "string/DFPN Get State/dfpn-get-state %m\n"
        "pspairs/DFPN Get Work/dfpn-get-work %m\n"
        "var/DFPN Get PV/dfpn-get-pv %m\n"
        "string/DFPN Solve State/dfpn-solve-state %m\n"
        "plist/DFPN Find Winning/dfpn-solver-find-winning %m\n"
    	"none/DFPN Find Puzzles/dfpn-find-puzzles"
        "none/DFPN Open DB/dfpn-open-db %r\n"
        "none/DFPN Close DB/dfpn-close-db\n"
        "none/DFPN Merge DB/dfpn-merge-db %r\n"
        "none/DFPN Dump DB/dfpn-dump-db\n"
        "none/DFPN Restore DB/dfpn-restore-db\n"
        "none/DFPN Dump TT/dfpn-dump-tt\n"
        "none/DFPN Restore TT/dfpn-restore-tt\n"
        "string/DFPN DB Stats/dfpn-db-stat\n"
        "string/DFPN Eval Info/dfpn-evaluation-info\n";
}

void DfpnCommands::CmdParamSolverDB(HtpCommand& cmd)
{
    SolverDBParameters& param = m_positions.Parameters();
    if (cmd.NuArg() == 0)
    {
        cmd << '\n'
            << "[bool] use_flipped_states " << param.m_useFlippedStates << '\n'
            << "[bool] use_proof_transpositions " 
            << param.m_useProofTranspositions << '\n'
            << "[string] max_stones " << param.m_maxStones << '\n'
            << "[string] trans_stones " << param.m_transStones << '\n';
    }
    else if (cmd.NuArg() == 2)
    {
        std::string name = cmd.Arg(0);
        if (name == "use_flipped_states")
            param.m_useFlippedStates = cmd.Arg<bool>(1);
        else if (name == "use_proof_transpositions")
            param.m_useProofTranspositions = cmd.Arg<bool>(1);
        else if (name == "max_stones")
            param.m_maxStones = cmd.ArgMin<int>(1, 0);
        else if (name == "trans_stones")
            param.m_transStones = cmd.ArgMin<int>(1, 0);
        else
            throw HtpFailure() << "unknown parameter: " << name;
    }
    else 
        throw HtpFailure("Expected 0 or 2 arguments");
}

void DfpnCommands::CmdParam(HtpCommand& cmd)
{
    if (cmd.NuArg() == 0)
    {
        cmd << '\n'
            << "[bool] use_guifx "
            << m_solver.UseGuiFx() << '\n'
            << "[bool] guifx_deep_bounds "
            << m_solver.GuiFxDeepBounds() << '\n'
            << "[string] timelimit "
            << m_solver.Timelimit() << '\n'
            << "[string] tt_size "
            << ((m_tt.get() == 0) ? 0 : m_tt->MaxHash()) << '\n'
            << "[string] widening_base "
            << m_solver.WideningBase() << '\n'
            << "[string] widening_factor "
            << m_solver.WideningFactor() << '\n'
            << "[string] epsilon "
            << m_solver.Epsilon() << '\n'
            << "[string] threads "
            << m_solver.Threads() << '\n'
            << "[string] thread_work "
            << m_solver.ThreadWork() << '\n'
            << "[string] db_bak_filename "
            << m_solver.DbBakFilename() << '\n'
            << "[string] db_bak_start "
            << '"' << m_solver.DbBakStart() << '"' << '\n'
            << "[string] db_bak_period "
            << m_solver.DbBakPeriod() << '\n'
            << "[string] tt_bak_filename "
            << m_solver.TtBakFilename() << '\n'
            << "[string] tt_bak_start "
            << '"' << m_solver.TtBakStart() << '"' << '\n'
            << "[string] tt_bak_period "
            << m_solver.TtBakPeriod() << '\n';
    }
    else if (cmd.NuArg() == 2)
    {
        std::string name = cmd.Arg(0);
        if (name == "use_guifx")
            m_solver.SetUseGuiFx(cmd.Arg<bool>(1));
        else if (name == "guifx_deep_bounds")
            m_solver.SetGuiFxDeepBounds(cmd.Arg<bool>(1));
        else if (name == "timelimit")
            m_solver.SetTimelimit(cmd.ArgMin<float>(1, 0.0));
	else if (name == "tt_size")
	{
	    int size = cmd.ArgMin<int>(1, 0);
	    if (size == 0)
		m_tt.reset(0);
	    else
		m_tt.reset(new DfpnHashTable(size));
	}
        else if (name == "widening_base")
            m_solver.SetWideningBase(cmd.ArgMin<int>(1, 0));
        else if (name == "widening_factor")
        {
            float value = cmd.Arg<float>(1);
            if (0.0f < value && value <= 1.0f)
                m_solver.SetWideningFactor(value);
            else
                throw GtpFailure() << "widening_factor must be in (0, 1]";
        }
        else if (name == "epsilon")
        {
            float value = cmd.Arg<float>(1);
            if (0.0f <= value)
                m_solver.SetEpsilon(value);
            else
                throw GtpFailure() << "epsilon cannot be negative";
        }
        else if (name == "threads")
            m_solver.SetThreads(cmd.ArgMinMax<int>(1, 1, DFPN_MAX_THREADS));
        else if (name == "thread_work")
            m_solver.SetThreadWork(cmd.ArgMin<size_t>(1, 1));
        else if (name == "db_bak_filename")
            m_solver.SetDbBakFilename(cmd.Arg(1));
        else if (name == "db_bak_start")
            m_solver.SetDbBakStart(time_from_string(cmd.Arg(1)));
        else if (name == "db_bak_period")
            m_solver.SetDbBakPeriod(duration_from_string(cmd.Arg(1)));
        else if (name == "tt_bak_filename")
            m_solver.SetTtBakFilename(cmd.Arg(1));
        else if (name == "tt_bak_start")
            m_solver.SetTtBakStart(time_from_string(cmd.Arg(1)));
        else if (name == "tt_bak_period")
            m_solver.SetTtBakPeriod(duration_from_string(cmd.Arg(1)));
        else
            throw GtpFailure() << "Unknown parameter: " << name;
    }
    else
        throw GtpFailure() << "Expected 0 or 2 arguments";
}

/** Solves the current state with dfpn using the current hashtable. */
void DfpnCommands::CmdSolveState(HtpCommand& cmd)
{
	SgTimer timer;
    cmd.CheckNuArgLessEqual(3);
    HexColor colorToMove = m_game.Board().WhoseTurn();
    if (cmd.NuArg() >= 1)
        colorToMove = HtpUtil::ColorArg(cmd, 0);
    // DfpnBounds::MAX_WORK cannot be used as an argument to ArgMinMax()
    // directly, because it is an integral constant class member that is not
    // defined anywhere and arguments to ArgMinMax() are passed by reference.
    // Older versions of GCC (including the current Cygwin GCC 4.3.4) generate
    // and error ("undefined reference to DfpnBounds::MAX_WORK"), probably in
    // accordance to the C++ standard. The best solution would be to change
    // GtpCommand::ArgMinMax() in Fuego to pass arguments by value.
    const DfpnBoundType maxWork = DfpnBounds::MAX_WORK;
    DfpnBoundType maxPhi = maxWork;
    DfpnBoundType maxDelta = maxWork;
    if (cmd.NuArg() >= 2)
        maxPhi = cmd.ArgMinMax<DfpnBoundType>(1, 0, maxWork);
    if (cmd.NuArg() >= 3)
        maxDelta = cmd.ArgMinMax<DfpnBoundType>(2, 0, maxWork);
    DfpnBounds maxBounds(maxPhi, maxDelta);
    PointSequence pv;
    HexBoard& brd = m_env.SyncBoard(m_game.Board());
    HexColor winner 
        = m_solver.StartSearch(HexState(m_game.Board(), colorToMove), brd, 
                               m_positions, pv, maxBounds);
    cmd << winner;
    LogInfo() << "Total Elapsed Time: " << timer.GetTime() << '\n';
}

typedef struct puzzle_info
{
	HexState state;
	PointSequence pv;
	double work;
	bool operator<(const puzzle_info& rhs) const
	{
		return this->work <rhs.work;
	}

} puzzle_info;

bitset_t RemoveRotations(const StoneBoard& brd, const bitset_t& consider)
{
    bitset_t ret;
    for (BitsetIterator it(consider); it; ++it)
    {
        HexPoint rotated = BoardUtil::Rotate(brd.Const(), *it);
        if (!ret.test(rotated))
            ret.set(*it);
    }
    return ret;
}

int GetPuzzleInfo(HexState& state, HexEnvironment& env, DfpnSolver& solver, DfpnStates& positions, std::priority_queue<puzzle_info>& puzzle_queue){
	    bitset_t consider = state.Position().GetEmpty();
	    if (state.Position().IsSelfRotation())
	    	        consider = RemoveRotations(state.Position(), consider);
	    HexColor colorToMove = state.ToPlay();
	    int win_count = 0;
	    for (BitsetIterator p(consider); p; ++p)
	    {
	        state.PlayMove(*p);
	        HexBoard& brd = env.SyncBoard(state.Position());
	        LogInfo() << "****** Trying " << *p << " ******\n" << brd << '\n';
	        PointSequence pv;
	        SgTimer timer;
	        HexColor winner = solver.StartSearch(state, brd, positions, pv);
	        DfpnData data;
	        solver.DBRead(state,data);
	        puzzle_info info = {HexState(state), pv, data.m_work};
	        puzzle_queue.push(info);
	        if (winner == colorToMove)
	        	win_count++;
	        LogInfo() << "****** " << winner << " wins ******\n";
	        state.UndoMove(*p);
	    }
	    return win_count;
}

/** Look for continuations of the current position which are
 	difficult to solve and have a low fraction of winning moves.
 	arg0 = color to move in current state
 	arg1 = keep this many of the hardest moves to solve at each point
 	arg2 = if the fraction of wins is less than this but more than zero we call it a puzzle*/
void DfpnCommands::CmdFindPuzzles(HtpCommand& cmd)
{
	std::queue<HexState> states;
	vector<HexState> puzzles;
	std::priority_queue<puzzle_info> puzzle_queue;
	HexState cur;
    cmd.CheckNuArg(3);
    HexColor colorToMove = HtpUtil::ColorArg(cmd, 0);
    int states_to_keep = cmd.IntArg(1);
    double winning_fraction = cmd.FloatArg(2);
    
    states.push(HexState(m_game.Board(), colorToMove));
    while(!states.empty()){
    	cur =states.front();
    	states.pop();
    	int move_count = cur.Position().GetEmpty().count();
    	int wins = GetPuzzleInfo(cur, m_env, m_solver, m_positions, puzzle_queue);
    	if(wins>0 && (float) wins/ (float) move_count < winning_fraction){
    		LogInfo()<<"Puzzle found:\n"<<cur.Position()<<"\n";
    		puzzles.push_back(cur);
    	}

    	int i = 0;
    	while(i<states_to_keep && !puzzle_queue.empty()){
    		puzzle_info info = puzzle_queue.top();
    		puzzle_queue.pop();
    		PointSequence pv = info.pv;
    		HexState state = HexState(info.state);
    		for(vector<HexPoint>::iterator p = pv.begin(); p<pv.end(); p++){
    			if((state.Position().GetEmpty().count()-1)*winning_fraction<1)
    				break;
    			state.PlayMove(*p);
    			states.push(HexState(state));
    		}
    	}
    }
    std::vector<HexState>::iterator it;
    LogInfo()<<"Puzzles:\n";
    for(it = puzzles.begin(); it< puzzles.end(); it++){
    	LogInfo()<<(*it).Position()<<"\n";
    }
}

/** Finds all winning moves in the current state with dfpn,
    using the current hashtable. */
void DfpnCommands::CmdFindWinning(HtpCommand& cmd)
{
    cmd.CheckNuArg(1);
    HexColor colorToMove = HtpUtil::ColorArg(cmd, 0);
    HexBoard& brd = m_env.SyncBoard(m_game.Board());
    //brd.ComputeAll(colorToMove);
    bitset_t consider = brd.GetPosition().GetEmpty();

    bitset_t winning;
    SgTimer timer;

    HexState state(m_game.Board(), colorToMove);
    for (BitsetIterator p(consider); p; ++p)
    {
        state.PlayMove(*p);
        HexBoard& brd = m_env.SyncBoard(state.Position());
        LogInfo() << "****** Trying " << *p << " ******\n" << brd << '\n';
        PointSequence pv;
        HexColor winner = m_solver.StartSearch(state, brd, m_positions, pv);
        if (winner == colorToMove)
            winning.set(*p);
        LogInfo() << "****** " << winner << " wins ******\n";
        state.UndoMove(*p);
    }
    LogInfo() << "Total Elapsed Time: " << timer.GetTime() << '\n';
    cmd << HexPointUtil::ToString(winning);
}

/** Clears the current dfpn hashtable. */
void DfpnCommands::CmdClearTT(HtpCommand& cmd)
{
    UNUSED(cmd);
    m_tt->Clear();
}

/** Displays information about the current state from the
    hashtable. */
void DfpnCommands::CmdGetState(HtpCommand& cmd)
{
    cmd.CheckNuArg(1);
    HexColor colorToMove = HtpUtil::ColorArg(cmd, 0);    
    HexState state(m_game.Board(), colorToMove);
    DfpnData data;
    if (m_positions.Get(state, data))
        cmd << data << '\n';
}

/** Displays bounds of every empty cell in current state.
    Bounds are obtained from the current hashtable. */
void DfpnCommands::CmdGetBounds(HtpCommand& cmd)
{
    cmd.CheckNuArg(1);
    HexColor colorToMove = HtpUtil::ColorArg(cmd, 0);
    HexState state(m_game.Board(), colorToMove);
    for (BitsetIterator it(state.Position().GetEmpty()); it; ++it)
    {
        state.PlayMove(*it);
        DfpnData data;
        if (m_positions.Get(state, data))
        {
            cmd << ' ' << *it << ' ';
            if (data.m_bounds.IsWinning())
                cmd << 'L';
            else if (data.m_bounds.IsLosing())
                cmd << 'W';
            else 
                cmd << data.m_bounds.phi << ':' << data.m_bounds.delta;
        }
        state.UndoMove(*it);
    }
}

/** Displays work of every empty cell in current state.
    Bounds are obtained from the current hashtable. */
void DfpnCommands::CmdGetWork(HtpCommand& cmd)
{
    cmd.CheckNuArg(1);
    HexColor colorToMove = HtpUtil::ColorArg(cmd, 0);
    HexState state(m_game.Board(), colorToMove);
    for (BitsetIterator it(state.Position().GetEmpty()); it; ++it)
    {
        state.PlayMove(*it);
        DfpnData data;
        if (m_positions.Get(state, data))
            cmd << ' ' << *it << ' ' << data.m_work;
        state.UndoMove(*it);
    }
}

/** Displays PV from current position. */
void DfpnCommands::CmdGetPV(HtpCommand& cmd)
{
    cmd.CheckNuArg(1);
    HexColor colorToMove = HtpUtil::ColorArg(cmd, 0);
    PointSequence pv;
    SolverDBUtil::GetVariation(HexState(m_game.Board(), colorToMove), 
                               m_positions, pv);
    cmd << HexPointUtil::ToString(pv);
}

/** Opens a database. 
    Usage: "db-open [filename]"
*/
void DfpnCommands::CmdOpenDB(HtpCommand& cmd)
{
    cmd.CheckNuArgLessEqual(3);
    std::string filename = cmd.Arg(0);
    try {
        m_db.reset(new DfpnDB(filename));
    }
    catch (BenzeneException& e) {
        m_db.reset(0);
        throw HtpFailure() << "Error opening db: '" << e.what() << "'\n";
    }
}

/** Closes an open database. */
void DfpnCommands::CmdCloseDB(HtpCommand& cmd)
{
    cmd.CheckNuArg(0);
    if (m_db.get() == 0)
        throw HtpFailure("No open database!\n");
    m_db.reset(0);
}

/** Merges a database into the current database.
 *    Usage: "db-merge [filename]"
 */
void DfpnCommands::CmdMergeDB(HtpCommand& cmd)
{
    cmd.CheckNuArgLessEqual(3);
    if (!m_db)
        throw HtpFailure("No open database!\n");
    std::string filename = cmd.Arg(0);
    boost::scoped_ptr<DfpnDB> other_db;
    try {
        other_db.reset(new DfpnDB(filename));
        m_db->Merge(*other_db);
    }
    catch (BenzeneException& e) {
        other_db.reset(0);
        throw HtpFailure() << "Error merging db: '" << e.what() << "'\n";
    }
}

/** Dumps db. */
void DfpnCommands::CmdDumpDB(HtpCommand& cmd)
{
    UNUSED(cmd);
    try {
        m_solver.DbDump(m_positions);
    }
    catch (BenzeneException& e) {
        throw HtpFailure() << e.what();
    }
}

/** Restores db. */
void DfpnCommands::CmdRestoreDB(HtpCommand& cmd)
{
    UNUSED(cmd);
    try {
        m_solver.DbRestore(m_positions);
    }
    catch (BenzeneException& e) {
        throw HtpFailure() << e.what();
    }
}

/** Dumps tt. */
void DfpnCommands::CmdDumpTT(HtpCommand& cmd)
{
    UNUSED(cmd);
    try {
        m_solver.TtDump(m_positions);
    }
    catch (BenzeneException& e) {
        throw HtpFailure() << e.what();
    }
}

/** Restores tt. */
void DfpnCommands::CmdRestoreTT(HtpCommand& cmd)
{
    UNUSED(cmd);
    try {
        m_solver.TtRestore(m_positions);
    }
    catch (BenzeneException& e) {
        throw HtpFailure() << e.what();
    }
}

/** Prints database statistics. */
void DfpnCommands::CmdDBStat(HtpCommand& cmd)
{
    cmd.CheckNuArg(0);
    if (m_db.get() == 0)
        throw HtpFailure("No open database!\n");
    cmd << m_db->BDBStatistics();
}

void DfpnCommands::CmdEvaluationInfo(HtpCommand& cmd)
{
    cmd.CheckNuArg(0);
    cmd << m_solver.EvaluationInfo();
}

//----------------------------------------------------------------------------
