//----------------------------------------------------------------------------
/** @file VCS.cpp */
//----------------------------------------------------------------------------

#include "SgSystem.h"
#include "SgTimer.h"

#include "Hex.hpp"
#include "BitsetIterator.hpp"
#include "Misc.hpp"
#include "VCS.hpp"
#include "VCOr.hpp"
#include "VCPattern.hpp"

using namespace benzene;

//----------------------------------------------------------------------------

VCBuilderParam::VCBuilderParam()
    : and_over_edge(false),
      use_patterns(false),
      use_non_edge_patterns(true),
      incremental_builds(true),
      limit_fulls(true),
      limit_or(true)
{
}

//----------------------------------------------------------------------------

inline CarrierList::CarrierList()
{
}

inline CarrierList::CarrierList(bitset_t carrier)
    : m_list(1, carrier)
{
}

inline CarrierList::CarrierList(const std::vector<bitset_t>& carriers_list)
{
    m_list.reserve(carriers_list.size());
    for (std::size_t i = 0; i < carriers_list.size(); ++i)
        AddNew(carriers_list[i]);
}

inline void CarrierList::AddNew(bitset_t carrier)
{
    m_list.push_back(carrier);
}

inline bool CarrierList::SupersetOfAny(bitset_t carrier) const
{
    for (std::size_t i = 0; i < m_list.size(); ++i)
        if (BitsetUtil::IsSubsetOf(m_list[i].carrier, carrier))
        {
            // Move to front
            Elem c = m_list[i];
            for (std::size_t j = i; j > 0; j--)
                m_list[j] = m_list[j - 1];
            m_list[0] = c;
            return true;
        }
    return false;
}

inline bool CarrierList::RemoveSupersetsOfCheckAnyRemoved(const CarrierList& filter)
{
    std::size_t j = 0;
    std::size_t i = 0;
    for (; i < m_list.size(); ++i)
    {
        if (!filter.SupersetOfAny(m_list[i].carrier))
            m_list[j++] = m_list[i];
    }
    m_list.resize(j);
    return j < i;
}

template <bool check_old>
inline bool CarrierList::RemoveSupersetsOf(bitset_t carrier)
{
    std::size_t j = 0;
    std::size_t i = 0;
    bool check_old_res = false;
    for (; i < m_list.size(); ++i)
    {
        if (!BitsetUtil::IsSubsetOf(carrier, m_list[i].carrier))
            m_list[j++] = m_list[i];
        else if (check_old)
        {
            if (m_list[i].old)
                check_old_res = true;
        }
    }
    m_list.resize(j);
    if (check_old)
        return check_old_res;
    else
        return j < i;
}

template <bool store_removed>
inline size_t CarrierList::RemoveAllContaining_(bitset_t set,
                                                std::vector<bitset_t>* removed)
{
    std::size_t j = 0;
    std::size_t i = 0;
    for (; i < m_list.size(); ++i)
    {
        if ((set & m_list[i].carrier).none())
            m_list[j++] = m_list[i];
        else if (store_removed)
            removed->push_back(m_list[i].carrier);
    }
    m_list.resize(j);
    return i - j;
}

inline size_t CarrierList::RemoveAllContaining(bitset_t set)
{
    return RemoveAllContaining_<false>(set, 0);
}

inline size_t CarrierList::RemoveAllContaining(bitset_t set,
                                               std::vector<bitset_t>& removed)
{
    return RemoveAllContaining_<true>(set, &removed);
}

inline bool CarrierList::RemoveSupersetsOfCheckOldRemoved(bitset_t carrier)
{
    return RemoveSupersetsOf<true>(carrier);
}

inline bool CarrierList::RemoveSupersetsOfCheckAnyRemoved(bitset_t carrier)
{
    return RemoveSupersetsOf<false>(carrier);
}

inline void CarrierList::RemoveSupersetsOfUnchecked(bitset_t carrier)
{
    RemoveSupersetsOf<false>(carrier);
}

inline bool CarrierList::TrySetOld(bitset_t carrier) const
{
    for (std::size_t i = 0; i < m_list.size(); ++i)
        if (m_list[i].carrier == carrier)
        {
            m_list[i].old = true;
            return true;
        }
    return false;
}

bitset_t CarrierList::GetGreedyUnion() const
{
    bitset_t U;
    bitset_t I;
    I.set();
    for (Iterator i(*this); i; ++i)
    {
        if ((I & i.Carrier()) != I)
        {
            I &= i.Carrier();
            U |= i.Carrier();
        }
    }
    return U;
}

inline bitset_t CarrierList::GetOldIntersection() const
{
    bitset_t I;
    I.set();
    for (Iterator i(*this); i; ++i)
        if (i.Old())
            I &= i.Carrier();
    return I;
}

inline bitset_t CarrierList::GetAllIntersection() const
{
    bitset_t I;
    I.set();
    for (Iterator i(*this); i; ++i)
        I &= i.Carrier();
    return I;
}

inline bitset_t CarrierList::GetNewIntersection() const
{
    bitset_t I;
    I.set();
    for (Iterator i(*this); i; ++i)
        if (!i.Old())
            I &= i.Carrier();
    return I;
}

inline void CarrierList::MarkAllOld()
{
    for (std::size_t i = 0; i < m_list.size(); i++)
        m_list[i].old = true;
}

inline void CarrierList::MarkAllNew()
{
    for (std::size_t i = 0; i < m_list.size(); i++)
        m_list[i].old = false;
}

inline void CarrierList::Clear()
{
    m_list.clear();
}

//----------------------------------------------------------------------------

inline VCS::Ends::Ends(HexPoint x, HexPoint y)
    : x(x), y(y)
{
}

inline VCS::Full::Full(HexPoint x, HexPoint y, bitset_t carrier)
    : Ends(x, y), carrier(carrier)
{
}

inline VCS::Semi::Semi(HexPoint x, HexPoint y, bitset_t carrier, HexPoint key)
    : Full(x, y, carrier), key(key)
{
}

//----------------------------------------------------------------------------

inline VCS::AndList::AndList()
{
    m_processed_intersection.set();
}

inline VCS::AndList::AndList(bitset_t carrier)
    : CarrierList(carrier)
{
    m_processed_intersection.set();
}

inline VCS::AndList::AndList(const std::vector<bitset_t>& carriers_list)
    : CarrierList(carriers_list)
{
    m_processed_intersection.set();
}

inline bool VCS::AndList::RemoveSupersetsOf(bitset_t carrier)
{
    if (RemoveSupersetsOfCheckOldRemoved(carrier))
    {
        CalcIntersection();
        return true;
    }
    return false;
}

inline void VCS::AndList::Add(bitset_t carrier)
{
    RemoveSupersetsOf(carrier);
    AddNew(carrier);
}

inline bool VCS::AndList::TryAdd(bitset_t carrier, bool limit)
{
    if (SupersetOfAny(carrier))
        return false;
    if (limit)
    {
        if (RemoveSupersetsOf(carrier))
        {
            AddNew(carrier);
            return true;
        }
        else if (!BitsetUtil::IsSubsetOf(GetAllIntersection(), carrier))
        {
            AddNew(carrier);
            return true;
        }
        return false;
    }
    Add(carrier);
    return true;
}

inline bitset_t VCS::AndList::GetIntersection() const
{
    return m_processed_intersection;
}

inline bool VCS::AndList::TrySetProcessed(bitset_t carrier)
{
    if (TrySetOld(carrier))
    {
        m_processed_intersection &= carrier;
        return true;
    }
    else
        return false;
}

inline void VCS::AndList::MarkAllUnprocessed()
{
    MarkAllNew();
    m_processed_intersection.set();
}

inline void VCS::AndList::CalcIntersection()
{
    m_processed_intersection = GetOldIntersection();
}

//----------------------------------------------------------------------------

inline VCS::OrList::OrList()
    : m_queued(false)
{
    m_intersection.set();
}

inline VCS::OrList::OrList(bitset_t carrier)
    : CarrierList(carrier),
      m_intersection(carrier),
      m_queued(false)
{
}

inline VCS::OrList::OrList(const CarrierList& carrier_list,
                           bitset_t intersection)
    : CarrierList(carrier_list),
      m_intersection(intersection),
      m_queued(false)
{
}

inline bitset_t VCS::OrList::GetIntersection() const
{
    return m_intersection;
}

inline bool VCS::OrList::TryAdd(bitset_t carrier)
{
    if (SupersetOfAny(carrier))
        return false;
    RemoveSupersetsOfUnchecked(carrier);
    AddNew(carrier);
    m_intersection &= carrier;
    return true;
}

inline bool VCS::OrList::TryAdd(bitset_t carrier, const CarrierList& filter)
{
    if (filter.SupersetOfAny(carrier))
        return false;
    else
        return TryAdd(carrier);
}

inline bool VCS::OrList::RemoveSupersetsOf(bitset_t carrier)
{
    if (RemoveSupersetsOfCheckAnyRemoved(carrier))
    {
        CalcIntersection();
        return true;
    }
    else
        return false;
}

inline bool VCS::OrList::RemoveSupersetsOf(const CarrierList& filter)
{
    if (RemoveSupersetsOfCheckAnyRemoved(filter))
    {
        CalcIntersection();
        return true;
    }
    else
        return false;
}

inline bool VCS::OrList::TryQueue(bitset_t capturedSet)
{
    bool prev_queued = m_queued;
    m_queued = BitsetUtil::IsSubsetOf(m_intersection, capturedSet);
    return !prev_queued && m_queued;
}

void VCS::OrList::MarkAllProcessed()
{
    MarkAllOld();
    m_queued = false;
}

void VCS::OrList::MarkAllUnprocessed()
{
    MarkAllNew();
}

inline void VCS::OrList::CalcIntersection()
{
    m_intersection = GetAllIntersection();
}

void VCS::OrList::Clear()
{
    CarrierList::Clear();
    m_intersection.set();
}

//---------------------------------------------------------------------------

inline void VCS::TestQueuesEmpty()
{
    BenzeneAssert(m_fulls_and_queue.IsEmpty());
    BenzeneAssert(m_semis_or_queue.IsEmpty());
}

VCS::VCS(HexColor color)
    : m_color(color),
      m_edge1(HexPointUtil::colorEdge1(color)),
      m_edge2(HexPointUtil::colorEdge2(color))
{
    LoadCapturedSetPatterns();
}

void VCS::LoadCapturedSetPatterns()
{
    std::ifstream inFile;
    try {
        std::string file = MiscUtil::OpenFile("vc-captured-set.txt", inFile);
        LogConfig() << "VCS: reading captured set patterns from '"
        << file << "'.\n";
    }
    catch (BenzeneException& e) {
        throw BenzeneException() << "VCS: " << e.what();
    }
    std::vector<Pattern> patterns;
    Pattern::LoadPatternsFromStream(inFile, patterns);
    LogConfig() << "VCS:: parsed " << patterns.size() << " patterns.\n";
    for (std::size_t i = 0; i < patterns.size(); ++i)
    {
        m_capturedSetPatterns[WHITE].push_back(patterns[i]);
        patterns[i].FlipColors();
        m_capturedSetPatterns[BLACK].push_back(patterns[i]);
    }
    for (BWIterator c; c; ++c)
        m_hash_capturedSetPatterns[*c].Hash(m_capturedSetPatterns[*c]);
}

VCS::VCS(const VCS& other)
    : m_brd(other.m_brd),
      m_color(other.m_color)
{
    BenzeneAssert(false /* TODO implement copy constructor */);
}

/** Computes the 0-connections defined by adjacency.*/
void VCS::AddBaseVCs()
{
    HexColorSet not_other = HexColorSetUtil::ColorOrEmpty(m_color);
    for (GroupIterator x(*m_groups, not_other); x; ++x)
    {
        for (BitsetIterator y(x->Nbs() & m_brd->GetEmpty()); y; ++y)
        {
            BenzeneAssert(*y == m_groups->CaptainOf(*y));
            m_statistics.base_attempts++;
            BenzeneAssert(x->Captain() != *y);
            if (TryAddFull(x->Captain(), *y, EMPTY_BITSET))
                m_statistics.base_successes++;
        }
    }
}

/** Adds vcs obtained by pre-computed patterns. */
void VCS::AddPatternVCs()
{
    const VCPatternSet& patterns
    = VCPattern::GetPatterns(m_brd->Width(), m_brd->Height(), m_color);
    for (std::size_t i=0; i<patterns.size(); ++i)
    {
        const VCPattern& pat = patterns[i];
        if (!m_param->use_non_edge_patterns
            && !HexPointUtil::isEdge(pat.Endpoint(0))
            && !HexPointUtil::isEdge(pat.Endpoint(1)))
            continue;
        if (pat.Matches(m_color, *m_brd))
        {
            bitset_t carrier = pat.NotOpponent() - m_brd->GetColor(m_color);
            carrier.reset(pat.Endpoint(0));
            carrier.reset(pat.Endpoint(1));

            HexPoint x = m_groups->CaptainOf(pat.Endpoint(0));
            HexPoint y = m_groups->CaptainOf(pat.Endpoint(1));
            if (x == y)
                continue;

            m_statistics.pattern_attempts++;
            if (TryAddFull(x, y, carrier))
                m_statistics.pattern_successes++;
        }
    }
}

void VCS::ComputeCapturedSets(const PatternState& patterns)
{
    SG_UNUSED(patterns);
    for (BoardIterator p(m_brd->Const().EdgesAndInterior()); p; ++p)
    {
        m_capturedSet[*p] = EMPTY_BITSET;
        if (m_brd->GetColor(*p) == EMPTY)
        {
            PatternHits hits;
            patterns.MatchOnCell(m_hash_capturedSetPatterns[m_color],
                                 *p, PatternState::STOP_AT_FIRST_HIT, hits);
            for (std::size_t i = 0; i < hits.size(); ++i)
            {
                const std::vector<HexPoint>& moves = hits[0].Moves2();
                for (std::size_t j = 0; j < moves.size(); ++j)
                    m_capturedSet[*p].set(moves[j]);
            }
        }
    }
}

void VCS::Build(VCBuilderParam& param,
                const Groups& groups, const PatternState& patterns)
{
    TestQueuesEmpty();
    SgTimer timer;
    m_param = &param;
    m_groups = &groups;
    m_brd = &m_groups->Board();
    Reset();

    ComputeCapturedSets(patterns);
    AddBaseVCs();
    //disabled for rex, can add in later if confident they are valid
    if (m_param->use_patterns)
        AddPatternVCs();
    DoSearch();

    LogFine() << "  " << timer.GetTime() << "s to build vcs.\n";
}

void VCS::Build(VCBuilderParam& param,
                const Groups& oldGroups, const Groups& newGroups,
                const PatternState& patterns,
                bitset_t added[BLACK_AND_WHITE], bool use_changelog)
{
    TestQueuesEmpty();
    SgTimer timer;
    m_param = &param;
    m_groups = &newGroups;
    m_brd = &m_groups->Board();
    if (use_changelog)
    {
        backups.push_back(Backup());
        backups.back().Create(*this);
    }

    if (param.incremental_builds)
    {
        bitset_t capturedSet[BITSETSIZE];
        for (int i = 0; i < BITSETSIZE; i++)
            capturedSet[i] = m_capturedSet[i];
        m_statistics = Statistics();
        ComputeCapturedSets(patterns);
        Merge(oldGroups, capturedSet, added);
    }
    else
    {
        Reset();
        ComputeCapturedSets(patterns);
        AddBaseVCs();
    }

    if (m_param->use_patterns)
        AddPatternVCs();
    DoSearch();

    LogFine() << "  " << timer.GetTime() << "s to build vcs.\n";
}

void VCS::Revert()
{
    backups.back().Restore(*this);
    backups.pop_back();
}

void VCS::Reset()
{
    m_fulls.Reset();
    m_semis.Reset();

    m_statistics = Statistics();
}

/** @page mergeshrink Incremental Update Algorithm

    The connection set is updated to the new state of the board in a
    single pass. In this pass connections touched by opponent stones
    are destroyed, connections touched by friendly stones are resized,
    and connections in groups that are merged into larger groups are
    merged into the proper connection lists. This entire process is
    called the "merge".

    Every list needs to be checked for shrinking. This entails
    removing any cells from a connection's carrier that are now
    occupied by friendly stones. Semi-connections that have their keys
    played must be upgraded to full connections. */
void VCS::Merge(const Groups& oldGroups, bitset_t* oldCapturedSet,
                bitset_t added[BLACK_AND_WHITE])
{
    // Kill connections containing stones the opponent just played.
    // NOTE: This *must* be done in the original state, not in the
    // state with the newly added stones. If we are adding stones of
    // both colors there could be two groups of our stones that are
    // going to be merged, but we need to kill connections touching
    // the opponent stones before we do so.
    RemoveAllContaining(oldGroups, added[!m_color]);

    bitset_t merged[BITSETSIZE];
    HexColorSet not_other = HexColorSetUtil::NotColor(!m_color);
    for (GroupIterator x(oldGroups, not_other); x; ++x)
    {
        HexPoint xc = x->Captain();
        merged[m_groups->CaptainOf(xc)].set(xc);
    }

    for (GroupIterator x(*m_groups, not_other); x; ++x)
    {
        HexPoint xc = x->Captain();
        MergeRemoveSelfEnds(merged[xc]);
        for (GroupIterator y(*m_groups, not_other); &*y != &*x; ++y)
        {
            HexPoint yc = y->Captain();
            MergeAndShrink(oldCapturedSet, added[m_color],
                           merged[xc], merged[yc], xc, yc);
        }
    }
}

/** Removes all connections whose intersection with given set is
    non-empty. */
inline void VCS::RemoveAllContaining(const Groups& oldGroups,
                                     bitset_t removed)
{
    if (removed.none())
        return;
    // Use old groupset and delete old groups that are
    // now the opponent's color.
    HexColorSet not_other = HexColorSetUtil::NotColor(!m_color);
    for (GroupIterator x(oldGroups, not_other); x; ++x)
    {
        HexPoint xc = x->Captain();
        bool killed = m_groups->GetGroup(xc).Color() == !m_color;
        for (GroupIterator y(oldGroups, not_other); &*y != &*x; ++y)
        {
            HexPoint yc = y->Captain();
            if (killed || m_groups->GetGroup(yc).Color() == !m_color)
            {
                m_fulls.Delete(xc, yc);
                m_semis.Delete(xc, yc);
                continue;
            }
            AndList* fulls = m_fulls[xc][yc];
            if (fulls)
            {
                size_t count = fulls->RemoveAllContaining(removed);
                m_statistics.killed0 += count;
                if (fulls->IsEmpty())
                    m_fulls.Delete(xc, yc, fulls);
                else if (count > 0)
                    fulls->CalcIntersection();
            }
            OrList* semis = m_semis[xc][yc];
            if (semis)
            {
                size_t count = semis->RemoveAllContaining(removed);
                m_statistics.killed1 += count;
                if (semis->IsEmpty())
                    m_semis.Delete(xc, yc, semis);
                else if (count > 0)
                    semis->CalcIntersection();
            }
        }
    }
}

inline void VCS::MergeRemoveSelfEnds(bitset_t x_merged)
{
    for (BitsetIterator x(x_merged); x; ++x)
        for (BitsetIterator y(x_merged); *y <= *x; ++y)
        {
            m_fulls.Delete(*x, *y);
            m_semis.Delete(*x, *y);
        }
}

inline bool VCS::Shrink(bitset_t added, HexPoint x, HexPoint y,
                        AndList* fulls)
{
    bool new_fulls = false;
    std::vector<bitset_t> to_shrink;
    fulls->RemoveAllContaining(added, to_shrink);
    if (to_shrink.empty())
        return false;
    fulls->CalcIntersection();
    for (std::vector<bitset_t>::iterator it = to_shrink.begin();
         it != to_shrink.end(); ++it)
    {
        bitset_t carrier = *it - added;
        if (fulls->TryAdd(carrier, m_param->limit_fulls))
        {
            m_statistics.shrunk0++;
            m_fulls_and_queue.Push(Full(x, y, carrier));
            new_fulls = true;
        }
    }
    return new_fulls;
}

inline bool VCS::Shrink(bitset_t added, OrList* semis, const AndList* filter)
{
    bool new_semis = false;
    std::vector<bitset_t> to_shrink;
    semis->RemoveAllContaining(added, to_shrink);
    if (to_shrink.empty())
        return false;
    semis->CalcIntersection();
    for (std::vector<bitset_t>::iterator it = to_shrink.begin();
        it != to_shrink.end(); ++it)
    {
        bitset_t carrier = *it - added;
        bool success;
        if (filter)
            success = semis->TryAdd(carrier, *filter);
        else
            success = semis->TryAdd(carrier);
        if (success)
        {
            m_statistics.shrunk1++;
            new_semis = true;
        }
    }
    return new_semis;
}

inline bool VCS::Shrink(bitset_t added, HexPoint x, HexPoint y,
                        AndList* fulls, const CarrierList& list)
{
    bool new_fulls = false;
    for (CarrierList::Iterator i(list); i; ++i)
    {
        bitset_t carrier = i.Carrier() - added;
        if (fulls->TryAdd(carrier, m_param->limit_fulls))
        {
            m_statistics.shrunk0++;
            m_fulls_and_queue.Push(Full(x, y, carrier));
            new_fulls = true;
        }
    }
    return new_fulls;
}

inline bool VCS::Shrink(bitset_t added, OrList* semis, const CarrierList& list,
                        const AndList* filter)
{
    bool new_semis = false;
    for (CarrierList::Iterator i(list); i; ++i)
    {
        bitset_t carrier = i.Carrier() - added;
        bool success;
        if (filter)
            success = semis->TryAdd(carrier, *filter);
        else
            success = semis->TryAdd(carrier);
        if (success)
        {
            m_statistics.shrunk1++;
            new_semis = true;
        }
    }
    return new_semis;
}

/** Merges and shrinks connections between the given endpoints. */
void VCS::MergeAndShrink(bitset_t* oldCapturedSet, bitset_t added,
                         bitset_t x_merged, bitset_t y_merged,
                         HexPoint x, HexPoint y)
{
    BenzeneAssert(x != y);
    BenzeneAssert((x_merged & y_merged).none());

    if (added.none())
        return;

    AndList* fulls = m_fulls[x][y];
    OrList* semis = m_semis[x][y];

    bool new_fulls = false;
    bool newCaptureSet =
        (oldCapturedSet[x] | oldCapturedSet[y]) !=
        (m_capturedSet[x] | m_capturedSet[y]);

    if (fulls)
        // First shrink fulls wihtout merge
        new_fulls |= Shrink(added, x, y, fulls);

    // Collect and shrink all fulls
    for (BitsetIterator xi(x_merged); xi; ++xi)
        for (BitsetIterator yi(y_merged); yi; ++yi)
        {
            // Merge and shrink fulls with merged ends
            if (*xi != x || *yi != y)
            {
                AndList* merged_fulls = m_fulls[*xi][*yi];
                if (!merged_fulls)
                    continue;
                if (!fulls)
                    fulls = m_fulls.Put(x, y);
                new_fulls |= Shrink(added, x, y, fulls, *merged_fulls);
                m_fulls.Delete(*xi, *yi, merged_fulls);
            }
        }

    if (fulls)
    {
        if (added.test(x) || added.test(y) || newCaptureSet)
        {
            for (CarrierList::Iterator i(*fulls); i; ++i)
                if (i.Old())
                    m_fulls_and_queue.Push(Full(x, y, i.Carrier()));
            fulls->MarkAllUnprocessed();
        }
    }

    // Shrink semis
    bool new_semis = false;
    if (semis)
    {
        if (new_fulls)
            // Filter out semis which are supersets of new fulls
            semis->RemoveSupersetsOf(*fulls);
        // Shrink semis without merge
        new_semis |= Shrink(added, semis, fulls);
    }

    // Collect and shrink semis
    for (BitsetIterator xi(x_merged); xi; ++xi)
        for (BitsetIterator yi(y_merged); yi; ++yi)
        {
            if (*xi == x && *yi == y)
                continue;
            OrList* merged_semis = m_semis[*xi][*yi];
            if (!merged_semis)
                continue;
            if (!semis)
                semis = m_semis.Put(x, y);
            new_semis |= Shrink(added, semis, *merged_semis, fulls);
            m_semis.Delete(*xi, *yi, merged_semis);
        }

    if (!semis)
        return;

    bool anyCaptureSetChanged =
        (oldCapturedSet[x] != m_capturedSet[x]) ||
        (oldCapturedSet[y] != m_capturedSet[y]);
    if (new_semis || anyCaptureSetChanged)
    {
        if (anyCaptureSetChanged)
            semis->MarkAllUnprocessed();
        if (semis->TryQueue(m_capturedSet[x] | m_capturedSet[y]))
            m_semis_or_queue.Push(Ends(x, y));
    }
}

void VCS::DoSearch()
{
    while (true)
    {
        if (!m_fulls_and_queue.IsEmpty())
        {
            Full vc = m_fulls_and_queue.Pop();
            AndFull(vc.x, vc.y, vc.carrier);
        }
        else if (!m_semis_or_queue.IsEmpty())
        {
            Ends p = m_semis_or_queue.Pop();
            OrSemis(p.x, p.y);
        }
        else
            break;
    }
    TestQueuesEmpty();
    m_fulls_and_queue.Clear();
    m_semis_or_queue.Clear();;
    TestQueuesEmpty();
}

// And rule stuff

#define FUNC(__name) VCS::VCAnd::Functor##__name()
#define SWITCHTO(__S) return func.template Apply<__S>(*this)

inline VCS::VCAnd::VCAnd(VCS& vcs, HexPoint x, HexPoint y, bitset_t capturedSet,
                         bitset_t xz_carrier, CarrierList *zy_list)
    : vcs(vcs), x(x), y(y), capturedSet(capturedSet),
      xz_carrier(xz_carrier), zy_iter(*zy_list)
{
    BenzeneAssert(x != y);
}

template <class Func>
inline void VCS::VCAnd::Run(Func func)
{
    SWITCHTO(InitialState);
}

template <class S, class Func>
inline void VCS::VCAnd::SemiRemoveSupersetsOf(bitset_t carrier, Func func)
{
    if (!S::semis_initialized)
    {
        semis = vcs.m_semis[x][y];
        if (semis)
            return SemiRemoveSupersetsOf<typename S::SemisSet>(carrier, func);
        else
            return SemiRemoveSupersetsOf<typename S::SemisNull>(carrier, func);
    }
    if (!S::semis_null)
        semis->RemoveSupersetsOf(carrier);
    SWITCHTO(S);
}

template <class S, class Func>
inline void VCS::VCAnd::TryAddFull(bitset_t carrier, Func func)
{
    if (!S::fulls_initialized)
    {
        fulls = vcs.m_fulls[x][y];
        if (fulls)
            return TryAddFull<typename S::FullsSet>(carrier, func);
        else
            return TryAddFull<typename S::FullsNull>(carrier, func);
    }
    if (S::fulls_null)
        fulls = vcs.m_fulls.Put(x, y, new AndList(carrier));
    else if (!fulls->TryAdd(carrier, vcs.m_param->limit_fulls))
        SWITCHTO(S);
    vcs.m_fulls_and_queue.Push(Full(x, y, carrier));
    SemiRemoveSupersetsOf<typename S::FullsSet>(carrier, func);
}

template <class S, class Func>
inline void VCS::VCAnd::TryAddSemi(bitset_t carrier, Func func)
{
    if (!S::semis_initialized)
    {
        semis = vcs.m_semis[x][y];
        if (semis)
            return TryAddSemi<typename S::SemisSet>(carrier, func);
        else
            return TryAddSemi<typename S::SemisNull>(carrier, func);
    }
    if (!S::fulls_initialized)
    {
        fulls = vcs.m_fulls[x][y];
        if (fulls)
            return TryAddSemi<typename S::FullsSet>(carrier, func);
        else
            return TryAddSemi<typename S::FullsNull>(carrier, func);
    }
    if (S::semis_null)
    {
        vcs.m_statistics.and_semi_attempts++;
        if (!S::fulls_null)
        {
            if (fulls->SupersetOfAny(carrier))
                SWITCHTO(S);
        }
        semis = vcs.m_semis.Put(x, y, new OrList(carrier));
    }
    else
    {
        vcs.m_statistics.and_semi_attempts++;
        if (!S::fulls_null)
        {
            if (fulls->SupersetOfAny(carrier))
                SWITCHTO(S);
        }
        if (!semis->TryAdd(carrier))
            SWITCHTO(S);
    }

    if (semis->TryQueue(capturedSet))
        vcs.m_semis_or_queue.Push(Ends(x, y));

    vcs.m_statistics.and_semi_successes++;
    SWITCHTO(typename S::SemisSet);
}

void VCS::AndFull(HexPoint x, HexPoint y, bitset_t carrier)
{
    BenzeneAssert(x == m_groups->CaptainOf(x));
    BenzeneAssert(y == m_groups->CaptainOf(y));
    BenzeneAssert(x != y);
    BenzeneAssert(m_brd->GetColor(x) != !m_color);
    BenzeneAssert(m_brd->GetColor(y) != !m_color);

    if (!m_fulls[x][y]->TrySetProcessed(carrier))
        return;

    bitset_t xyCapturedSet = m_capturedSet[x] | m_capturedSet[y];
    AndFull(x, y, carrier, m_brd->GetColor(y), xyCapturedSet);
    AndFull(y, x, carrier, m_brd->GetColor(x), xyCapturedSet);
}

inline void VCS::AndFull(HexPoint x, HexPoint z, bitset_t carrier,
                         HexColor zcolor, bitset_t xzCapturedSet)
{
    if (!m_param->and_over_edge && HexPointUtil::isEdge(z))
        return;
    if (zcolor == EMPTY)
        AndFullEmptyFull(x, z, carrier.set(z), xzCapturedSet);
    else
        AndFullStoneFull(x, z, carrier, xzCapturedSet);
}

inline void VCS::AndFullEmptyFull(HexPoint x, HexPoint z, bitset_t carrier,
                                  bitset_t xzCapturedSet)
{
    for (BitsetIterator it(m_fulls[z].Entries().reset(x) - carrier); it; ++it)
    {
        HexPoint y = *it;
        BenzeneAssert(y == m_groups->CaptainOf(y));
        BenzeneAssert(x != y && z != y);
        BenzeneAssert(!carrier.test(y));
        AndFullEmptyFull(x, z, y, carrier, xzCapturedSet | m_capturedSet[y]);
    }
}

inline void VCS::AndFullEmptyFull(HexPoint x, HexPoint z, HexPoint y,
                                  bitset_t carrier, bitset_t xyCapturedSet)
{
    AndList* zy_fulls = m_fulls[z][y];
    BenzeneAssert(zy_fulls);
    if (!BitsetUtil::IsSubsetOf(zy_fulls->GetIntersection() & carrier,
        xyCapturedSet))
        return;

    VCAnd vcAnd(*this, x, y, xyCapturedSet, carrier, zy_fulls);
    vcAnd.Run(FUNC(FEF));
}

template <class S>
inline void VCS::VCAnd::FEF()
{
    if (!zy_iter)
        return;
    if (!zy_iter.Old())
        return FEFNext<S>();
    if (zy_iter.Carrier().test(x))
        return FEFNext<S>();
    intersection = xz_carrier & zy_iter.Carrier();
    if (intersection.none())
        return TryAddSemi<S>(xz_carrier | zy_iter.Carrier(), FUNC(FEFNext));
    else if (BitsetUtil::IsSubsetOf(intersection, capturedSet))
        return TryAddSemi<S>(xz_carrier | zy_iter.Carrier() | capturedSet,
                             FUNC(FEFNext));
    FEFNext<S>();
}

template <class S>
inline void VCS::VCAnd::FEFNext()
{
    ++zy_iter;
    FEF<S>();
}

inline void VCS::AndFullStoneFull(HexPoint x, HexPoint z, bitset_t carrier,
                                  bitset_t xzCapturedSet)
{
    for (BitsetIterator it(m_fulls[z].Entries().reset(x) - carrier); it; ++it)
    {
        HexPoint y = *it;
        BenzeneAssert(y == m_groups->CaptainOf(y));
        BenzeneAssert(x != y && z != y);
        BenzeneAssert(!carrier.test(y));
        AndFullStoneFull(x, z, y, carrier, xzCapturedSet | m_capturedSet[y]);
    }
}

inline void VCS::AndFullStoneFull(HexPoint x, HexPoint z, HexPoint y,
                                  bitset_t carrier, bitset_t xyCapturedSet)
{
    AndList* zy_fulls = m_fulls[z][y];
    BenzeneAssert(zy_fulls);
    if (!BitsetUtil::IsSubsetOf(zy_fulls->GetIntersection() & carrier,
        xyCapturedSet))
        return;

    VCAnd vcAnd(*this, x, y, xyCapturedSet, carrier, zy_fulls);
    vcAnd.Run(FUNC(FSF));
}

template <class S>
inline void VCS::VCAnd::FSF()
{
    if (!zy_iter)
        return;
    if (!zy_iter.Old())
        return FSFNext<S>();
    if (zy_iter.Carrier().test(x))
        return FSFNext<S>();
    intersection = xz_carrier & zy_iter.Carrier();
    if (intersection.none())
        return TryAddFull<S>(xz_carrier | zy_iter.Carrier(), FUNC(FSFNext));
    else if (BitsetUtil::IsSubsetOf(intersection, capturedSet))
        return TryAddFull<S>(xz_carrier | zy_iter.Carrier() | capturedSet,
                             FUNC(FSFNext));
    FSFNext<S>();
}

template <class S>
inline void VCS::VCAnd::FSFNext()
{
    ++zy_iter;
    FSF<S>();
}

void VCS::OrSemis(HexPoint x, HexPoint y)
{
    BenzeneAssert(x != y);
    OrList* xy_semis = m_semis[x][y];
    BenzeneAssert(xy_semis);
    AndList *xy_fulls = m_fulls[x][y];
    m_statistics.doOrs++;
    //modified for rex
    std::vector<bitset_t> new_fulls = VC2Or(*xy_semis, m_capturedSet[x]|m_capturedSet[y]);/*m_param->limit_or ?
        VCOr(*xy_semis, xy_fulls ? xy_fulls->GetAllIntersection() : bitset_t().set(),
             m_capturedSet[x], m_capturedSet[y]) :
        VCOr(*xy_semis, xy_fulls ? *xy_fulls : CarrierList(),
             m_capturedSet[x], m_capturedSet[y]);*/
    xy_semis->MarkAllProcessed();
    if (new_fulls.empty())
        return;
    m_statistics.goodOrs++;
    m_statistics.or_attempts += new_fulls.size();
    m_statistics.or_successes += new_fulls.size();
    if (!xy_fulls)
        xy_fulls = m_fulls.Put(x, y, new AndList());
    for (std::vector<bitset_t>::iterator it = new_fulls.begin();
            it != new_fulls.end(); ++it)
        if (xy_fulls->TryAdd(*it, m_param->limit_fulls))
            m_fulls_and_queue.Push(Full(x, y, *it));
}

//compute only 2-or connections (pairing strategies for rex)
vector<bitset_t> benzene::VC2Or(CarrierList semis, bitset_t capturedSet)
{
	vector<bitset_t> res;
	for (CarrierList::Iterator i(semis); i; ++i){
		CarrierList::Iterator j(i);
		for (++j; j; ++j){
			if(!j.Old()){
				if(!(i.Carrier()&j.Carrier()).any()){
					bitset_t new_con = i.Carrier()|j.Carrier();
					res.push_back(new_con);
				}
				else if(BitsetUtil::IsSubsetOf(i.Carrier()&j.Carrier(), capturedSet)){
					bitset_t new_con = i.Carrier()|j.Carrier()|capturedSet;
					res.push_back(new_con);
				}
			}
		}
	}
	return res;
}

vector<bitset_t> benzene::VCOr(CarrierList semis, bitset_t cands,
                               bitset_t xCapturedSet, bitset_t yCapturedSet)
{
    vector<bitset_t> res;
    bitset_t capturedSet;
    while (cands.any())
    {
        semis.RemoveSupersetsOfUnchecked(cands);
        bitset_t I_new = semis.GetNewIntersection();
        cands -= I_new;
        if (cands.none())
            break;

        bitset_t I_old = semis.GetOldIntersection();
        bitset_t I = I_new & I_old;

        if (!BitsetUtil::IsSubsetOf(I, capturedSet))
            capturedSet |= xCapturedSet;
        if (!BitsetUtil::IsSubsetOf(I, capturedSet))
            capturedSet |= yCapturedSet;
        if (!BitsetUtil::IsSubsetOf(I, capturedSet))
            break;

        cands -= capturedSet;
        if (cands.none())
            break;

        size_t a = cands._Find_first();

        bitset_t I2;
        I2.set();

        for (CarrierList::Iterator i(semis); i; ++i)
            if (!i.Carrier()[a])
                I2 &= i.Carrier();

        if (BitsetUtil::IsSubsetOf(I2, capturedSet))
        {
            bitset_t U = capturedSet;
            I2.set();
            for (CarrierList::Iterator i(semis); i; ++i)
            {
                if (i.Carrier()[a] || BitsetUtil::IsSubsetOf(I2, i.Carrier()))
                    continue;
                I2 &= i.Carrier();
                U |= i.Carrier();
                if (BitsetUtil::IsSubsetOf(I2, capturedSet))
                    break;
            }
            res.push_back(U);
            cands &= U;
        }
        else
            cands.reset(a);
    }
    return res;
}

inline bool VCS::TryAddFull(HexPoint x, HexPoint y, bitset_t carrier)
{
    BenzeneAssert(x != y);
    AndList* fulls = m_fulls[x][y];
    if (!fulls)
        fulls = m_fulls.Put(x, y, new AndList(carrier));
    else if (!fulls->TryAdd(carrier, m_param->limit_fulls))
        return false;
    m_fulls_and_queue.Push(Full(x, y, carrier));
    OrList* semis = m_semis[x][y];
    if (semis)
        semis->RemoveSupersetsOf(carrier);
    return true;
}

inline void VCS::Backup::Create(VCS& vcs)
{
    vcs.TestQueuesEmpty();
    fulls.Create(vcs.m_fulls);
    semis.Create(vcs.m_semis);
}

inline void VCS::Backup::Restore(VCS& vcs)
{
    vcs.TestQueuesEmpty();
    fulls.Restore(vcs.m_fulls);
    semis.Restore(vcs.m_semis);
}

bitset_t VCS::GetSmallestSemisUnion() const
{
    return SemiGreedyUnion(m_edge1, m_edge2);
}

bool VCS::SmallestFullCarrier(bitset_t& carrier) const
{
    AndList* fulls = m_fulls[m_edge1][m_edge2];
    if (!fulls)
        return false;
    if (fulls->IsEmpty())
        return false;
    size_t best = std::numeric_limits<size_t>::max();
    for (CarrierList::Iterator i(*fulls); i; ++i)
    {
        size_t count = i.Carrier().count();
        if (count < best)
        {
            best = count;
            carrier = i.Carrier();
        }
    }
    return true;
}

int VCS::FullAdjacent(HexPoint x, HexPoint y) const
{
    AndList* fulls = m_fulls[x][y];
    if (!fulls)
        return -1;
    if (fulls->IsEmpty())
        return -1;
    CarrierList::Iterator i(*fulls);
    return i.Carrier().any() ? 1 : 0;
}

bool VCS::SmallestSemiCarrier(bitset_t& carrier) const
{
    OrList* semis = m_semis[m_edge1][m_edge2];
    if (!semis)
        return false;
    if (semis->IsEmpty())
        return false;
    size_t best = std::numeric_limits<size_t>::max();
    for (CarrierList::Iterator i(*semis); i; ++i)
    {
        size_t count = i.Carrier().count();
        if (count < best)
        {
            best = count;
            carrier = i.Carrier();
        }
    }
    return true;
}

HexPoint VCS::SemiKey(bitset_t carrier, HexPoint x, HexPoint y) const
{
    bitset_t pairCapturedSet = m_capturedSet[x] | m_capturedSet[y];
    for (BitsetIterator k(carrier); k; ++k)
    {
        bitset_t capturedSet = pairCapturedSet | m_capturedSet[*k];
        if (!BitsetUtil::IsSubsetOf(capturedSet, carrier))
            capturedSet.reset();
        AndList* fulls1 = m_fulls[x][*k];
        if (!fulls1)
            continue;
        AndList* fulls2 = m_fulls[y][*k];
        if (!fulls2)
            continue;
        for (CarrierList::Iterator i(*fulls1); i; ++i)
        {
            if (!BitsetUtil::IsSubsetOf(i.Carrier(), carrier))
                continue;
            for (CarrierList::Iterator j(*fulls2); j; ++j)
            {
                if (!BitsetUtil::IsSubsetOf(j.Carrier(), carrier))
                    continue;
                if (BitsetUtil::IsSubsetOf(i.Carrier() & j.Carrier(),
                                           capturedSet))
                    return *k;
            }
        }
    }
    BenzeneAssert(false /* always should find a key */);
    return INVALID_POINT;
}

HexPoint VCS::SmallestSemiKey() const
{
    bitset_t carrier;
    if (!SmallestSemiCarrier(carrier))
        return INVALID_POINT;
    bitset_t edgesCapturedSet = m_capturedSet[m_edge1] | m_capturedSet[m_edge2];
    for (BitsetIterator k(carrier); k; ++k)
    {
        bitset_t capturedSet = edgesCapturedSet | m_capturedSet[*k];
        if (!BitsetUtil::IsSubsetOf(capturedSet, carrier))
            capturedSet.reset();
        AndList* fulls1 = m_fulls[m_edge1][*k];
        if (!fulls1)
            continue;
        AndList* fulls2 = m_fulls[m_edge2][*k];
        if (!fulls2)
            continue;
        for (CarrierList::Iterator i(*fulls1); i; ++i)
        {
            if (!BitsetUtil::IsSubsetOf(i.Carrier(), carrier))
                continue;
            for (CarrierList::Iterator j(*fulls2); j; ++j)
            {
                if (!BitsetUtil::IsSubsetOf(j.Carrier(), carrier))
                    continue;
                if (BitsetUtil::IsSubsetOf(i.Carrier() & j.Carrier(),
                                           capturedSet))
                    return *k;
            }
        }
    }
    BenzeneAssert(false /* always should find a key */);
    return INVALID_POINT;
}

bool VCS::FullExists(HexPoint x, HexPoint y) const
{
    AndList* fulls = m_fulls[x][y];
    if (!fulls)
        return false;
    return !fulls->IsEmpty();
}

bool VCS::FullExists() const
{
    return FullExists(m_edge1, m_edge2);
}

bool VCS::SemiExists() const
{
    OrList* semis = m_semis[m_edge1][m_edge2];
    if (!semis)
        return false;
    return !semis->IsEmpty();
}

bool VCS::SemiExists(HexPoint x, HexPoint y) const
{
    OrList* semis = m_semis[x][y];
    if (!semis)
        return false;
    return !semis->IsEmpty();
}

static CarrierList empty_carrier_list;

const CarrierList& VCS::GetFullCarriers(HexPoint x, HexPoint y) const
{
    AndList* fulls = m_fulls[x][y];
    if (!fulls)
        return empty_carrier_list;
    return *fulls;
}

const CarrierList& VCS::GetSemiCarriers(HexPoint x, HexPoint y) const
{
    OrList* semis = m_semis[x][y];
    if (!semis)
        return empty_carrier_list;
    return *semis;
}

const CarrierList& VCS::GetFullCarriers() const
{
    return GetFullCarriers(m_edge1, m_edge2);
}

const CarrierList& VCS::GetSemiCarriers() const
{
    OrList* semis = m_semis[m_edge1][m_edge2];
    if (!semis)
        return empty_carrier_list;
    return *semis;
}

bitset_t VCS::GetFullNbs(HexPoint x) const
{
    return m_fulls[x].Entries();
}

bitset_t VCS::GetSemiNbs(HexPoint x) const
{
    return m_semis[x].Entries();
}

bitset_t VCS::FullIntersection(HexPoint x, HexPoint y) const
{
    AndList* fulls = m_fulls[x][y];
    return fulls ? fulls->GetAllIntersection() : bitset_t().set();
}

bitset_t VCS::FullGreedyUnion(HexPoint x, HexPoint y) const
{
    AndList* fulls = m_fulls[x][y];
    return fulls ? fulls->GetGreedyUnion() : EMPTY_BITSET;
}

bitset_t VCS::SemiIntersection(HexPoint x, HexPoint y) const
{
    OrList* semis = m_semis[x][y];
    return semis ? semis->GetIntersection() : bitset_t().set();
}

bitset_t VCS::SemiIntersection() const
{
    return SemiIntersection(m_edge1, m_edge2);
}

bitset_t VCS::SemiGreedyUnion(HexPoint x, HexPoint y) const
{
    OrList* semis = m_semis[x][y];
    return semis ? semis->GetGreedyUnion() : EMPTY_BITSET;
}
