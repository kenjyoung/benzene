//----------------------------------------------------------------------------
/** @file EndgameUtil.cpp */
//----------------------------------------------------------------------------

#include "EndgameUtil.hpp"
#include "BitsetIterator.hpp"
#include "BoardUtil.hpp"
#include "VCS.hpp"
#include "VCUtil.hpp"

using namespace benzene;

//----------------------------------------------------------------------------

/** @page playingdeterminedstates Playing in Determined States
    
    A determined state is defined as a state were one player has
    a winning semi/full connection.

    In a winning state, returns key of smallest semi connection,
    if one exists.  If no semi connection, plays move that overlaps
    the maximum number of full connections.

    In a losing state, returns move overlapping the most SCs (instead
    of VCs) since any winning SC still remaining on our opponent's
    next turn will allow them to win. Thus, we want to eliminate those
    winning SCs that are shortest/easiest to find. It is also possible
    that our opponent has winning VCs and yet no winning SCs. In this
    case, we just perform the overlap with the VCs.

    @bug It is possible our opponent has winning VCs that are not
    derived from the winning SCs in our list. Thus, we may want to
    consider overlapping the winning VCs as well.
*/

/** @page computingmovestoconsider Computing the set of moves to consider

    The set of moves to consider is defined as the mustplay minus the
    inferior cells minus cells that create states that are mirrors of
    themselves (these are losing via the strategy stealing argument)
    minus any cells that are rotations of other cells (if the state is
    a rotation of itself). This set can never be empty, because
    IsLostGame() detects such states and reports them as losing (these
    states will be handled by PlayDeterminedState()).
*/

//----------------------------------------------------------------------------

/** Local functions. */
namespace {
bool CheckColorSymmetry(const StoneBoard& brd){
	bitset_t black = brd.GetColor(BLACK);
	bitset_t white_mirror1 = BoardUtil::Mirror(brd.Const(),brd.GetPlayed(WHITE));
	bitset_t white_mirror2 = BoardUtil::Mirror(brd.Const(),BoardUtil::Rotate(brd.Const(),
                                                  brd.GetPlayed(WHITE)));
	if(white_mirror1 == black || white_mirror2 == black)
		return true;
	return false;
}

bitset_t ComputeLossesViaStrategyStealingArgument(const StoneBoard& brd,
                                                  HexColor color)
{
    bitset_t ret;
    if ((brd.Width() == brd.Height())
        && (brd.GetPlayed(!color).count() - brd.GetPlayed(color).count() == 1))
    {
        bitset_t mirror1 
            = BoardUtil::Mirror(brd.Const(), brd.GetPlayed(!color))
            - brd.GetPlayed(color);
        if (mirror1.count() == 1)
            ret |= mirror1;
        bitset_t mirror2 =
            BoardUtil::Mirror(brd.Const(), BoardUtil::Rotate(brd.Const(), 
                                                  brd.GetPlayed(!color)))
            - brd.GetPlayed(color);
        if (mirror2.count() == 1)
            ret |= mirror2;
        ret &= brd.GetEmpty();
    }
    return ret;
}

bitset_t ComputeWinsViaStrategyStealingArgument(const StoneBoard& brd,
        HexColor color)
{
	bitset_t ret;
    if ((brd.Width() == brd.Height())
        && (brd.GetPlayed(!color).count() - brd.GetPlayed(color).count() == 1))
    {
        bitset_t mirror1
            = BoardUtil::Mirror(brd.Const(), brd.GetPlayed(!color))
            - brd.GetPlayed(color);
        if (mirror1.count() == 1)
            ret |= mirror1;
        bitset_t mirror2 =
            BoardUtil::Mirror(brd.Const(), BoardUtil::Rotate(brd.Const(),
                                                  brd.GetPlayed(!color)))
            - brd.GetPlayed(color);
        if (mirror2.count() == 1)
            ret |= mirror2;
        ret &= brd.GetEmpty();
    }
    return ret;
}

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

bitset_t GetSemiKeys(const HexBoard& brd, HexColor color){
	CarrierList semis = brd.Cons(color).GetSemiCarriers();
	bitset_t semikeys;
	for(CarrierList::Iterator it(semis); it; ++it){
		semikeys|=brd.Cons(color).SemiKey(it.Carrier(), HexPointUtil::colorEdge1(color), HexPointUtil::colorEdge2(color));
	}
	return semikeys;
}

bitset_t ComputeConsiderSet(const HexBoard& brd, HexColor color)
{
	bitset_t consider;
	//modified for rex solver
    /*bitset_t consider = VCUtil::GetMustplay(brd, color);
    consider = consider - inf.Vulnerable()
                        - inf.Reversible()
                        - inf.Dominated()
          - ComputeLossesViaStrategyStealingArgument(brd.GetPosition(), color);*/
	bitset_t dead = brd.GetDead() & brd.GetPosition().GetEmpty();
	//if there are any dead cells they are provably equivalent and superior moves so just return one to play
	if(dead.any()){
		consider = bitset_t();
		consider.set(dead._Find_first());
		return consider;
	}
	//in rex symmetric boards are second player wins for odd numbers of empty cells
	if(brd.GetPosition().GetEmpty().count()%2 == 0){
		bitset_t symmetry_wins = ComputeWinsViaStrategyStealingArgument(brd.GetPosition(), color);
		if(symmetry_wins.any()){
			consider = bitset_t();
			consider.set(symmetry_wins._Find_first());
			return consider;
		}
	}

	const InferiorCells& inf = brd.GetInferiorCells();
	consider = brd.GetPosition().GetEmpty();
	consider-= inf.Dominated();
	consider-= GetSemiKeys(brd, color);

	//in rex symmetric boards are first player wins for even numbers of empty cells
	//else they are first player losses
	if(brd.GetPosition().GetEmpty().count()%2 == 1)
		consider-=ComputeLossesViaStrategyStealingArgument(brd.GetPosition(), color);
	if (brd.GetPosition().IsSelfRotation())
	        consider = RemoveRotations(brd.GetPosition(), consider);
    return consider;
}

bitset_t StonesInProof(const HexBoard& brd, HexColor color)
{
    return brd.GetPosition().GetPlayed(color) 
        | brd.GetInferiorCells().DeductionSet(color);
}

/** Priority is given to eliminating the most easily-answered
    moves first (i.e. dead cells require no answer, answering
    vulnerable plays only requires knowledge of local adjacencies,
    etc.) */
void TightenMoveBitset(bitset_t& moveBitset, const InferiorCells& inf)
{
    BitsetUtil::SubtractIfLeavesAny(moveBitset, inf.Dead());
    BitsetUtil::SubtractIfLeavesAny(moveBitset, inf.Vulnerable());
    BitsetUtil::SubtractIfLeavesAny(moveBitset, inf.Captured(BLACK));
    BitsetUtil::SubtractIfLeavesAny(moveBitset, inf.Captured(WHITE));
    BitsetUtil::SubtractIfLeavesAny(moveBitset, inf.Reversible());
    BitsetUtil::SubtractIfLeavesAny(moveBitset, inf.Dominated());
    BenzeneAssert(moveBitset.any());
}
    
/** Intersects as many of the smallest connections as possible. Then,
    subject to that restriction, tries to be a non-inferior move (using
    the member variables), and then to overlap as many other connections
    as possible. */
HexPoint MostOverlappingMove(const CarrierList& carriers,
                             const InferiorCells& inf)
{
    bitset_t intersectSmallest;
    intersectSmallest.flip();
    
    // compute intersection of smallest until next one makes it empty
    for (CarrierList::Iterator it(carriers); it; ++it)
    {
        if ((it.Carrier() & intersectSmallest).none())
	    break;
	intersectSmallest &= it.Carrier();
    }
    LogFine() << "Intersection of smallest set is:\n"
	      << HexPointUtil::ToString(intersectSmallest) << '\n';
    
    // remove as many inferior moves as possible from this intersection
    TightenMoveBitset(intersectSmallest, inf);
    
    LogFine() << "After elimination of inferior cells:\n"
	      << HexPointUtil::ToString(intersectSmallest) << '\n';
    
    // determine which of the remaining cells performs best with
    // regards to other connections
    int numHits[BITSETSIZE];
    memset(numHits, 0, sizeof(numHits));
    for (CarrierList::Iterator it(carriers); it; ++it)
    {
	for (int i = 0; i < BITSETSIZE; i++) 
	    if (intersectSmallest.test(i) && it.Carrier().test(i))
		numHits[i]++;
    }
    
    int curBestMove = -1;
    int curMostHits = 0;
    for (int i = 0; i < BITSETSIZE; i++) 
    {
	if (numHits[i] > curMostHits) 
        {
	    BenzeneAssert(intersectSmallest.test(i));
	    curMostHits = numHits[i];
	    curBestMove = i;
	}
    }
    
    if (curMostHits == 0)
        LogWarning() << "curMostHits == 0!!\n";
    BenzeneAssert(curMostHits > 0);
    return (HexPoint)curBestMove;
}
    
/** Returns best winning move. */
HexPoint PlayWonGame(const HexBoard& brd, HexColor color)
{
    BenzeneAssert(EndgameUtil::IsWonGame(brd, color));

    // If we have a winning SC, then play in the key of the smallest one
    HexPoint semi_key = brd.Cons(color).SmallestSemiKey();
    if (semi_key != INVALID_POINT)
    {
        LogInfo() << "Winning SC.\n";
        return semi_key;
    }
    
    // If instead we have a winning VC, then play best move in its carrier set
    if (brd.Cons(color).FullExists())
    {
        LogFine() << "Winning VC.\n";
        return MostOverlappingMove(brd.Cons(color).GetFullCarriers(),
                                   brd.GetInferiorCells());
    }
    // Should never get here!
    BenzeneAssert(false);
    return INVALID_POINT;
}

/** Returns most blocking (ie, the "best") losing move. */
HexPoint PlayLostGame(const HexBoard& brd, HexColor color)
{
    BenzeneAssert(EndgameUtil::IsLostGame(brd, color));

    // Determine if color's opponent has guaranteed win
    HexColor other = !color;
    
    LogInfo() << "Opponent has won; playing most blocking move.\n";
    
    /** Uses semi-connections. 
        @see @ref playingdeterminedstates 
    */
    return MostOverlappingMove(
        brd.Cons(other).SemiExists() ?
        brd.Cons(other).GetSemiCarriers() : brd.Cons(other).GetFullCarriers(),
        brd.GetInferiorCells());
}

} // anonymous namespace

//----------------------------------------------------------------------------

bool EndgameUtil::IsWonGame(const HexBoard& brd, HexColor color, 
                            bitset_t& proof)
{
	/*modified for rex*/
    if (brd.GetGroups().GetWinner() == color)
    {
        proof = StonesInProof(brd, color);
        return true;
    }
    bitset_t carrier;
    //if opponent pairing exists we win
    if (brd.Cons(!color).SmallestFullCarrier(carrier))
    {
        proof = carrier | StonesInProof(brd, color);
        return true;
    }
    if(brd.GetPosition().GetEmpty().count()%2 == 0 )
    {
		//if even number of cells remain and opponent pre-pairing exist we win
		if (brd.Cons(!color).SmallestSemiCarrier(carrier))
		{
			proof = carrier | StonesInProof(brd, color);
			return true;
		}
		//if even number of cells remain and board is color symmetric we win
		if(CheckColorSymmetry(brd.GetPosition()))
			return true;

    }
    return false;
}

bool EndgameUtil::IsLostGame(const HexBoard& brd, HexColor color, 
                             bitset_t& proof)
{
	/*modified for rex*/
    if (brd.GetGroups().GetWinner() == !color)
    {
        proof = StonesInProof(brd, !color);
        return true;
    }
    bitset_t carrier;
    //if we have a pairing-strat we lose
    if (brd.Cons(color).SmallestFullCarrier(carrier))
    {
        proof = carrier | StonesInProof(brd, !color);
        return true;
    }

    if(brd.GetPosition().GetEmpty().count()%2==1){
    //if oddnumber of cells remain and we have pre-pairing we lose
		if (brd.Cons(color).SmallestSemiCarrier(carrier))
		{
			proof = carrier | StonesInProof(brd, color);
			return true;
		}
		if(CheckColorSymmetry(brd.GetPosition()))
			return true;
    }
    return false;
}

bool EndgameUtil::IsDeterminedState(const HexBoard& brd, HexColor color, 
                                    HexEval& score, bitset_t& proof)
{
    score = 0;
    if (IsWonGame(brd, color, proof))
    {
        score = IMMEDIATE_WIN;
        return true;
    }
    if (IsLostGame(brd, color, proof))
    {
        score = IMMEDIATE_LOSS;
        return true;
    }
    return false;
}

HexPoint EndgameUtil::PlayDeterminedState(const HexBoard& brd, HexColor color)
                                          
{
    BenzeneAssert(HexColorUtil::isBlackWhite(color));
    BenzeneAssert(IsDeterminedState(brd, color));
    BenzeneAssert(!brd.GetGroups().IsGameOver());

    if (IsWonGame(brd, color))
        return PlayWonGame(brd, color);

    BenzeneAssert(IsLostGame(brd, color));
    return PlayLostGame(brd, color);
}

bitset_t EndgameUtil::MovesToConsider(const HexBoard& brd, HexColor color)
{
    BenzeneAssert(HexColorUtil::isBlackWhite(color));
    BenzeneAssert(!IsDeterminedState(brd, color));
    
    bitset_t consider = ComputeConsiderSet(brd, color);
    BenzeneAssert(consider.any());

    LogFine() << "Moves to consider for " << color << ":" 
              << brd.Write(consider) << '\n';
    return consider;
}

//----------------------------------------------------------------------------
