#include "creature_tracker.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <initializer_list>
#include <limits>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "avatar.h"
#include "cata_assert.h"
#include "debug.h"
#include "game.h"
#include "map.h"
#include "map_scale_constants.h"
#include "mapdata.h"
#include "maptile_fwd.h"
#include "mongroup.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "point.h"
#include "string_formatter.h"
#include "submap.h"  // IWYU pragma: keep
#include "type_id.h"

class Character;

static const efftype_id effect_ridden( "ridden" );

#define dbg(x) DebugLog((x),D_GAME) << __FILE__ << ":" << __LINE__ << ": "

creature_tracker::creature_tracker() = default;

creature_tracker::~creature_tracker() = default;

shared_ptr_fast<monster> creature_tracker::find( const tripoint_abs_ms &pos ) const
{
    const auto iter = monsters_by_location.find( pos );
    if( iter != monsters_by_location.end() ) {
        const shared_ptr_fast<monster> &mon_ptr = iter->second;
        if( !mon_ptr->is_dead() ) {
            return mon_ptr;
        }
    }
    return nullptr;
}

int creature_tracker::temporary_id( const monster &critter ) const
{
    const auto iter = std::find_if( monsters_list.begin(), monsters_list.end(),
    [&]( const shared_ptr_fast<monster> &ptr ) {
        return ptr.get() == &critter;
    } );
    if( iter == monsters_list.end() ) {
        return -1;
    }
    return iter - monsters_list.begin();
}

shared_ptr_fast<monster> creature_tracker::from_temporary_id( const int id )
{
    if( static_cast<size_t>( id ) < monsters_list.size() ) {
        return monsters_list[id];
    } else {
        return nullptr;
    }
}

bool creature_tracker::add( const shared_ptr_fast<monster> &critter_ptr )
{
    cata_assert( critter_ptr );
    map *here = &get_map();
    monster &critter = *critter_ptr;

    if( critter.type->id.is_null() ) { // Don't want to spawn null monsters o.O
        return false;
    }

    if( const shared_ptr_fast<monster> existing_mon_ptr = find( critter.pos_abs() ) ) {
        // We can spawn stuff on hallucinations, but we need to kill them first
        if( existing_mon_ptr->is_hallucination() ) {
            existing_mon_ptr->die( here, nullptr );
            // But don't remove - that would change the monster order and could segfault
        } else if( critter.is_hallucination() ) {
            return false;
        } else {
            debugmsg( "there's already a monster at %s", critter.pos_abs().to_string_writable() );
            return false;
        }
    }

    if( MonsterGroupManager::monster_is_blacklisted( critter.type->id ) ) {
        return false;
    }

    monsters_list.emplace_back( critter_ptr );
    monsters_by_location[critter.pos_abs()] = critter_ptr;
    return true;
}

size_t creature_tracker::size() const
{
    return monsters_list.size();
}

bool creature_tracker::update_pos( const monster &critter, const tripoint_abs_ms &old_pos,
                                   const tripoint_abs_ms &new_pos )
{
    map &here = get_map();

    if( critter.is_dead() ) {
        // find ignores dead critters anyway, changing their position in the
        // monsters_by_location map is useless.
        remove_from_location_map( critter );
        return true;
    }

    if( const shared_ptr_fast<monster> new_critter_ptr = find( new_pos ) ) {
        auto &othermon = *new_critter_ptr;
        if( othermon.is_hallucination() ) {
            othermon.die( &here, nullptr );
        } else {
            debugmsg( "update_zombie_pos: wanted to move %s to %s, but new location already has %s",
                      critter.disp_name(), new_pos.to_string_writable(), othermon.disp_name() );
            return false;
        }
    }

    const auto iter = std::find_if( monsters_list.begin(), monsters_list.end(),
    [&]( const shared_ptr_fast<monster> &ptr ) {
        return ptr.get() == &critter;
    } );
    if( iter != monsters_list.end() ) {
        monsters_by_location.erase( old_pos );
        monsters_by_location[new_pos] = *iter;
        return true;
    } else {
        // We're changing the x/y/z coordinates of a zombie that hasn't been added
        // to the game yet. `add` will update monsters_by_location for us.
        debugmsg( "update_zombie_pos: no %s at %s (moving to %s)",
                  critter.disp_name(), old_pos.to_string_writable(), new_pos.to_string_writable() );
        // Rebuild cache in case the monster actually IS in the game, just bugged
        rebuild_cache();
        return false;
    }
}

void creature_tracker::remove_from_location_map( const monster &critter )
{
    const auto pos_iter = monsters_by_location.find( critter.pos_abs() );
    if( pos_iter != monsters_by_location.end() && pos_iter->second.get() == &critter ) {
        monsters_by_location.erase( pos_iter );
        return;
    }

    // When it's not in the map at its current location, it might still be there under,
    // another location, so look for it.
    const auto iter = std::find_if( monsters_by_location.begin(), monsters_by_location.end(),
    [&]( const decltype( monsters_by_location )::value_type & v ) {
        return v.second.get() == &critter;
    } );
    if( iter != monsters_by_location.end() ) {
        monsters_by_location.erase( iter );
    }
}

void creature_tracker::remove( const monster &critter )
{
    const auto iter = std::find_if( monsters_list.begin(), monsters_list.end(),
    [&]( const shared_ptr_fast<monster> &ptr ) {
        return ptr.get() == &critter;
    } );
    if( iter == monsters_list.end() ) {
        debugmsg( "Tried to remove invalid monster %s", critter.name() );
        return;
    }

    remove_from_location_map( critter );
    removed_this_turn_.emplace( *iter );
    monsters_list.erase( iter );
}

void creature_tracker::clear()
{
    monsters_list.clear();
    monsters_by_location.clear();
    removed_this_turn_.clear();
    creatures_by_zone_and_faction_.clear();
    invalidate_reachability_cache();
}

void creature_tracker::rebuild_cache()
{
    monsters_by_location.clear();
    for( const shared_ptr_fast<monster> &mon_ptr : monsters_list ) {
        monsters_by_location[mon_ptr->pos_abs()] = mon_ptr;
    }
}

bool creature_tracker::is_present( Creature *creature ) const
{
    if( creature->is_monster() ) {
        if( const auto iter = monsters_by_location.find( creature->pos_abs() );
            iter != monsters_by_location.end() ) {
            if( static_cast<const Creature *>( iter->second.get() ) == creature ) {
                return !iter->second->is_dead();
            }
        }
    } else if( creature->is_avatar() ) {
        return true;
    } else if( creature->is_npc() ) {
        for( const shared_ptr_fast<npc> &cur_npc : active_npc ) {
            if( static_cast<const Creature *>( cur_npc.get() ) == creature ) {
                return !cur_npc->is_dead();
            }
        }
    }
    return false;
}

void creature_tracker::swap_positions( monster &first, monster &second )
{
    if( first.pos_abs() == second.pos_abs() ) {
        return;
    }

    if( first.get_reachable_zone() != second.get_reachable_zone() ) {
        invalidate_reachability_cache();
    }

    // Either of them may be invalid!
    const auto first_iter = monsters_by_location.find( first.pos_abs() );
    const auto second_iter = monsters_by_location.find( second.pos_abs() );
    // implied: first_iter != second_iter

    shared_ptr_fast<monster> first_ptr;
    if( first_iter != monsters_by_location.end() ) {
        first_ptr = first_iter->second;
        monsters_by_location.erase( first_iter );
    }

    shared_ptr_fast<monster> second_ptr;
    if( second_iter != monsters_by_location.end() ) {
        second_ptr = second_iter->second;
        monsters_by_location.erase( second_iter );
    }
    // implied: (first_ptr != second_ptr) or (first_ptr == nullptr && second_ptr == nullptr)

    const tripoint_abs_ms temp = second.pos_abs();
    second.spawn( first.pos_abs() );
    first.spawn( temp );

    // If the pointers have been taken out of the list, put them back in.
    if( first_ptr ) {
        monsters_by_location[first.pos_abs()] = first_ptr;
    }
    if( second_ptr ) {
        monsters_by_location[second.pos_abs()] = second_ptr;
    }
}

bool creature_tracker::kill_marked_for_death()
{
    map &here = get_map();
    // Important: `Creature::die` must not be called after creature objects (NPCs, monsters) have
    // been removed, the dying creature could still have a pointer (the killer) to another creature.
    bool monster_is_dead = false;
    // Copy the list so we can iterate the copy safely *and* add new monsters from within monster::die
    // This happens for example with blob monsters (they split into two smaller monsters).
    const auto copy = monsters_list;
    for( const shared_ptr_fast<monster> &mon_ptr : copy ) {
        cata_assert( mon_ptr );
        monster &critter = *mon_ptr;
        if( !critter.is_dead() ) {
            continue;
        }
        dbg( D_INFO ) << string_format( "cleanup_dead: critter at %s hp:%d %s",
                                        critter.pos_abs().to_string_writable(),
                                        critter.get_hp(), critter.name() );
        critter.die( &here, nullptr );
        monster_is_dead = true;
    }

    return monster_is_dead;
}

void creature_tracker::remove_dead()
{
    // Can't use game::all_monsters() as it would not contain *dead* monsters.
    for( auto iter = monsters_list.begin(); iter != monsters_list.end(); ) {
        monster *const critter = iter->get();
        if( critter->is_dead() ) {
            remove_from_location_map( *critter );
            iter = monsters_list.erase( iter );
        } else {
            ++iter;
        }
    }
    removed_this_turn_.clear();
}

template<typename T>
T *creature_tracker::creature_at( const tripoint_bub_ms &p, bool allow_hallucination )
{
    return creature_at<T>( get_map().get_abs( p ), allow_hallucination );
}

template<typename T>
T *creature_tracker::creature_at( const tripoint_abs_ms &p, bool allow_hallucination )
{
    if( const shared_ptr_fast<monster> mon_ptr = find( p ) ) {
        if( !allow_hallucination && mon_ptr->is_hallucination() ) {
            return nullptr;
        }
        // if we wanted to check for an NPC / player / avatar,
        // there is sometimes a monster AND an NPC/player there at the same time.
        // because the NPC/player etc may be riding that monster.
        // so only return the monster if we were actually looking for a monster.
        // otherwise, keep looking for the rider.
        // creature_at<creature> or creature_at() with no template will still default to returning monster first,
        // which is ok for the occasions where that happens.
        if( !mon_ptr->has_effect( effect_ridden ) || ( std::is_same_v<T, monster> ||
                std::is_same_v<T, Creature> || std::is_same_v<T, const monster> ||
                std::is_same_v<T, const Creature> ) ) {
            return dynamic_cast<T *>( mon_ptr.get() );
        }
    }
    constexpr bool is_npc = std::is_same_v<T, npc> || std::is_same_v<T, const npc>;
    if( !is_npc ) {
        avatar &you = get_avatar();
        if( p == you.pos_abs() ) {
            return dynamic_cast<T *>( &you );
        }
    }
    for( auto &cur_npc : active_npc ) {
        if( cur_npc->pos_abs() == p && !cur_npc->is_dead() ) {
            return dynamic_cast<T *>( cur_npc.get() );
        }
    }
    return nullptr;
}

namespace
{

using zone_row = std::bitset<MAPSIZE_X>;
using zone_level = std::array<zone_row, MAPSIZE_Y>;

// Which tiles of the reality bubble one creature zone covers. Creatures see and move
// through the same tiles in long runs (open sky above the city is most of a zone), so
// this works on whole rows of tiles at once and only asks the map about a tile when
// the zone first touches it.
class zone_fill
{
    public:
        void reset( map &here ) {
            here_ = &here;
            for( zone_level &level : reached_ ) {
                for( zone_row &row : level ) {
                    row.reset();
                }
            }
            for( std::bitset<MAPSIZE_Y> &known : open_known_ ) {
                known.reset();
            }
            for( std::bitset<MAPSIZE_Y> &queued : queued_ ) {
                queued.reset();
            }
            work_.clear();
            vertical_.clear();
            for( std::array<zone_level, OVERMAP_LAYERS> &tried : tried_ ) {
                for( zone_level &level : tried ) {
                    for( zone_row &row : level ) {
                        row.reset();
                    }
                }
            }
        }

        void fill_from( const tripoint_bub_ms &start );

        bool reached( const tripoint_bub_ms &p ) const {
            return p.x() >= 0 && p.x() < MAPSIZE_X && p.y() >= 0 && p.y() < MAPSIZE_Y &&
                   p.z() >= -OVERMAP_DEPTH && p.z() <= OVERMAP_HEIGHT &&
                   reached_[p.z() + OVERMAP_DEPTH][p.y()][p.x()];
        }

    private:
        // Tiles a creature can see or move through, ignoring fields.
        const zone_row &open_row( int z, int y );
        // Whether a creature can get into this tile from the level below or above.
        bool enters_from_below( const tripoint_bub_ms &p ) const;
        bool enters_from_above( const tripoint_bub_ms &p ) const;
        bool passable_down( const tripoint_bub_ms &p, const maptile &mt, const ter_t &ter ) const;
        void reach( int z, int y, const zone_row &add );
        // Spreads through each level from the queued rows.
        void fill_rows();
        // Takes one step to another level, returning whether it found a way.
        bool step_vertical();

        map *here_ = nullptr;
        std::array<zone_level, OVERMAP_LAYERS> reached_;
        std::array<zone_level, OVERMAP_LAYERS> open_;
        std::array<std::bitset<MAPSIZE_Y>, OVERMAP_LAYERS> open_known_;
        std::array<std::bitset<MAPSIZE_Y>, OVERMAP_LAYERS> queued_;
        // Tiles already asked whether they lead up (0) or down (1).
        std::array<std::array<zone_level, OVERMAP_LAYERS>, 2> tried_;
        std::vector<point> work_;
        // Rows to look for holes and stairs in, as (y, z).
        std::vector<point> vertical_;
};

const zone_row &zone_fill::open_row( int z, int y )
{
    const int zi = z + OVERMAP_DEPTH;
    zone_row &row = open_[zi][y];
    if( !open_known_[zi][y] ) {
        open_known_[zi].set( y );
        row.reset();
        for( int x = 0; x < MAPSIZE_X; ++x ) {
            const tripoint_bub_ms p( x, y, z );
            if( here_->inbounds( p ) && ( here_->is_transparent_wo_fields( p ) || here_->passable( p ) ) ) {
                row.set( x );
            }
        }
    }
    return row;
}

bool zone_fill::passable_down( const tripoint_bub_ms &p, const maptile &mt,
                               const ter_t &ter ) const
{
    // Only holes and stairs connect levels, so check that first.
    return ( ter.has_flag( ter_furn_flag::TFLAG_NO_FLOOR ) ||
             ter.has_flag( ter_furn_flag::TFLAG_GOES_DOWN ) ) &&
           ( ( ter.movecost != 0 && mt.get_furn_t().movecost >= 0 ) ||
             here_->is_transparent_wo_fields( p ) );
}

bool zone_fill::enters_from_below( const tripoint_bub_ms &p ) const
{
    const maptile &up = here_->maptile_at( p );
    const ter_t &up_ter = up.get_ter_t();
    return !up_ter.id.is_null() && passable_down( p, up, up_ter );
}

bool zone_fill::enters_from_above( const tripoint_bub_ms &p ) const
{
    // The hole is in the tile above, but the furniture checked is the one below it.
    const maptile &up = here_->maptile_at( p + tripoint::above );
    const ter_t &up_ter = up.get_ter_t();
    return !up_ter.id.is_null() && passable_down( p, here_->maptile_at( p ), up_ter );
}

void zone_fill::reach( int z, int y, const zone_row &add )
{
    const int zi = z + OVERMAP_DEPTH;
    reached_[zi][y] |= add;
    if( !queued_[zi][y] ) {
        queued_[zi].set( y );
        work_.emplace_back( y, z );
    }
}

void zone_fill::fill_from( const tripoint_bub_ms &start )
{
    if( !reached( start ) && start.x() >= 0 && start.x() < MAPSIZE_X && start.y() >= 0 &&
        start.y() < MAPSIZE_Y && start.z() >= -OVERMAP_DEPTH && start.z() <= OVERMAP_HEIGHT &&
        open_row( start.z(), start.y() )[start.x()] ) {
        zone_row seed;
        seed.set( start.x() );
        reach( start.z(), start.y(), seed );
    }
    do {
        fill_rows();
    } while( step_vertical() );
}

void zone_fill::fill_rows()
{
    while( !work_.empty() ) {
        const point zy = work_.back();
        work_.pop_back();
        const int y = zy.x;
        const int z = zy.y;
        const int zi = z + OVERMAP_DEPTH;
        queued_[zi].reset( y );

        // Spread along the row to the ends of each open run.
        const zone_row &open = open_row( z, y );
        zone_row &row = reached_[zi][y];
        if( open.all() ) {
            row = open;
        } else {
            zone_row prev;
            do {
                prev = row;
                row = ( row | row << 1 | row >> 1 ) & open;
            } while( row != prev );
        }
        const zone_row spread = row | row << 1 | row >> 1;
        for( const int y2 : {
                 y - 1, y + 1
             } ) {
            if( y2 < 0 || y2 >= MAPSIZE_Y ) {
                continue;
            }
            const zone_row add = spread & open_row( z, y2 ) & ~reached_[zi][y2];
            if( add.any() ) {
                reach( z, y2, add );
            }
        }
        // Holes and stairs lead to the levels above and below. Look for them once this
        // level is filled, as by then most of the tiles they lead to are already reached.
        vertical_.emplace_back( y, z );
    }
}

bool zone_fill::step_vertical()
{
    while( !vertical_.empty() ) {
        const point zy = vertical_.back();
        const int y = zy.x;
        const int z = zy.y;
        const zone_row &row = reached_[z + OVERMAP_DEPTH][y];
        for( const int dz : {
                 1, -1
             } ) {
            const int z2 = z + dz;
            if( dz > 0 ? z2 >= OVERMAP_HEIGHT : z2 < -OVERMAP_DEPTH ) {
                continue;
            }
            zone_row &tried = tried_[dz > 0 ? 0 : 1][z2 + OVERMAP_DEPTH][y];
            const zone_row candidates = row & ~reached_[z2 + OVERMAP_DEPTH][y] & ~tried;
            if( candidates.none() ) {
                continue;
            }
            const zone_row &open2 = open_row( z2, y );
            for( int x = 0; x < MAPSIZE_X; ++x ) {
                if( !candidates[x] ) {
                    continue;
                }
                tried.set( x );
                const tripoint_bub_ms p( x, y, z2 );
                if( open2[x] && ( dz > 0 ? enters_from_below( p ) : enters_from_above( p ) ) ) {
                    // Fill from here before trying the rest of this row.
                    zone_row add;
                    add.set( x );
                    reach( z2, y, add );
                    return true;
                }
            }
        }
        vertical_.pop_back();
    }
    return false;
}

} // namespace

void creature_tracker::faction_changed( Creature &critter, const mfaction_id &old_faction )
{
    if( dirty_ ) {
        return;
    }
    const auto zone_iter = creatures_by_zone_and_faction_.find( critter.get_reachable_zone() );
    if( zone_iter == creatures_by_zone_and_faction_.end() ) {
        // Not grouped yet; its zone will be filled before anyone looks it up.
        return;
    }
    const auto old_iter = zone_iter->second.find( old_faction );
    if( old_iter != zone_iter->second.end() ) {
        std::vector<shared_ptr_fast<Creature>> &group = old_iter->second;
        const auto it = std::find_if( group.begin(), group.end(),
        [&critter]( const shared_ptr_fast<Creature> &ptr ) {
            return ptr.get() == &critter;
        } );
        if( it != group.end() ) {
            shared_ptr_fast<Creature> ptr = std::move( *it );
            group.erase( it );
            zone_iter->second[critter.get_monster_faction()].emplace_back( std::move( ptr ) );
            return;
        }
    }
    // The zone does not list it under its old faction, so rebuild the zones to be safe.
    invalidate_reachability_cache();
}

/** This is lazily evaluated on demand. Each creature in a zone is visited
 * as it flood fills, then the zone number is incremented. At the end all creatures in
 * the same zone will have the same zone number assigned, which can be used to have creatures in
 * different zones ignore each other very cheaply.
 */
void creature_tracker::flood_fill_zone( const Creature &origin )
{
    if( dirty_ ) {
        creatures_by_zone_and_faction_.clear();
        zone_tick_ = zone_tick_ > 0 ? -1 : 1;
        zone_number_ = 1;
        dirty_ = false;
    }

    // This check insures we only flood fill when the target monster has an uninitialized zone,
    // or if it has a zone from last turn.  In other words it only triggers on
    // the first monster in a zone each turn. We can detect this because the sign
    // of the zone numbers changes on every invalidation.
    int old_zone = origin.get_reachable_zone();
    // Compare with zone_tick == old_zone && old_zone != 0
    if( old_zone * zone_tick_ > 0 ) {
        return;
    }

    map &here = get_map();

    static std::unique_ptr<zone_fill> fill = std::make_unique<zone_fill>();
    fill->reset( here );
    fill->fill_from( origin.pos_bub() );

    // Visit the creatures in the zone in a fixed order.
    std::vector<tripoint_bub_ms> spots;
    spots.reserve( monsters_by_location.size() + active_npc.size() + 1 );
    const auto add_spot = [&here, &spots]( const tripoint_abs_ms & abs ) {
        const tripoint_bub_ms p = here.get_bub( abs );
        if( here.inbounds( p ) ) {
            spots.push_back( p );
        }
    };
    for( const std::pair<const tripoint_abs_ms, shared_ptr_fast<monster>> &mon :
         monsters_by_location ) {
        add_spot( mon.first );
    }
    for( const shared_ptr_fast<npc> &guy : active_npc ) {
        add_spot( guy->pos_abs() );
    }
    add_spot( get_avatar().pos_abs() );
    std::sort( spots.begin(), spots.end() );
    spots.erase( std::unique( spots.begin(), spots.end() ), spots.end() );
    for( const tripoint_bub_ms &loc : spots ) {
        if( !fill->reached( loc ) ) {
            continue;
        }
        if( Creature *creature = this->creature_at<Creature>( loc, true ) ) {
            if( shared_ptr_fast<Creature> ptr = g->shared_from( *creature ) ) {
                const int n = zone_number_ * zone_tick_;
                creatures_by_zone_and_faction_[n][creature->get_monster_faction()].emplace_back( std::move( ptr ) );
                creature->set_reachable_zone( n );
            }
        }
    }
    if( zone_number_ == std::numeric_limits<int>::max() ) {
        zone_number_ = 1;
    } else {
        zone_number_++;
    }
}

template<typename T>
const T *creature_tracker::creature_at( const tripoint_bub_ms &p, bool allow_hallucination ) const
{
    return creature_at<T>( get_map().get_abs( p ), allow_hallucination );
}

template<typename T>
const T *creature_tracker::creature_at( const tripoint_abs_ms &p, bool allow_hallucination ) const
{
    return const_cast<creature_tracker *>( this )->creature_at<T>( p, allow_hallucination );
}

template const monster *creature_tracker::creature_at<monster>( const tripoint_bub_ms &,
        bool ) const;
template const monster *creature_tracker::creature_at<monster>( const tripoint_abs_ms &,
        bool ) const;
template monster *creature_tracker::creature_at<monster>( const tripoint_bub_ms &, bool );
template monster *creature_tracker::creature_at<monster>( const tripoint_abs_ms &, bool );
template const npc *creature_tracker::creature_at<npc>( const tripoint_bub_ms &, bool ) const;
template const npc *creature_tracker::creature_at<npc>( const tripoint_abs_ms &, bool ) const;
template npc *creature_tracker::creature_at<npc>( const tripoint_bub_ms &, bool );
template npc *creature_tracker::creature_at<npc>( const tripoint_abs_ms &, bool );
template const avatar *creature_tracker::creature_at<avatar>( const tripoint_bub_ms &, bool ) const;
template const avatar *creature_tracker::creature_at<avatar>( const tripoint_abs_ms &, bool ) const;
template avatar *creature_tracker::creature_at<avatar>( const tripoint_bub_ms &, bool );
template avatar *creature_tracker::creature_at<avatar>( const tripoint_abs_ms &, bool );
template const Character *creature_tracker::creature_at<Character>( const tripoint_bub_ms &,
        bool ) const;
template const Character *creature_tracker::creature_at<Character>( const tripoint_abs_ms &,
        bool ) const;
template Character *creature_tracker::creature_at<Character>( const tripoint_bub_ms &, bool );
template Character *creature_tracker::creature_at<Character>( const tripoint_abs_ms &, bool );
template const Creature *creature_tracker::creature_at<Creature>( const tripoint_bub_ms &,
        bool ) const;
template const Creature *creature_tracker::creature_at<Creature>( const tripoint_abs_ms &,
        bool ) const;
template Creature *creature_tracker::creature_at<Creature>( const tripoint_bub_ms &, bool );
template Creature *creature_tracker::creature_at<Creature>( const tripoint_abs_ms &, bool );
