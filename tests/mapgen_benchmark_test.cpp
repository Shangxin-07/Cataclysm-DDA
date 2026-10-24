#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <ratio>
#include <string>
#include <string_view>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "coordinates.h"
#include "item.h"
#include "map.h"
#include "map_scale_constants.h"
#include "mapbuffer.h"
#include "overmapbuffer.h"
#include "point.h"
#include "rng.h"
#include "submap.h"
#include "type_id.h"
#include "vehicle.h"

// Local map generation throughput, for profiling.
// Run with: tests/cata_test "[mapgen_benchmark]"
// Tune with env BENCH_SEED and BENCH_RADIUS (overmap tiles generated around a
// city: (2r+1)^2). Prints a fingerprint of everything generated and of the RNG
// state afterwards, so runs of two builds can be checked for identical maps.
static int env_int( const char *name, int fallback )
{
    const char *v = std::getenv( name );
    return v ? static_cast<int>( std::strtol( v, nullptr, 10 ) ) : fallback;
}

static void fingerprint_add( uint64_t &h, std::string_view s )
{
    for( const char c : s ) {
        h = ( h ^ static_cast<unsigned char>( c ) ) * 1099511628211ULL;
    }
    h = ( h ^ 0xff ) * 1099511628211ULL;
}

static void fingerprint_add( uint64_t &h, int64_t v )
{
    fingerprint_add( h, std::to_string( v ) );
}

static void fingerprint_submap( uint64_t &h, const submap &sm )
{
    for( int x = 0; x < SEEX; ++x ) {
        for( int y = 0; y < SEEY; ++y ) {
            const point_sm_ms p( x, y );
            fingerprint_add( h, sm.get_ter( p ).id().str() );
            fingerprint_add( h, sm.get_furn( p ).id().str() );
            for( const item &it : sm.get_items( p ) ) {
                fingerprint_add( h, it.typeId().str() );
                fingerprint_add( h, it.charges );
            }
        }
    }
    for( const spawn_point &sp : sm.spawns ) {
        fingerprint_add( h, sp.type.str() );
        fingerprint_add( h, sp.count );
    }
    for( const std::unique_ptr<vehicle> &veh : sm.vehicles ) {
        fingerprint_add( h, veh->type.str() );
        fingerprint_add( h, static_cast<int64_t>( veh->part_count() ) );
    }
}

TEST_CASE( "mapgen_throughput", "[.][mapgen_benchmark]" )
{
    const int seed = env_int( "BENCH_SEED", 12345 );
    const int radius = env_int( "BENCH_RADIUS", 6 );

    rng_set_engine_seed( static_cast<unsigned int>( seed ) );
    overmap_buffer.clear();

    // Map loading reads the weather of the avatar's overmap.
    const point_abs_om avatar_om = project_to<coords::om>( get_avatar().pos_abs_omt().xy() );
    overmap_buffer.get( avatar_om );
    // Work a few overmaps away from the avatar's map so nothing is loaded yet.
    const point_abs_om om_center = avatar_om + point( 3, 0 );
    for( int x = -1; x <= 1; ++x ) {
        for( int y = -1; y <= 1; ++y ) {
            overmap_buffer.get( om_center + point( x, y ) );
        }
    }
    const tripoint_abs_omt om_mid( project_to<coords::omt>( om_center ) + point( OMAPX / 2,
                                   OMAPY / 2 ), 0 );
    city_reference city = overmap_buffer.closest_city( project_to<coords::sm>( om_mid ) );
    REQUIRE( city );
    const tripoint_abs_omt center = project_to<coords::omt>( city.abs_sm_pos );
    // Generate every overmap the tiles below can touch up front, so only local
    // map generation is timed.
    const point_abs_om city_om = project_to<coords::om>( center.xy() );
    for( int x = -1; x <= 1; ++x ) {
        for( int y = -1; y <= 1; ++y ) {
            overmap_buffer.get( city_om + point( x, y ) );
        }
    }

    std::vector<tripoint_abs_omt> tiles;
    for( int x = -radius; x <= radius; ++x ) {
        for( int y = -radius; y <= radius; ++y ) {
            tiles.emplace_back( center + point( x, y ) );
        }
    }

    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    for( const tripoint_abs_omt &p : tiles ) {
        tinymap tm;
        tm.load( p, false );
    }
    const std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();

    uint64_t maps = 1469598103934665603ULL;
    for( const tripoint_abs_omt &p : tiles ) {
        for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
            const tripoint_abs_sm corner = project_to<coords::sm>( tripoint_abs_omt( p.xy(), z ) );
            for( int dx = 0; dx < 2; ++dx ) {
                for( int dy = 0; dy < 2; ++dy ) {
                    const submap *sm = MAPBUFFER.lookup_submap( corner + point( dx, dy ) );
                    fingerprint_add( maps, sm == nullptr ? std::string( "-" ) : std::string( "+" ) );
                    if( sm != nullptr ) {
                        fingerprint_submap( maps, *sm );
                    }
                }
            }
        }
    }
    const unsigned int rng_after = rng_get_engine()();

    std::printf( "mapgen_benchmark: seed %d, %zu overmap tiles in %.1f ms = %.2f ms each; "
                 "maps %016llx, rng %08x\n", seed, tiles.size(),
                 std::chrono::duration<double, std::milli>( t1 - t0 ).count(),
                 std::chrono::duration<double, std::milli>( t1 - t0 ).count() / tiles.size(),
                 static_cast<unsigned long long>( maps ), rng_after );
}
