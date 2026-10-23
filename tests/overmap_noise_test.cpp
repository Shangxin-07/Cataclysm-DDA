#include <initializer_list>
#include <string>

#include "cata_catch.h"
#include "coordinates.h"
#include "map_scale_constants.h"
#include "overmap_debug.h"
#include "overmap_noise.h"
#include "point.h"

TEST_CASE( "om_noise_layer_forest_export", "[.]" )
{
    const om_noise::om_noise_layer_forest f( point_abs_omt(), 1920237457 );
    om_debug::export_raw_noise( "forest-map-raw.pgm", f, OMAPX, OMAPY );
}

TEST_CASE( "om_noise_layer_floodplain_export", "[.]" )
{
    const om_noise::om_noise_layer_floodplain f( point_abs_omt(), 1920237457 );
    om_debug::export_raw_noise( "floodplain-map-raw.pgm", f, OMAPX, OMAPY );
}

TEST_CASE( "om_noise_layer_lake_export", "[.]" )
{
    const om_noise::om_noise_layer_lake f( point_abs_omt(), 1920237457 );
    om_debug::export_raw_noise( "lake-map-raw.pgm", f, OMAPX * 5, OMAPY * 5 );
    om_debug::export_interpreted_noise( "lake-map-interp.pgm", f, OMAPX * 5, OMAPY * 5, 0.25, false );
}

TEST_CASE( "om_noise_layer_lake_noise_exceeds_matches_noise_at", "[overmap][noise]" )
{
    // noise_exceeds() exits early from the octave sum; it must agree with noise_at() exactly.
    const unsigned seed = GENERATE( 0u, 1920237457u, 4000000000u );
    const om_noise::om_noise_layer_lake f( point_abs_omt( -3 * OMAPX, 2 * OMAPY ), seed );
    int mismatches = 0;
    for( const double threshold : {
             0.0, 0.05, 0.25, 0.5, 0.9
         } ) {
        for( int x = -5; x < OMAPX + 5; x++ ) {
            for( int y = -5; y < OMAPY + 5; y++ ) {
                const point_om_omt p( x, y );
                if( f.noise_exceeds( p, threshold ) != ( f.noise_at( p ) > threshold ) ) {
                    mismatches++;
                }
            }
        }
    }
    CHECK( mismatches == 0 );
}
