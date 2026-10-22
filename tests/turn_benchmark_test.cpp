#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <ratio>
#include <string>

#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "game.h"
#include "map.h"
#include "map_scale_constants.h"
#include "monster.h"
#include "npc.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "turn_profiler.h"
#include "type_id.h"
#include "vpart_position.h"

static const efftype_id effect_sleep( "sleep" );

static const mtype_id mon_zombie( "mon_zombie" );
static const mtype_id mon_zombie_dog( "mon_zombie_dog" );

static const npc_template_id npc_template_test_talker( "test_talker" );

static const trait_id trait_DEBUG_NODMG( "DEBUG_NODMG" );
static const trait_id trait_DEBUG_NOTEMP( "DEBUG_NOTEMP" );

// Whole-turn throughput on a real generated city, for profiling.
// Run with: tests/cata_test "[turn_benchmark]"
// Tune with env BENCH_TURNS, BENCH_MONSTERS, BENCH_NPCS, BENCH_SEED.
static int env_int( const char *name, int fallback )
{
    const char *v = std::getenv( name );
    return v ? static_cast<int>( std::strtol( v, nullptr, 10 ) ) : fallback;
}

TEST_CASE( "turn_throughput_city", "[.][turn_benchmark]" )
{
    const int turns = env_int( "BENCH_TURNS", 600 );
    const int n_monsters = env_int( "BENCH_MONSTERS", 150 );
    const int n_npcs = env_int( "BENCH_NPCS", 6 );
    const int seed = env_int( "BENCH_SEED", 12345 );

    // Data loading draws from the RNG, so how much it draws decides the overmaps
    // generated after it. Start them over from a fixed seed so code changes to
    // loading don't change the scene being measured.
    rng_set_engine_seed( static_cast<unsigned int>( seed ) );
    overmap_buffer.clear();
    clear_avatar();
    avatar &u = get_avatar();
    map &here = get_map();

    // closest_city only searches overmaps that exist; generate the one we're on
    overmap_buffer.ter( project_to<coords::omt>( u.pos_abs() ) );
    const tripoint_abs_sm start = project_to<coords::sm>( u.pos_abs() );
    city_reference city = overmap_buffer.closest_city( start );
    REQUIRE( city );
    const std::chrono::steady_clock::time_point t_load0 = std::chrono::steady_clock::now();
    g->place_player_overmap( project_to<coords::omt>( city.abs_sm_pos ) );
    const std::chrono::steady_clock::time_point t_load1 = std::chrono::steady_clock::now();

    // place_player_overmap can leave the avatar off the new map when the
    // destination tile is occupied; put them back in the middle
    const tripoint_bub_ms map_center( SEEX * MAPSIZE / 2, SEEY * MAPSIZE / 2, 0 );
    if( u.pos_bub() != map_center ) {
        if( Creature *blocker = get_creature_tracker().creature_at( map_center ) ) {
            if( monster *mon = blocker->as_monster() ) {
                mon->die( &here, nullptr );
                g->cleanup_dead();
            }
        }
        u.setpos( here, map_center );
    }
    REQUIRE( u.pos_bub() == map_center );

    u.set_mutation( trait_DEBUG_NODMG );
    u.set_mutation( trait_DEBUG_NOTEMP );

    const tripoint_bub_ms center = u.pos_bub();
    int placed = 0;
    for( int i = 0; i < n_monsters * 4 && placed < n_monsters; ++i ) {
        const tripoint_bub_ms p = center + point( rng( -40, 40 ), rng( -40, 40 ) );
        if( !here.inbounds( p ) || !here.passable( p ) || get_creature_tracker().creature_at( p ) ) {
            continue;
        }
        if( g->place_critter_at( one_in( 5 ) ? mon_zombie_dog : mon_zombie, p ) ) {
            ++placed;
        }
    }
    int npcs_placed = 0;
    for( int i = 0; i < n_npcs * 10 && npcs_placed < n_npcs; ++i ) {
        const tripoint_bub_ms p = center + point( rng( -20, 20 ), rng( -20, 20 ) );
        if( !here.inbounds( p ) || !here.passable( p ) || here.veh_at( p ) ||
            get_creature_tracker().creature_at( p ) ) {
            continue;
        }
        here.place_npc( p.xy(), npc_template_test_talker );
        ++npcs_placed;
    }
    g->load_npcs();
    // an NPC that walks up to talk opens a dialogue window, which needs a UI
    for( npc &guy : g->all_npcs() ) {
        guy.set_attitude( NPCATT_NULL );
    }
    for( int i = 0; i < 6; ++i ) {
        here.add_field( center + point( rng( -15, 15 ), rng( -15, 15 ) ), field_type_id( "fd_fire" ), 3 );
    }

    std::printf( "bench: avatar at %s, abs_sub %s\n", u.pos_bub().to_string().c_str(),
                 here.get_abs_sub().to_string().c_str() );
    u.add_effect( effect_sleep, 1000_days );
    u.set_moves( 0 );

    // the first turn builds every cache from scratch; keep it out of the numbers
    u.set_moves( 0 );
    g->do_turn();
    turn_profiler::reset();
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    for( int t = 0; t < turns; ++t ) {
        u.set_moves( 0 );
        g->do_turn();
    }
    const std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>( t1 - t0 ).count();
    std::printf( "turn_benchmark: load %.1f ms; %d monsters (%zu alive at end), %d npcs; "
                 "%d turns in %.1f ms = %.3f ms/turn\n",
                 std::chrono::duration<double, std::milli>( t_load1 - t_load0 ).count(),
                 placed, g->num_creatures() - 1, npcs_placed, turns, ms, ms / turns );
    turn_profiler::report( stdout );
}
