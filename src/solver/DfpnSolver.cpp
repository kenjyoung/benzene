//----------------------------------------------------------------------------
/** @file DfpnSolver.cpp */
//----------------------------------------------------------------------------

#include "SgSystem.h"
#include "SgWrite.h"
#include "BitsetIterator.hpp"
#include "BoardUtil.hpp"
#include "DfpnSolver.hpp"
#include "PatternState.hpp"
#include "EndgameUtil.hpp"
#include "ProofUtil.hpp"
#include "Resistance.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/scoped_array.hpp>

using namespace benzene;
using namespace std;
using namespace boost::gregorian;
using namespace boost::posix_time;

using std::ceil;

//----------------------------------------------------------------------------

/** Current version of the dfpn database.
    Update this if DfpnData changes to prevent old out-of-date
    databases from being loaded. */
const std::string DfpnDB::DFPN_DB_VERSION("BENZENE_DFPN_DB_VER_0003");

//----------------------------------------------------------------------------

#ifndef NDEBUG
void DfpnBounds::CheckConsistency() const
{
    // Check range
    BenzeneAssert(phi <= INFTY);
    BenzeneAssert(delta <= INFTY);
    // one is 0 iff the other is infinity
    BenzeneAssert(0 != phi || INFTY == delta);
    BenzeneAssert(INFTY != phi || 0 == delta);
    BenzeneAssert(0 != delta || INFTY == phi);
    BenzeneAssert(INFTY != delta || 0 == phi);
}
#else
void DfpnBounds::CheckConsistency() const
{
}
#endif

template <class T>
static inline void VecPrune(std::vector<T>& v, bitset_t what)
{
    size_t j = 0;
    for (size_t i = 0; i < v.size(); ++i)
        if (!what.test(i))
            v[j++] = v[i];
    v.resize(j);
}

//----------------------------------------------------------------------------

void VirtualBoundsTT::Store(int id, size_t depth, SgHashCode hash,
                            const DfpnBounds& bounds)
{
    if (depth >= data.size())
        data.resize(depth + 1);
    else
    {
        std::vector<Entry>& level = data[depth];
        for (size_t i = 0; i < level.size(); ++i)
            if (level[i].hash == hash)
            {
                level[i].workers.set(id);
                level[i].bounds = bounds;
                return;
            }
    }
    Entry entry;
    entry.hash = hash;
    entry.workers.set(id);
    entry.bounds = bounds;
    data[depth].push_back(entry);
}

void VirtualBoundsTT::Lookup(size_t depth, SgHashCode hash,
                             DfpnBounds& bounds)
{
    if (depth >= data.size())
        return;
    const std::vector<Entry>& level = data[depth];
    for (size_t i = 0; i < level.size(); ++i)
        if (level[i].hash == hash)
        {
            bounds = level[i].bounds;
            return;
        }
}

void VirtualBoundsTT::Remove(int id, size_t depth, SgHashCode hash,
                             const DfpnBounds& bounds, bool solved, bool *path_solved)
{
    std::vector<Entry>& level = data[depth];
    for (size_t i = 0; i < level.size(); ++i)
        if (level[i].hash == hash)
        {
            level[i].workers.reset(id);
            if (level[i].workers.none())
            {
                level[i] = level.back();
                level.pop_back();
            }
            else
            {
                level[i].bounds = bounds;
                if (solved)
                {
                    for (int tid = 0; tid < DFPN_MAX_THREADS; tid++)
                        if (level[i].workers[tid])
                            path_solved[tid] = true;
                }
            }
            return;
        }
}

//----------------------------------------------------------------------------

DfpnChildren::DfpnChildren()
{
}

void DfpnChildren::SetChildren(const std::vector<HexPoint>& children)
{
    m_children = children;
}

void DfpnChildren::PlayMove(std::size_t index, HexState& state) const
{
    state.PlayMove(m_children[index]);
}

void DfpnChildren::UndoMove(std::size_t index, HexState& state) const
{
    state.UndoMove(m_children[index]);
}

std::size_t DfpnChildren::MoveIndex(HexPoint x) const
{
    for (std::size_t i = 0; i < m_children.size(); ++i)
        if (m_children[i] == x)
            return i;
        return 0;
}

//----------------------------------------------------------------------------

/** @page dfpnguifx Dfpn Progress Indication
    @ingroup dfpn

    It is difficult to present the user with meaningful progress
    indication in dfpn. The current method simply displays the current
    (phi, delta) bounds of each child of the root. This is output
    whenever a child's bound changes. This is reasonably useful,
    except in the case where only a single child remains and the
    search is stuck several ply deep.
*/

DfpnSolver::GuiFx::GuiFx(const DfpnSolver& solver)
    : m_solver(solver),
      m_pvDoShift(false),
      m_timeOfLastWrite(0.0),
      m_delay(1.0)
{
}

void DfpnSolver::GuiFx::ClearChildren()
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_data.clear();
}

void DfpnSolver::GuiFx::SetChildren(const DfpnChildren& children,
                                    const std::vector<DfpnData>& data)
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    m_children = children;
    m_data = data;
}

void DfpnSolver::GuiFx::SetChildrenOnce(const DfpnChildren& children,
                                        const std::vector<DfpnData>& data)
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    if (m_data.empty())
    {
        m_children = children;
        m_data = data;
    }
}

void DfpnSolver::GuiFx::SetFirstPlayer(HexColor color)
{
    m_firstColor = color;
}

void DfpnSolver::GuiFx::SetPV()
{
    SetPV(std::vector<std::pair<HexPoint, DfpnBounds> >());
}

void DfpnSolver::GuiFx::SetPV(HexPoint move, DfpnBounds bounds)
{
    SetPV(std::vector<std::pair<HexPoint, DfpnBounds> >(1, std::make_pair(move, bounds)));
}

void DfpnSolver::GuiFx::SetPV(const std::vector<std::pair<HexPoint, DfpnBounds> >& pv)
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    if (m_pvDoShift)
    {
        m_pvToWrite = m_pvCur;
        m_pvDoShift = false;
    }
    m_pvCur = pv;
    std::size_t i = 0;
    for (; i < std::min(m_pvToWrite.size(), pv.size()); ++i)
        if (m_pvToWrite[i].first != pv[i].first)
            break;
    m_pvToWrite.resize(i);
}

void DfpnSolver::GuiFx::UpdateBounds(HexPoint move, const DfpnBounds& bounds)
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    if (m_data.empty())
        return;
    for (std::size_t i = 0; i < m_children.Size(); ++i)
        if (m_children.FirstMove(i) == move)
        {
            m_data[i].m_bounds = bounds;
            break;
        }
}

/** Always writes output. */
void DfpnSolver::GuiFx::WriteForced()
{
    DoWrite();
}

/** Writes output only if last write was more than m_delay seconds
    ago or if the move is different. */
void DfpnSolver::GuiFx::Write()
{
    double currentTime = SgTime::Get();
    if (currentTime < m_timeOfLastWrite + m_delay)
        return;
    m_timeOfLastWrite = currentTime;
    DoWrite();
}

/** Writes progress indication. */
void DfpnSolver::GuiFx::DoWrite()
{
    boost::lock_guard<boost::mutex> lock(m_mutex);
    if (m_data.empty())
        return;
    std::ostringstream os;
    os << "gogui-gfx:\n";
    os << "dfpn\n";
    os << "VAR";
    std::vector<int> pv_idx(BITSETSIZE, -1);
    std::vector<HexPoint> to_print;
    int numLosses = 0;
    for (std::size_t i = 0; i < m_children.Size(); ++i)
    {
        HexPoint move = m_children.FirstMove(i);
        pv_idx[move] = 0;
        to_print.push_back(move);
        if (m_data[i].m_bounds.IsWinning())
            numLosses++;
    }
    HexColor color = m_firstColor;
    size_t pv_size = std::min(m_pvToWrite.size() + 1, m_pvCur.size());
    for (std::size_t i = 0; i < pv_size; ++i)
    {
        os << ' ' << (color == BLACK ? 'B' : 'W')
           << ' ' << m_pvCur[i].first;
        HexPoint move = m_pvCur[i].first;
        if (pv_idx[move] < 0 && m_solver.GuiFxDeepBounds())
            to_print.push_back(move);
        pv_idx[move] = (int)(i + 1);
        color = !color;
    }
    os << '\n';
    m_pvDoShift = true;
    os << "LABEL";
    for (size_t i = 0; i < to_print.size(); ++i)
    {
        os << ' ' << to_print[i];
        size_t idx = pv_idx[to_print[i]];
        DfpnBounds bounds;
        if (idx > 0 && m_solver.GuiFxDeepBounds())
        {
            bounds = m_pvCur[idx - 1].second;
            if ((idx & 1) == 0)
                std::swap(bounds.phi, bounds.delta);
        }
        else
            bounds = m_data[i].m_bounds;
        if (0 == bounds.phi)
        {
            os << " L";
            if (idx)
                os << idx;
        }
        else if (0 == bounds.delta)
        {
            os << " W";
            if (idx)
                os << idx;
        }
        else
        {
            os << ' ' << bounds.delta << '@';
            if (idx)
                os << idx;
            os << '@' << bounds.phi;
        }
    }
    os << '\n';
    os << "TEXT ";
    os << numLosses << '/' << m_children.Size() << " proven losses, var:";
    for (std::size_t i = 0; i < m_pvCur.size(); ++i)
        os << ' ' << m_pvCur[i].first;
    os << "\n\n";
    std::cout << os.str();
    std::cout.flush();
}

//----------------------------------------------------------------------------

int DfpnData::PackedSize() const
{
    return sizeof(m_bounds)
        + sizeof(m_bestMove)
        + sizeof(m_work)
        + sizeof(m_maxProofSet)
        + sizeof(m_evaluationScore)
        + sizeof(short) * (m_children.Size() + 1);
}

void DfpnData::Pack(byte* data) const
{
    byte* off = data;
    *reinterpret_cast<DfpnBounds*>(off) = m_bounds;
    off += sizeof(m_bounds);
    *reinterpret_cast<HexPoint*>(off) = m_bestMove;
    off += sizeof(m_bestMove);
    *reinterpret_cast<size_t*>(off) = m_work;
    off += sizeof(m_work);
    *reinterpret_cast<bitset_t*>(off) = m_maxProofSet;
    off += sizeof(m_maxProofSet);
    *reinterpret_cast<float*>(off) = m_evaluationScore;
    off += sizeof(m_evaluationScore);
    const std::vector<HexPoint>& moves = m_children.m_children;
    for (std::size_t i = 0; i < moves.size(); ++i)
    {
        *reinterpret_cast<short*>(off) = static_cast<short>(moves[i]);
        off += sizeof(short);
    }
    *reinterpret_cast<short*>(off) = static_cast<short>(INVALID_POINT);
    off += sizeof(short);
    if (off - data != PackedSize())
        throw BenzeneException("Bad size!");
}

void DfpnData::Unpack(const byte* data)
{
    m_bounds = *reinterpret_cast<const DfpnBounds*>(data);
    data += sizeof(m_bounds);
    m_bestMove = *reinterpret_cast<const HexPoint*>(data);
    data += sizeof(m_bestMove);
    m_work = *reinterpret_cast<const size_t*>(data);
    data += sizeof(m_work);
    m_maxProofSet = *reinterpret_cast<const bitset_t*>(data);
    data += sizeof(m_maxProofSet);
    m_evaluationScore = *reinterpret_cast<const float*>(data);
    data += sizeof(m_evaluationScore);
    std::vector<HexPoint> moves;
    while (true)
    {
        short s = *reinterpret_cast<const short*>(data);
        data += sizeof(short);
        HexPoint p = static_cast<HexPoint>(s);
        if (p == INVALID_POINT)
            break;
        moves.push_back(p);
    }
    m_children.SetChildren(moves);
    Validate();
}

void DfpnData::Rotate(const ConstBoard& brd)
{
    if (m_bestMove != INVALID_POINT)
        m_bestMove = BoardUtil::Rotate(brd, m_bestMove);
    m_maxProofSet = BoardUtil::Rotate(brd, m_maxProofSet);
    std::vector<HexPoint>& moves = m_children.m_children;
    for (std::size_t i = 0; i < moves.size(); ++i)
        moves[i] = BoardUtil::Rotate(brd, moves[i]);
}

bool DfpnData::ReplaceBy(const DfpnData& data) const
{
    if (m_bounds.IsSolved())
        return false;
    if (data.m_bounds.IsSolved())
        return true;
    return m_work < data.m_work;
}

//----------------------------------------------------------------------------

bool DfpnData::m_useTimeStamp;

DfpnSolver::DfpnSolver()
    : m_positions(0),
      m_useGuiFx(false),
      m_guiFxDeepBounds(false),
      m_timelimit(0.0),
      m_wideningBase(1),
      m_wideningFactor(0.25f),
      m_epsilon(0.0f),
      m_threads(1),
      m_threadWork(1000),
      m_db_bak_filename("db.dump"),
      m_db_bak_start(date(2012,2,11),hours(3)),
      m_db_bak_period(hours(-48)),
      m_tt_bak_filename("tt.dump"),
      m_tt_bak_start(date(2012,2,11),hours(3)),
      m_tt_bak_period(hours(-48)),
      m_guiFx(*this),
      m_allEvaluation(-2.5, 2.0, 45),
      m_allSolvedEvaluation(-2.5, 2.0, 45),
      m_winningEvaluation(-2.5, 2.0, 45),
      m_losingEvaluation(-2.5, 2.0, 45)
{
	DfpnData::m_useTimeStamp = false;
}

DfpnSolver::~DfpnSolver()
{
}

void DfpnSolver::PrintStatistics(HexColor winner,
                                 const PointSequence& pv) const
{
    std::ostringstream os;
    os << '\n'
       << SgWriteLabel("MID calls") << m_numMIDcalls << '\n'
       << SgWriteLabel("VC builds") << m_numVCbuilds << '\n'
       << SgWriteLabel("Terminal") << m_numTerminal << '\n'
       << SgWriteLabel("Work") << m_numMIDcalls + m_numTerminal << '\n'
       << SgWriteLabel("Wasted Work") << m_totalWastedWork
       << " (" << (double(m_totalWastedWork) * 100.0 
                   / double(m_numMIDcalls + m_numTerminal)) << "%)\n"
       << SgWriteLabel("Elapsed Time") << m_timer.GetTime() << '\n'
       << SgWriteLabel("MIDs/sec") 
       << double(m_numMIDcalls) / m_timer.GetTime() << '\n'
       << SgWriteLabel("VCs/sec")
       << double(m_numVCbuilds) / m_timer.GetTime() << '\n'
       << SgWriteLabel("Cnt prune sib") << m_prunedSiblingStats.Count() << '\n'
       << SgWriteLabel("Avg prune sib");
    m_prunedSiblingStats.Write(os);
    os << '\n' << SgWriteLabel("Consider Size");
    m_considerSetSize.Write(os);
    os << '\n' << SgWriteLabel("Move Index");
    m_moveOrderingIndex.Write(os);
    os << '\n' << SgWriteLabel("Move Percent");
    m_moveOrderingPercent.Write(os);
    os << '\n' << SgWriteLabel("Delta Increase");
    m_deltaIncrease.Write(os);
    os << '\n'
       << SgWriteLabel("Winner") << winner << '\n'
       << SgWriteLabel("PV") << HexPointUtil::ToString(pv) << '\n';
    if (m_positions->Database())
        os << '\n' << m_positions->Database()->GetStatistics().Write() << '\n';
    if (m_positions->HashTable())
        os << '\n' << *m_positions->HashTable() << '\n';
    LogInfo() << os.str();
}

std::string DfpnSolver::EvaluationInfo() const
{
    std::ostringstream os;
    os << "All:\n";
    m_allEvaluation.Write(os);
    os << "All Solved:\n";
    m_allSolvedEvaluation.Write(os);
    os << "Winning:\n";
    m_winningEvaluation.Write(os);
    os << "Losing:\n";
    m_losingEvaluation.Write(os);
    return os.str();
}

HexColor DfpnSolver::StartSearch(const HexState& state, HexBoard& board,
                                 DfpnStates& positions, PointSequence& pv)
{
    return StartSearch(state, board, positions, pv, 
                       DfpnBounds(DfpnBounds::MAX_WORK, DfpnBounds::MAX_WORK));
}

class RunDfpnThread
{
    DfpnSolver& solver;
    int id;
    const DfpnBounds& maxBounds;
    const HexState& state;
    HexBoard& board;
public:
    RunDfpnThread(DfpnSolver& solver, int id, const DfpnBounds& maxBounds,
                  const HexState& state, HexBoard& board)
        : solver(solver), id(id), maxBounds(maxBounds), state(state), board(board)
    { }

    void operator()()
    {
        solver.RunThread(id, maxBounds, state, board);
    }
};

HexColor DfpnSolver::StartSearch(const HexState& state, HexBoard& board,
                                 DfpnStates& positions, PointSequence& pv,
                                 const DfpnBounds& maxBounds)
{
    m_aborted = false;
    m_positions = &positions;
    m_numTerminal = 0;
    m_numMIDcalls = 0;
    m_numVCbuilds = 0;
    m_totalWastedWork = 0;
    m_prunedSiblingStats.Clear();
    m_moveOrderingPercent.Clear();
    m_moveOrderingIndex.Clear();
    m_considerSetSize.Clear();
    m_deltaIncrease.Clear();
    m_losingEvaluation.Clear();
    m_winningEvaluation.Clear();
    m_allEvaluation.Clear();
    m_allSolvedEvaluation.Clear();
    m_checkTimerAbortCalls = 0;

    // Skip search if already solved
    DfpnData data;
    if (DBRead(state, data) && data.m_bounds.IsSolved())
    {
        LogInfo() << "Already solved!\n";
        HexColor w = data.m_bounds.IsWinning()
            ? state.ToPlay() : !state.ToPlay();
        SolverDBUtil::GetVariation(state, *m_positions, pv);
        LogInfo() << w << " wins!\n";
        LogInfo() << "PV: " << HexPointUtil::ToString(pv) << '\n';
        return w;
    }
    DfpnData rotdata;
    StoneBoard rotbrd(state.Position());
    rotbrd.RotateBoard();
    HexState rotstate(rotbrd, state.ToPlay());
    if (DBRead(rotstate, rotdata) && rotdata.m_bounds.IsSolved()){
    	LogInfo() << "Rotation already solved!\n";
    	HexColor w = rotdata.m_bounds.IsWinning()
    	   ? rotstate.ToPlay() : !rotstate.ToPlay();
    	SolverDBUtil::GetVariation(rotstate, *m_positions, pv);
    	LogInfo() << w << " wins!\n";
    	LogInfo() << "PV: " << HexPointUtil::ToString(pv) << '\n';
    	return w;
    }

    m_timer.Start();
    if (m_useGuiFx)
    {
        m_guiFx.ClearChildren();
        m_guiFx.SetFirstPlayer(state.ToPlay());
    }
    boost::scoped_array<boost::thread> threads(new boost::thread[m_threads]);
    TryDoBackups(true); // only adjust start time for next backup
    for (int i = 0; i < m_threads; i++)
    {
        RunDfpnThread run(*this, i, maxBounds, state, board);
        threads[i] = boost::thread(run);
    }
    for (int i = 0; i < m_threads; i++)
        threads[i].join();
    m_timer.Stop();

    if (m_useGuiFx)
    {
        m_guiFx.SetPV();
        m_guiFx.WriteForced();
    }

    SolverDBUtil::GetVariation(state, *m_positions, pv);
    HexColor winner = EMPTY;
    if (DBRead(state, data) && data.m_bounds.IsSolved())
        winner = data.m_bounds.IsWinning()
            ? state.ToPlay() : !state.ToPlay();
    PrintStatistics(winner, pv);

    if (m_aborted)
        LogInfo() << "Search aborted.\n";
    return winner;
}

bool DfpnSolver::CheckAbort()
{
    boost::lock_guard<boost::mutex> lock(m_abort_mutex);
    if (!m_aborted)
    {
        if (SgUserAbort()) 
        {
            m_aborted = true;
            LogInfo() << "DfpnSolver::CheckAbort(): Abort flag!\n";
            m_nothingToSearch_cond.notify_all();
        }
        else if (m_timelimit > 0)
        {
            if (m_checkTimerAbortCalls == 0)
            {
                double elapsed = m_timer.GetTime();
                if (elapsed > m_timelimit)
                {
                    m_aborted = true;
                    LogInfo() << "DfpnSolver::CheckAbort(): Timelimit!\n";
                    m_nothingToSearch_cond.notify_all();
                }
                else
                {
                    if (m_numMIDcalls < 100)
                        m_checkTimerAbortCalls = 10;
                    else
                    {
                        size_t midsPerSec = static_cast<size_t>
                            (double(m_numMIDcalls) / elapsed);
                        m_checkTimerAbortCalls = midsPerSec / 2;
                    }
                }
            }
            else
                --m_checkTimerAbortCalls;
        }
    }
    return m_aborted;
}

void DfpnSolver::StoreVBounds(int id, size_t depth, TopMidData* d)
{
    BenzeneAssert(d);
    m_vtt.Store(id, depth, d->hash, d->vBounds);
    d = d->parent;
    if (!d)
        return;
    size_t maxChildIndex = ComputeMaxChildIndex(d->virtualBounds);
    UpdateBounds(d->vBounds, d->virtualBounds, maxChildIndex);
    StoreVBounds(id, depth - 1, d);
}

size_t DfpnSolver::TopMid(const DfpnBounds& maxBounds,
                          DfpnData& data, DfpnBounds& vBounds,
                          TopMidData* parent, bool& midCalled)
{
    BenzeneAssert(!midCalled);
    size_t depth = m_history->Depth();
    if (!maxBounds.GreaterThan(vBounds))
        return 0;

    size_t work = 0;
    TopMidData d(data, vBounds, m_state->Hash(), parent);

    if (data.m_work < m_threadWork &&
        (depth != 0 || (data.m_work == 0)))
    {
        if (vBounds.phi <= vBounds.delta)
            DfpnBounds::SetToWinning(vBounds);
        else
            DfpnBounds::SetToLosing(vBounds);
        m_thread_path_solved[*m_thread_id] = false;
        StoreVBounds(*m_thread_id, depth, &d);
        m_topmid_mutex.unlock();
        {
            std::vector<std::pair<HexPoint, DfpnBounds> > pv;
            for (TopMidData* d = parent; d; d = d->parent)
                pv.push_back(std::make_pair(d->data.m_children.FirstMove(d->bestIndex),
                                            d->childrenData[d->bestIndex].GetBounds()));
            reverse(pv.begin(), pv.end());
            if (m_useGuiFx)
            {
                m_guiFx.SetPV(pv);
                m_guiFx.Write();
            }
            for (size_t i = 0; i < pv.size(); ++i)
            {
                if (i)
                    LogDfpnThread() << ' ';
                LogDfpnThread() << pv[i].first;
            }
            LogDfpnThread() << '\n';
        }
        work = MID(maxBounds, depth == 0 ? 1 : m_threadWork, data);
        midCalled = true;
        m_topmid_mutex.lock();
        vBounds = data.m_bounds;
        DBWrite(*m_state, data);
        m_vtt.Remove(*m_thread_id, depth, d.hash, data.m_bounds,
                     data.m_bounds.IsSolved(), m_thread_path_solved);
        return work;
    }

    BenzeneAssert(data.IsValid());

    d.bestIndex = data.m_children.MoveIndex(data.m_bestMove);

    d.childrenData.resize(data.m_children.Size());
    d.virtualBounds.resize(data.m_children.Size());

    bool first = true;
    while (true)
    {
        if (first || midCalled)
        {
            LookupChildrenDB(d.childrenData, data.m_children);
            LookupChildren(depth + 1, d.virtualBounds,
                        d.childrenData, data.m_children);
            first = false;
        }

        if (m_useGuiFx && depth == 0)
            m_guiFx.SetChildrenOnce(data.m_children, d.childrenData);

        size_t maxChildIndex = ComputeMaxChildIndex(d.childrenData);
        UpdateBounds(data.m_bounds, d.childrenData, maxChildIndex);

        size_t virtualMaxChildIndex = ComputeMaxChildIndex(d.virtualBounds);
        UpdateBounds(vBounds, d.virtualBounds, virtualMaxChildIndex);

        if (midCalled)
            break;
        if (!maxBounds.GreaterThan(vBounds))
        {
            m_topmid_tt[d.hash] = vBounds;
            break;
        }
        if (CheckAbort())
            break;

        if (m_useGuiFx && depth == 1)
            m_guiFx.UpdateBounds(m_history->LastMove(), data.m_bounds);

        DfpnBounds childMaxBounds;
        SelectChild(d.bestIndex, childMaxBounds, vBounds,
                    d.virtualBounds, maxBounds, virtualMaxChildIndex);

        data.m_bestMove = data.m_children.FirstMove(d.bestIndex);
        data.m_children.PlayMove(d.bestIndex, *m_state);
        m_history->Push(data.m_bestMove, d.hash);
        work += TopMid(childMaxBounds, d.childrenData[d.bestIndex],
                       d.virtualBounds[d.bestIndex], &d, midCalled);
        m_history->Pop();
        data.m_children.UndoMove(d.bestIndex, *m_state);

        UpdateStatsOnWin(d.childrenData, d.bestIndex, data.m_work + work);

        bitset_t toPrune =
            ChildrenToPrune(data.m_children, data.m_bestMove,
                            d.childrenData[d.bestIndex].m_maxProofSet);
        if (toPrune.any())
        {
            VecPrune(data.m_children.m_children, toPrune);
            d.bestIndex = data.m_children.MoveIndex(data.m_bestMove);
            VecPrune(d.childrenData, toPrune);
            if (!midCalled)
                VecPrune(d.virtualBounds, toPrune);
            if (m_useGuiFx && depth == 0)
                m_guiFx.SetChildren(data.m_children, d.childrenData);
        }
    }

    UpdateSolvedBestMove(data, d.childrenData);

    data.m_work += work;
    if (data.m_bounds.IsSolved())
        NotifyListeners(*m_history, data);
    DBWrite(*m_state, data);
    if (midCalled)
        m_vtt.Remove(*m_thread_id, depth, d.hash, vBounds,
                     data.m_bounds.IsSolved(), m_thread_path_solved);
    if (m_useGuiFx && depth == 1)
        m_guiFx.UpdateBounds(m_history->LastMove(), data.m_bounds);
    return work;
}

void DfpnSolver::RunThread(int id, const DfpnBounds& maxBounds,
                           const HexState& state, HexBoard& board)
{
    m_state.reset(new HexState(state));
    m_workBoard.reset(new HexBoard(board));
    m_history.reset(new DfpnHistory());
    m_thread_id.reset(new int(id));

    DfpnData data;
    DfpnBounds vBounds;

    bool wasWaiting = false;
    while (true)
    {
        TryDoBackups();

        boost::unique_lock<boost::mutex> lock(m_topmid_mutex);

        m_topmid_tt.clear();
        DBRead(*m_state, data);
        vBounds = data.m_bounds;
        m_vtt.Lookup(0, m_state->Hash(), vBounds);
        bool midCalled = false;
        TopMid(maxBounds, data, vBounds, 0, midCalled);
        if (m_aborted || !maxBounds.GreaterThan(data.m_bounds))
            break;
        if (!midCalled)
        {
            if (!wasWaiting)
                LogDfpnThread() << "waiting\n";
            wasWaiting = true;
            if (m_useGuiFx)
                m_guiFx.Write();
            m_nothingToSearch_cond.wait(lock);
        }
        else
        {
            m_nothingToSearch_cond.notify_all();
            wasWaiting = false;
        }
    }

    m_nothingToSearch_cond.notify_all();
}

size_t DfpnSolver::CreateData(DfpnData& data)
{
    if (data.IsValid())
        return 0;

    HexColor colorToMove = m_state->ToPlay();

    m_workBoard->GetPosition().SetPosition(m_state->Position());
    m_workBoard->ComputeAll(colorToMove);
    ++m_numVCbuilds;

    data.Validate();

    // Compute the maximum possible proof set if colorToMove wins.
    // This data is used to prune siblings of this state.
    data.m_maxProofSet =
        ProofUtil::MaximumProofSet(*m_workBoard, colorToMove);

    if (EndgameUtil::IsDeterminedState(*m_workBoard, colorToMove))
    {
        ++m_numTerminal;
        if (EndgameUtil::IsWonGame(*m_workBoard, colorToMove))
            DfpnBounds::SetToWinning(data.m_bounds);
        else
            DfpnBounds::SetToLosing(data.m_bounds);

        if (m_useGuiFx && m_history->Depth() == 1)
        {
            m_guiFx.UpdateBounds(m_history->LastMove(), data.m_bounds);
            m_guiFx.Write();
        }
        data.m_bestMove = INVALID_POINT;
        data.m_work = 0;
        data.m_evaluationScore = 0.0;
        return 1;
    }

    bitset_t childrenBitset =
        EndgameUtil::MovesToConsider(*m_workBoard, colorToMove);

    m_considerSetSize.Add(float(childrenBitset.count()));
    Resistance resist;
    resist.Evaluate(*m_workBoard);
    data.m_evaluationScore = (colorToMove == BLACK)
        ? resist.Score() : -resist.Score();
    m_allEvaluation.Add(data.m_evaluationScore);
    std::vector<std::pair<HexEval, HexPoint> > mvsc;
    for (BitsetIterator it(childrenBitset); it; ++it)
    {
        HexEval score = -resist.Score(*it);
        mvsc.push_back(std::make_pair(-score, *it));
    }
    stable_sort(mvsc.begin(), mvsc.end());
    std::vector<HexPoint> sortedChildren;
    for (size_t i = 0; i < mvsc.size(); ++i)
        sortedChildren.push_back(mvsc[i].second);
    data.m_bestMove = sortedChildren[0];
    data.m_children.SetChildren(sortedChildren);
    return 1;
}

void DfpnSolver::UpdateStatsOnWin(const std::vector<DfpnData>& childrenData,
                                  size_t bestIndex, size_t work)
{
    if (!childrenData[bestIndex].m_bounds.IsLosing())
        return;
    m_moveOrderingIndex.Add(float(bestIndex));
    m_moveOrderingPercent.Add(float(bestIndex) / (float)childrenData.size());
    m_totalWastedWork += work - childrenData[bestIndex].m_work;
}

bitset_t DfpnSolver::ChildrenToPrune(DfpnChildren& children,
                                     HexPoint bestMove, bitset_t maxProofSet)
{
    bitset_t res;
    maxProofSet.set(bestMove);
    for (size_t i = 0; i < children.Size(); ++i)
        if (!maxProofSet.test(children.FirstMove(i)))
            res.set(i);
    if (res.any())
        m_prunedSiblingStats.Add(float(res.count()));
    return res;
}

void DfpnSolver::UpdateSolvedBestMove(DfpnData& data,
                                      const std::vector<DfpnData>& childrenData)
{
    if (!data.m_bounds.IsSolved())
        return;
    // Find the most delaying move for losing states, and the smallest
    // winning move for winning states.
    m_allSolvedEvaluation.Add(data.m_evaluationScore);
    if (data.m_bounds.IsLosing())
    {
        m_losingEvaluation.Add(data.m_evaluationScore);
        std::size_t maxWork = 0;
        for (std::size_t i = 0; i < data.m_children.Size(); ++i)
        {
            if (childrenData[i].m_work > maxWork)
            {
                maxWork = childrenData[i].m_work;
                data.m_bestMove = data.m_children.FirstMove(i);
            }
        }
    }
    else
    {
        m_winningEvaluation.Add(data.m_evaluationScore);
        std::size_t minWork = DfpnBounds::INFTY;
        for (std::size_t i = 0; i < data.m_children.Size(); ++i)
        {
            if (childrenData[i].m_bounds.IsLosing()
                && childrenData[i].m_work < minWork)
            {
                minWork = childrenData[i].m_work;
                data.m_bestMove = data.m_children.FirstMove(i);
            }
        }
    }
}

size_t DfpnSolver::MID(const DfpnBounds& maxBounds,
                       const size_t workBound, DfpnData& data)
{
    maxBounds.CheckConsistency();
    BenzeneAssert(maxBounds.phi > 1);
    BenzeneAssert(maxBounds.delta > 1);

    int depth = m_history->Depth();
    size_t work = CreateData(data);
    data.m_work += work;

    if (!maxBounds.GreaterThan(data.m_bounds))
    {
        if (work)
        {
            if (data.m_bounds.IsSolved())
                NotifyListeners(*m_history, data);
            TTWrite(*m_state, data);
        }
        return work;
    }

    ++m_numMIDcalls;

    std::vector<DfpnData> childrenData(data.m_children.Size());
    LookupChildrenTT(childrenData, data.m_children);

    size_t bestIndex = data.m_children.MoveIndex(data.m_bestMove);

    SgHashCode currentHash = m_state->Hash();
    do
    {
        // Index used for progressive widening
        size_t maxChildIndex = ComputeMaxChildIndex(childrenData);

        if (m_useGuiFx && depth == 0)
            m_guiFx.SetChildren(data.m_children, childrenData);

        UpdateBounds(data.m_bounds, childrenData, maxChildIndex);

        UpdateSolvedBestMove(data, childrenData);

        if (data.m_bounds.IsSolved())
            NotifyListeners(*m_history, data);
        TTWrite(*m_state, data);

        if (m_useGuiFx && depth == 1)
        {
            m_guiFx.UpdateBounds(m_history->LastMove(), data.m_bounds);
            m_guiFx.Write();
        }

        if (!maxBounds.GreaterThan(data.m_bounds))
            break;

        if (work >= workBound)
            break;

        // Select most proving child
        DfpnBounds childMaxBounds;
        SelectChild(bestIndex, childMaxBounds, data.m_bounds,
                    childrenData, maxBounds, maxChildIndex);
        data.m_bestMove = data.m_children.FirstMove(bestIndex);
        // Recurse on best child
        if (m_useGuiFx && depth == 0)
            m_guiFx.SetPV(data.m_bestMove, childrenData[bestIndex].GetBounds());
        data.m_children.PlayMove(bestIndex, *m_state);
        m_history->Push(data.m_bestMove, currentHash);
        size_t childWork = MID(childMaxBounds, workBound - work,
                               childrenData[bestIndex]);
        work += childWork;
        data.m_work += childWork;
        m_history->Pop();
        data.m_children.UndoMove(bestIndex, *m_state);

        if (m_useGuiFx && depth == 0)
            m_guiFx.SetPV();

        UpdateStatsOnWin(childrenData, bestIndex, data.m_work + work);

        bitset_t toPrune =
            ChildrenToPrune(data.m_children, data.m_bestMove,
                            childrenData[bestIndex].m_maxProofSet);
        if (toPrune.any())
        {
            VecPrune(data.m_children.m_children, toPrune);
            bestIndex = data.m_children.MoveIndex(data.m_bestMove);
            VecPrune(childrenData, toPrune);
            if (m_useGuiFx && m_history->Depth() == 0)
                m_guiFx.SetChildren(data.m_children, childrenData);
        }
    } while (!m_thread_path_solved[*m_thread_id] && !CheckAbort());

    if (m_useGuiFx && depth == 0)
        m_guiFx.WriteForced();

    return work;
}

template <class T>
size_t DfpnSolver::ComputeMaxChildIndex(const std::vector<T>&
                                        childrenBounds) const
{
    BenzeneAssert(!childrenBounds.empty());

    int numNonLosingChildren = 0;
    for (size_t i = 0; i < childrenBounds.size(); ++i)
        if (!childrenBounds[i].GetBounds().IsWinning())
            ++numNonLosingChildren;
    if (numNonLosingChildren < 2)
        return childrenBounds.size();

    // this needs experimenting!
    int childrenToLookAt = WideningBase() 
        + int(ceil(float(numNonLosingChildren) * WideningFactor()));
    // Must examine at least two children when have two or more live,
    // since otherwise delta2 will be set to infinity in SelectChild.
    BenzeneAssert(childrenToLookAt >= 1);

    int numNonLosingSeen = 0;
    for (size_t i = 0; i < childrenBounds.size(); ++i)
    {
        if (!childrenBounds[i].GetBounds().IsWinning())
            if (++numNonLosingSeen == childrenToLookAt)
                return i + 1;
    }
    return childrenBounds.size();
}

void DfpnSolver::NotifyListeners(const DfpnHistory& history,
                                 const DfpnData& data)
{
    boost::lock_guard<boost::mutex> lock(m_listeners_mutex);
    for (std::size_t i = 0; i < m_listener.size(); ++i)
        m_listener[i]->StateSolved(history, data);
}

inline DfpnBoundType DfpnSolver::GetDeltaBound(DfpnBoundType delta) const
{
    return std::max(delta + 1, DfpnBoundType(delta * (1.0 + m_epsilon)));
}

template <class T>
void DfpnSolver::SelectChild(size_t& bestIndex, DfpnBounds& childMaxBounds,
                             const DfpnBounds& currentBounds,
                             const std::vector<T>& childrenBounds,
                             const DfpnBounds& maxBounds,
                             size_t maxChildIndex)
{
    DfpnBoundType delta2 = DfpnBounds::INFTY;
    DfpnBoundType delta1 = DfpnBounds::INFTY;

    BenzeneAssert(1 <= maxChildIndex && maxChildIndex <= childrenBounds.size());
    size_t cand = bestIndex;
    for (size_t i = 0; i < maxChildIndex; ++i)
    {
        const DfpnBounds& child = childrenBounds[i].GetBounds();

        // Store the child with smallest delta and record 2nd smallest delta
        if (child.delta < delta1)
        {
            delta2 = delta1;
            delta1 = child.delta;
            cand = i;
        }
        else if (child.delta < delta2)
        {
            delta2 = child.delta;
        }

        // Winning move found
        if (child.IsLosing())
            break;
    }
    BenzeneAssert(delta1 < DfpnBounds::INFTY);
    bool useDelta2 = true;
    if (cand != bestIndex)
    {
        childMaxBounds.delta = std::min(maxBounds.phi, GetDeltaBound(delta1));
        if (bestIndex >= maxChildIndex ||
            childrenBounds[bestIndex].GetBounds().delta >= childMaxBounds.delta)
            bestIndex = cand;
        else
            useDelta2 = false;
    }
    if (useDelta2)
    {
        childMaxBounds.delta =
            delta2 < DfpnBounds::INFTY ? GetDeltaBound(delta2) : delta2;
        childMaxBounds.delta = std::min(maxBounds.phi, childMaxBounds.delta);
    }

    // Compute maximum bound for child
    const DfpnBounds childBounds(childrenBounds[bestIndex].GetBounds());
    childMaxBounds.phi = maxBounds.delta - (currentBounds.delta - childBounds.phi);
    BenzeneAssert(childMaxBounds.GreaterThan(childBounds));
    if (delta2 != DfpnBounds::INFTY)
        m_deltaIncrease.Add(float(childMaxBounds.delta-childBounds.delta));
}

template <class T>
void DfpnSolver::UpdateBounds(DfpnBounds& bounds,
                              const std::vector<T>& childrenBounds,
                              size_t maxChildIndex) const
{
    DfpnBounds boundsAll(DfpnBounds::INFTY, 0);
    BenzeneAssert(1 <= maxChildIndex && maxChildIndex <= childrenBounds.size());
    for (std::size_t i = 0; i < childrenBounds.size(); ++i)
    {
        const DfpnBounds& childBounds = childrenBounds[i].GetBounds();
        // Abort on losing child (a winning move)
        if (childBounds.IsLosing())
        {
            DfpnBounds::SetToWinning(bounds);
            return;
        }
        if (i < maxChildIndex)
            boundsAll.phi = std::min(boundsAll.phi, childBounds.delta);
        BenzeneAssert(childBounds.phi != DfpnBounds::INFTY);
        boundsAll.delta += childBounds.phi;
    }
    if (boundsAll.phi == DfpnBounds::INFTY)
        boundsAll.delta = 0;
    bounds = boundsAll;
}

void DfpnSolver::LookupDataTT(DfpnData& data, const DfpnChildren& children, 
                            std::size_t childIndex, HexState& state)
{
    children.PlayMove(childIndex, state);
    TTReadNoLock(state, data);
    children.UndoMove(childIndex, state);
}

bool DfpnSolver::LookupDataDB(DfpnData& data, const DfpnChildren& children,
                              std::size_t childIndex, HexState& state)
{
    children.PlayMove(childIndex, state);
    bool res = DBRead(state, data);
    children.UndoMove(childIndex, state);
    return res;
}

void DfpnSolver::LookupChildrenTT(std::vector<DfpnData>& childrenData,
                                const DfpnChildren& children)
{
    boost::shared_lock<boost::shared_mutex> lock(m_tt_mutex);
    for (size_t i = 0; i < children.Size(); ++i)
        LookupDataTT(childrenData[i], children, i, *m_state);
}

void DfpnSolver::LookupChildrenDB(std::vector<DfpnData>& childrenData,
                                  const DfpnChildren& children)
{
    std::vector<bool> dbRead(children.Size());
    bool allRead = true;
    for (size_t i = 0; i < children.Size(); ++i)
    {
        dbRead[i] = LookupDataDB(childrenData[i], children, i, *m_state);
        allRead &= dbRead[i];
    }
    if (allRead)
        return;
    boost::shared_lock<boost::shared_mutex> lock(m_tt_mutex);
    for (size_t i = 0; i < children.Size(); ++i)
        if (!dbRead[i])
            LookupDataTT(childrenData[i], children, i, *m_state);
}

void DfpnSolver::LookupChildren(size_t depth,
                                std::vector<DfpnBounds>& virtualBounds,
                                const std::vector<DfpnData>& childrenData,
                                const DfpnChildren& children)
{
    for (size_t i = 0; i < children.Size(); ++i)
    {
        virtualBounds[i] = childrenData[i].GetBounds();
        if (virtualBounds[i].IsSolved())
            continue;
        children.PlayMove(i, *m_state);
        map<SgHashCode, DfpnBounds>::const_iterator it = m_topmid_tt.find(m_state->Hash());
        if (it == m_topmid_tt.end())
            m_vtt.Lookup(depth, m_state->Hash(), virtualBounds[i]);
        else
            virtualBounds[i] = it->second;
        children.UndoMove(i, *m_state);
    }
}

bool DfpnSolver::TTReadNoLock(const HexState& state, DfpnData& data)
{
    return m_positions->GetHT(state, data);
}

void DfpnSolver::TTWrite(const HexState& state, DfpnData& data)
{
    data.m_bounds.CheckConsistency();
    boost::upgrade_lock<boost::shared_mutex> lock(m_tt_mutex);
    DfpnData prev;
    bool hit = m_positions->GetHT(state, prev);
    if (hit && prev.m_bounds.IsSolved())
    {
        data = prev;
        return;
    }
    boost::upgrade_to_unique_lock<boost::shared_mutex> unique_lock(lock);
    m_positions->PutHT(state, data);
}

bool DfpnSolver::TTRead(const HexState& state, DfpnData& data)
{
    boost::shared_lock<boost::shared_mutex> lock(m_tt_mutex);
    return TTReadNoLock(state, data);
}

void DfpnSolver::DBWrite(const HexState& state, DfpnData& data)
{
    TTWrite(state, data);
    DfpnData prev;
    bool hit = m_positions->GetDB(state, prev);
    if (hit && prev.m_bounds.IsSolved())
    {
        data = prev;
        return;
    }
    m_positions->PutDB(state, data);
}

bool DfpnSolver::DBRead(const HexState& state, DfpnData& data)
{
    if (m_positions->GetDB(state, data))
        return true;
    return TTRead(state, data);
}

//----------------------------------------------------------------------------

void DfpnSolver::PropagateBackwards(const Game& game, DfpnStates& pos)
{
    HexState state(game.Board(), BLACK);
    MoveSequence history(game.History());
    if (history.empty())
        return;
    do
    {
        HexPoint cell = history.back().Point();
        state.UndoMove(cell);
        state.SetToPlay(history.back().Color());
        history.pop_back();
        DfpnData data;
        if (!pos.Get(state, data))
            break;
        if (data.m_bounds.IsSolved())
            break;
        std::vector<DfpnData> childrenData(data.m_children.Size());
        for (size_t i = 0; i < data.m_children.Size(); ++i)
            LookupDataDB(childrenData[i], data.m_children, i, state);
        size_t maxChildIndex = ComputeMaxChildIndex(childrenData);
        UpdateBounds(data.m_bounds, childrenData, maxChildIndex);
        pos.Put(state, data);
    } while(!history.empty());
}

//----------------------------------------------------------------------------

void DfpnSolver::DbDump(DfpnStates& positions)
{
    if (!positions.UseDatabase())
        throw BenzeneException("No open database!\n");
    if (m_db_bak_filename.empty())
        throw BenzeneException("Db backup filename is empty!\n");
    ofstream os(m_db_bak_filename.c_str(), ios::out|ios::binary|ios::trunc);
    if (!os.is_open())
        throw BenzeneException() << "Error creating file '" << m_db_bak_filename << "'\n";
    positions.Database()->Dump(os);
}

void DfpnSolver::DbRestore(DfpnStates& positions)
{
    if (!positions.UseDatabase())
        throw BenzeneException("No open database!\n");
    if (m_db_bak_filename.empty())
        throw BenzeneException("Db backup filename is empty!\n");
    ifstream is(m_db_bak_filename.c_str(), ios::in|ios::binary);
    if (!is.is_open())
        throw BenzeneException() << "Error opening file '" << m_db_bak_filename << "'\n";
    positions.Database()->Restore(is);
}

void DfpnSolver::TtDump(DfpnStates& positions, bool locked)
{
    SgTimer timer;
    timer.Start();
    if (!positions.UseHashTable())
        throw BenzeneException("No tt used!\n");
    if (m_tt_bak_filename.empty())
        throw BenzeneException("Tt backup filename is empty!\n");
    if (locked)
        m_tt_mutex.unlock_shared();
    ofstream os(m_tt_bak_filename.c_str(), ios::out|ios::binary|ios::trunc);
    if (!os.is_open())
        throw BenzeneException() << "Error creating file '" << m_tt_bak_filename << "'\n";
    size_t count = 0;
    if (locked)
        m_tt_mutex.lock_shared();
    for (DfpnHashTable::Iterator i(*positions.HashTable()); i; ++i)
    {
        SgHashCode k = i->m_hash;
        int size = i->m_data.PackedSize();
        boost::scoped_array<byte> data(new byte[size]);
        i->m_data.Pack(data.get());
        if (locked)
            m_tt_mutex.unlock_shared();
        os.write(reinterpret_cast<const char *>(&k), sizeof(k));
        os.write(reinterpret_cast<const char *>(data.get()), size);
        if (os.bad())
            throw BenzeneException() << "Error writing to file '" << m_tt_bak_filename << "'\n";
        count++;
        if (locked)
            m_tt_mutex.lock_shared();
    }
    timer.Stop();
    LogDfpnThread()
        << "Tt dump: #entries=" << count
        << " time=" << timer.GetTime() << '\n';
}

void DfpnSolver::TtRestore(DfpnStates& positions)
{
    SgTimer timer;
    timer.Start();
    if (!positions.UseHashTable())
        throw BenzeneException("No tt used!\n");
    if (m_tt_bak_filename.empty())
        throw BenzeneException("Tt backup filename is empty!\n");
    ifstream is(m_tt_bak_filename.c_str(), ios::in|ios::binary|ios::ate);
    if (!is.is_open())
        throw BenzeneException() << "Error opening file '" << m_tt_bak_filename << "'\n";
    ifstream::pos_type size = is.tellg();
    is.seekg(0, ios::beg);
    static const size_t SIZE = 1 << 20;
    size_t pos = 0;
    size_t in = 0;
    size_t count = 0;
    byte buf[2 * SIZE];
    while (in > 0 || is.tellg() < size)
    {
        if (in < SIZE && is.tellg() < size)
        {
            if (pos > 0)
            {
                memmove(buf, buf + pos, in);
                pos = 0;
            }
            size_t to_read = min(SIZE, size_t(size - is.tellg()));
            is.read(reinterpret_cast<char *>(buf + in), to_read);
            in += to_read;
        }
        SgHashCode k = *reinterpret_cast<SgHashCode*>(buf + pos);
        pos += sizeof(k);
        in -= sizeof(k);
        DfpnData d;
        d.Unpack(buf + pos);
        positions.HashTable()->Store(k, d);
        pos += d.PackedSize();
        in -= d.PackedSize();
        count++;
    }
    timer.Stop();
    LogDfpnThread()
        << "Tt restore: #entries=" << count
        << " time=" << timer.GetTime() << '\n';
}

void DfpnSolver::TryDoBackups(bool adjust_start)
{
    boost::unique_lock<boost::mutex> lock(m_backup_mutex, boost::try_to_lock);
    if (!lock)
        return;
    if (!m_db_bak_period.is_negative())
    {
        bool backup = false;
        if (!adjust_start)
        {
            if (second_clock::local_time() >= m_db_bak_start)
            {
                backup = true;
                m_db_bak_start += m_db_bak_period;
                try {
                    boost::lock_guard<boost::mutex> dblock(m_topmid_mutex);
                    BenzeneAssert(m_positions);
                    DbDump(*m_positions);
                } catch (BenzeneException& e) {
                    LogSevere() << "DbDump(): " << e.what();
                }
            }
        }
        if (adjust_start || backup)
        {
            ptime t(second_clock::local_time());
            while (m_db_bak_start < t)
                m_db_bak_start += m_db_bak_period;
        }
    }
    if (!m_tt_bak_period.is_negative())
    {
        bool backup = false;
        if (!adjust_start)
        {
            if (second_clock::local_time() >= m_tt_bak_start)
            {
                backup = true;
                m_tt_bak_start += m_tt_bak_period;
                try {
                    boost::shared_lock<boost::shared_mutex> lock(m_tt_mutex);
                    BenzeneAssert(m_positions);
                    TtDump(*m_positions, true);
                } catch (BenzeneException& e) {
                    LogSevere() << "TtDump(): " << e.what();
                }
            }
        }
        if (adjust_start || backup)
        {
            ptime t(second_clock::local_time());
            while (m_tt_bak_start < t)
                m_tt_bak_start += m_tt_bak_period;
        }
    }
}

//----------------------------------------------------------------------------
