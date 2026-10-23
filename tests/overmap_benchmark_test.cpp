#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "cata_catch.h"
#include "coordinates.h"
#include "map_scale_constants.h"
#include "omdata.h"
#include "overmapbuffer.h"
#include "point.h"
#include "rng.h"
#include "type_id.h"

// Overmap generation throughput, for profiling.
// Run with: tests/cata_test "[overmap_benchmark]"
// Tune with env BENCH_SEED and BENCH_RADIUS (overmaps generated: (2r+1)^2).
// Prints a fingerprint of every generated terrain tile and of the RNG state
// after generation, so runs of two builds can be checked for identical maps.
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

TEST_CASE( "overmap_generation_throughput", "[.][overmap_benchmark]" )
{
    const int seed = env_int( "BENCH_SEED", 12345 );
    const int radius = env_int( "BENCH_RADIUS", 1 );

    rng_set_engine_seed( static_cast<unsigned int>( seed ) );
    overmap_buffer.clear();

    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    int generated = 0;
    for( int x = -radius; x <= radius; ++x ) {
        for( int y = -radius; y <= radius; ++y ) {
            overmap_buffer.get( point_abs_om( x, y ) );
            ++generated;
        }
    }
    const std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();

    uint64_t terrain = 1469598103934665603ULL;
    for( int x = -radius; x <= radius; ++x ) {
        for( int y = -radius; y <= radius; ++y ) {
            const point_abs_omt corner = project_to<coords::omt>( point_abs_om( x, y ) );
            for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
                for( int i = 0; i < OMAPX; ++i ) {
                    for( int j = 0; j < OMAPY; ++j ) {
                        const tripoint_abs_omt p( corner + point( i, j ), z );
                        fingerprint_add( terrain, overmap_buffer.ter( p ).id().str() );
                    }
                }
            }
        }
    }
    const unsigned int rng_after = rng_get_engine()();

    std::printf( "overmap_benchmark: seed %d, %d overmaps in %.1f ms = %.1f ms each; "
                 "terrain %016llx, rng %08x\n", seed, generated,
                 std::chrono::duration<double, std::milli>( t1 - t0 ).count(),
                 std::chrono::duration<double, std::milli>( t1 - t0 ).count() / generated,
                 static_cast<unsigned long long>( terrain ), rng_after );
}
