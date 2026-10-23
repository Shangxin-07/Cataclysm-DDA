#include <cmath>
#include <algorithm>

#include "overmap_noise.h"
#include "simplexnoise.h"

namespace om_noise
{

float om_noise_layer_forest::noise_at( const point_om_omt &local_omt_pos ) const
{
    const point_abs_omt p = global_omt_pos( local_omt_pos );
    float r = scaled_octave_noise_3d( 4, 0.5, 0.03, 0, 1, p.x(), p.y(), get_seed() );
    r = std::pow( r, 2.0f );

    float d = scaled_octave_noise_3d( 6, 0.5, 0.07, 0, 1, p.x(), p.y(), get_seed() );
    d = std::pow( d, 3.0f );

    return std::max( 0.0f, r - d * 0.5f );
}

float om_noise_layer_floodplain::noise_at( const point_om_omt &local_omt_pos ) const
{
    const point_abs_omt p = global_omt_pos( local_omt_pos );
    float r = scaled_octave_noise_3d( 4, 0.5, 0.05, 0, 1, p.x(), p.y(), get_seed() );
    r = std::pow( r, 2.0f );
    return r;
}

float om_noise_layer_lake::noise_at( const point_om_omt &local_omt_pos ) const
{
    const point_abs_omt p = global_omt_pos( local_omt_pos );
    float r = scaled_octave_noise_3d( 8, 0.5, 0.002, 0, 1, p.x(), p.y(), get_seed() );
    r = std::pow( r, 4.0f );
    return r;
}

bool om_noise_layer_lake::noise_exceeds( const point_om_omt &local_omt_pos,
        const double threshold ) const
{
    // Mirrors the octave sum in noise_at(). raw_noise_3d stays within [-1, 1] (sampled
    // maximum is about 0.98), so after each octave the final sum is bounded by the remaining
    // amplitudes. When the whole bound lands on one side of the threshold the answer is known.
    // Otherwise fall back to noise_at() itself, so the result is always identical.
    constexpr int octaves = 8;
    constexpr double persistence = 0.5;
    constexpr double max_amplitude = 1.9921875; // 1 + 0.5 + ... + 0.5^7
    // Widens the bound well beyond any float rounding in noise_at().
    constexpr double slack = 1e-4;
    const point_abs_omt p = global_omt_pos( local_omt_pos );
    const float x = p.x();
    const float y = p.y();
    const float z = get_seed();
    const auto lake_value = []( double octave_sum ) {
        const double r = octave_sum / max_amplitude / 2 + 0.5;
        return r * r * r * r;
    };
    double total = 0.0;
    double remaining = max_amplitude;
    float frequency = 0.002f;
    double amplitude = 1.0;
    for( int i = 0; i < octaves - 1; i++ ) {
        total += raw_noise_3d( x * frequency, y * frequency, z * frequency ) * amplitude;
        remaining -= amplitude;
        frequency *= 2;
        amplitude *= persistence;
        const double lo = total - remaining - slack;
        const double hi = total + remaining + slack;
        // (sum / max / 2 + 0.5)^4 is convex with its minimum at sum == -max_amplitude.
        if( std::max( lake_value( lo ), lake_value( hi ) ) < threshold ) {
            return false;
        }
        if( lo >= -max_amplitude && lake_value( lo ) > threshold ) {
            return true;
        }
    }
    return noise_at( local_omt_pos ) > threshold;
}

float om_noise_layer_ocean::noise_at( const point_om_omt &local_omt_pos ) const
{
    // this is a duplicate of lake noise.  Changing it might cause artifacts if oceans
    // and lakes intersect.
    const point_abs_omt p = global_omt_pos( local_omt_pos );
    float r = scaled_octave_noise_3d( 8, 0.5, 0.002, 0, 1, p.x(), p.y(), get_seed() );
    r = std::pow( r, 4.0f );
    return r;
}

} // namespace om_noise
