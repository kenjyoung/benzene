//----------------------------------------------------------------------------
/** @file
 */
//----------------------------------------------------------------------------

#ifndef MOHEXPLAYER_HPP
#define MOHEXPLAYER_HPP

#include "BenzenePlayer.hpp"
#include "HexUctSearch.hpp"
#include "HexUctPolicy.hpp"

//----------------------------------------------------------------------------

/** Player using UCT to generate moves. */
class MoHexPlayer : public BenzenePlayer
{
public:

    /** Constructor. */
    MoHexPlayer();

    /** Destructor. */
    virtual ~MoHexPlayer();

    /** Returns "mohex". */
    std::string name() const;

    /** Returns the search. */
    HexUctSearch& Search();

    /** Returns the search. */
    const HexUctSearch& Search() const;

    /** Copy settings from other player. */
    void CopySettingsFrom(const MoHexPlayer& other);

    //-----------------------------------------------------------------------

    /** @name Parameters */
    // @{

    bool BackupIceInfo() const;

    void SetBackupIceInfo(bool enable);

    /** Max number of games to play. */
    int MaxGames() const;

    /** See MaxGames() */
    void SetMaxGames(int games);

    /** Maximum time to spend on search (in seconds). */
    double MaxTime() const;

    /** See MaxTime() */
    void SetMaxTime(double time);

    /** Search is initialized using the subttree of the last search
        tree rooted at the current position. */
    bool ReuseSubtree() const;
    
    /** See ReuseSubtree() */
    void SetReuseSubtree(bool reuse);

    // @}

protected:

    HexUctSharedPolicy m_shared_policy;
    
    HexUctSearch m_search;
   
    bool m_backup_ice_info;

    /** See MaxGames() */
    int m_max_games;

    /** See MaxTime() */
    double m_max_time;

    /** See ReuseSubtree() */
    bool m_reuse_subtree;

    /** Generates a move in the given gamestate using uct. */
    virtual HexPoint search(HexBoard& brd, const Game& game_state,
			    HexColor color, const bitset_t& consider,
                            double time_remaining, double& score);

    /** Returns relevant subtree from previous searchtree, if
        possible. */
    SgUctTree* TryReuseSubtree(const Game& game);

};

inline std::string MoHexPlayer::name() const
{
    return "mohex";
}

inline HexUctSearch& MoHexPlayer::Search()
{
    return m_search;
}

inline const HexUctSearch& MoHexPlayer::Search() const
{
    return m_search;
}

inline bool MoHexPlayer::BackupIceInfo() const
{
    return m_backup_ice_info;
}

inline void MoHexPlayer::SetBackupIceInfo(bool enable)
{
    m_backup_ice_info = enable;
}

inline int MoHexPlayer::MaxGames() const
{
    return m_max_games;
}

inline void MoHexPlayer::SetMaxGames(int games)
{
    m_max_games = games;
}

inline double MoHexPlayer::MaxTime() const
{
    return m_max_time;
}

inline void MoHexPlayer::SetMaxTime(double time)
{
    m_max_time = time;
}

inline bool MoHexPlayer::ReuseSubtree() const
{
    return m_reuse_subtree;
}

inline void MoHexPlayer::SetReuseSubtree(bool reuse)
{
    m_reuse_subtree = reuse;
}

//----------------------------------------------------------------------------

#endif // MOHEXPLAYER_HPP