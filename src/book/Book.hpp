//----------------------------------------------------------------------------
/** @file Book.hpp
 */
//----------------------------------------------------------------------------

#ifndef OPENINGBOOK_HPP
#define OPENINGBOOK_HPP

#include "Hex.hpp"
#include "HexBoard.hpp"
#include "HashDB.hpp"
#include "HexEval.hpp"
#include "PositionDB.hpp"

_BEGIN_BENZENE_NAMESPACE_

//----------------------------------------------------------------------------

/** @defgroup openingbook Automatic Opening Book Construction
    Hex-specific opening book construction.

    Code is based on Thomas R. Lincke's paper "Strategies for the
    Automatic Construction of Opening Books" published in 2001.
    
    We make the following adjustments:
    - Neither side is assumed to be the book player, so the expansion
      formula is identical for all nodes (see page 80 of the paper). In other
      words, both sides can play sub-optimal moves.
    - We do not include the swap rule as a move, since this would lead to
      redundant evaluation computations (such as a2-f6 and a2-swap-f6). 
      We do handle swap implicitly, however. States in which swap is a valid 
      move are scored taking it into account. 
    - A single node for each state is stored, such that transpositions
      are not re-computed. Hence the book forms a DAG of states, not a tree.
    - Progressive widening is used on internal nodes to restrict the 
      search initially. 

    We also think there is a typo with respect to the formula of epo_i on
    page 80. Namely, since p_i is the negamax of p_{s_j}s, then we should
    sum the values to find the distance from optimal, not subtract. That is,
    we use epo_i = 1 + min(s_j) (epb_{s_j} + alpha*(p_i + p_{s_j}) instead.

    @todo
    - Book expansion using game records (in addition to Lincke's formula)
    - Function for child/move selection, based on level of information
      and propagated value. 
    - Make game independent.
*/

//----------------------------------------------------------------------------

/** State in the Opening Book. 
    @ingroup openingbook
 */
class BookNode
{
public:

    //------------------------------------------------------------------------

    static const int DUMMY_VALUE = -9999999;
    static const int DUMMY_PRIORITY = 9999999;
    static const int LEAF_PRIORITY = 0;
    static const HexPoint DUMMY_SUCC = INVALID_POINT;
    static const HexPoint LEAF_SUCC = INVALID_POINT;

    //------------------------------------------------------------------------

    /** Heuristic value of this state. */
    float m_heurValue;

    /** Minmax value of this state. */
    float m_value;

    /** Expansion priority. */
    float m_priority;

    /** Number of times this node was explored. */
    unsigned m_count;
    
    //------------------------------------------------------------------------

    /** Constructors. Note that we should only construct leaves. */
    // @{

    BookNode();

    BookNode(float heuristicValue);

    // @}
    
    //------------------------------------------------------------------------

    /** Returns value of board, taking into account swap moves. */ 
    float Value(const StoneBoard& brd) const;

    /** Returns score for this node, taking into account the amount of
        information in the subtree. Use to select moves when using
        book. Note the score is from the pov of the player moving into
        this position, not for the player to move in this position.
    */
    float Score(const StoneBoard& brd, float countWeight) const;

    //------------------------------------------------------------------------

    /** @name Additional properties */
    // @{

    /** Returns true iff this node is a leaf in the opening book. */
    bool IsLeaf() const;

    /** Returns true if node's propagated value is a win or a loss. */
    bool IsTerminal() const;

    // @}

    //------------------------------------------------------------------------

    /** @name Update methods */
    // @{

    /** Increment the nodes counter. */
    void IncrementCount();

    // @}

    //------------------------------------------------------------------------

    /** @name Methods for PositionDBStateConcept (so it can be stored
        in a PositionDB) */
    // @{

    int PackedSize() const;

    byte* Pack() const;

    void Unpack(const byte* t);

    void Rotate(const ConstBoard& brd);

    // @}

    //------------------------------------------------------------------------

    /** Outputs node in string form. */
    std::string toString() const;

private:

};

inline BookNode::BookNode()
    : m_heurValue(DUMMY_VALUE),
      m_value(DUMMY_VALUE),
      m_priority(DUMMY_PRIORITY),
      m_count(0)
{
}

inline BookNode::BookNode(float heuristicValue)
    : m_heurValue(heuristicValue),
      m_value(heuristicValue),
      m_priority(LEAF_PRIORITY),
      m_count(0)
{
}

inline void BookNode::IncrementCount()
{
    m_count++;
}

inline int BookNode::PackedSize() const
{
    return sizeof(BookNode);
}

inline byte* BookNode::Pack() const
{
    return (byte*)this;
}

inline void BookNode::Unpack(const byte* t)
{
    *this = *(const BookNode*)t;
}

inline void BookNode::Rotate(const ConstBoard& brd)
{
    SG_UNUSED(brd);
    // No rotation-dependant data
}

//----------------------------------------------------------------------------

/** Extends standard stream output operator for BookNodes. */
inline std::ostream& operator<<(std::ostream& os, const BookNode& node)
{
    os << node.toString();
    return os;
}

//----------------------------------------------------------------------------

/** A book is just a database of BookNodes. */
typedef PositionDB<BookNode> Book;

//----------------------------------------------------------------------------

/** Utilities on Books. 
    @ingroup openingbook
*/
namespace BookUtil
{
    /** Evaluation for other player. */
    float InverseEval(float eval);

    /** Returns number of child states existing in this book. */
    unsigned NumChildren(const Book& book, const StoneBoard& brd);

    /** Returns the priority of expanding the child node. */
    float ComputePriority(const StoneBoard& brd, const BookNode& parent,
                          const BookNode& child, double alpha);
    
    /** Re-computes node's value by checking all children. Does
        nothing if node has no children. */
    void UpdateValue(const Book& book, BookNode& node, StoneBoard& brd);

    /** Re-computes node's priority and returns the best child to
        expand. Requires that UpdateValue() has been called on this
        node. Returns INVALID_POINT if node has no children. */
    HexPoint UpdatePriority(const Book& book, BookNode& node, 
                            StoneBoard& brd, float alpha);

    /** Finds best response in book.
        @todo Does not consider SWAP_PIECES if it is available.
        Returns INVALID_POINT if not in book or if node's count is 
        less than minCount. */
    HexPoint BestMove(const Book& book, const StoneBoard& pos,
                      unsigned minCount, float countWeight);

    //-----------------------------------------------------------------------

    /** Writes a (score, depth) pair to output stream for each leaf in
        the book. Can be visualized with GnuPlot. */
    void DumpVisualizationData(const Book& book, StoneBoard& brd, 
                               int depth, std::ostream& out);

    //-----------------------------------------------------------------------

    /** Writes variations leading to non-terminal leafs whose values
        differ from 0.5 by at least polarization. The given pv must be
        the variation leading to the current state of the board. */
    void DumpPolarizedLeafs(const Book& book, StoneBoard& brd,
                            float polarization, PointSequence& pv, 
                            std::ostream& out, const PositionSet& ignoreSet);

    /** Reads solved leaf positions from a file and adds them to the
        given book. Overwrites value of any existing states. */
    void ImportSolvedStates(Book& book, const ConstBoard& constBoard,
                            std::istream& positions);
    
    //-----------------------------------------------------------------------

    /** Returns the depth of the mainline from the given position. */
    int GetMainLineDepth(const Book& book, const StoneBoard& pos);

    /** Returns the number of nodes in the tree rooted at the current
        position. */
    std::size_t GetTreeSize(const Book& book, const StoneBoard& brd);
}

//----------------------------------------------------------------------------

_END_BENZENE_NAMESPACE_

#endif // OPENINGBOOK_HPP