#include "turn_profiler.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <ratio>

namespace
{
struct phase_total {
    const char *name = nullptr;
    std::chrono::steady_clock::duration total{};
    int calls = 0;
};

// Phase names are string literals, so pointer identity is enough.
std::array<phase_total, 64> phases;
size_t phase_count = 0;
} // namespace

bool turn_profiler::enabled()
{
    static const bool on = std::getenv( "CATA_TURN_PROFILE" ) != nullptr;
    return on;
}

void turn_profiler::add( const char *phase, std::chrono::steady_clock::duration d )
{
    for( size_t i = 0; i < phase_count; ++i ) {
        if( phases[i].name == phase ) {
            phases[i].total += d;
            ++phases[i].calls;
            return;
        }
    }
    if( phase_count < phases.size() ) {
        phases[phase_count++] = { phase, d, 1 };
    }
}

void turn_profiler::reset()
{
    phase_count = 0;
}

void turn_profiler::report( std::FILE *out )
{
    std::sort( phases.begin(), phases.begin() + phase_count,
    []( const phase_total & a, const phase_total & b ) {
        return a.total > b.total;
    } );
    for( size_t i = 0; i < phase_count; ++i ) {
        const double ms = std::chrono::duration<double, std::milli>( phases[i].total ).count();
        ( void )std::fprintf( out, "  %-32s %10.1f ms %8d calls %9.3f ms/call\n", phases[i].name, ms,
                              phases[i].calls, ms / phases[i].calls );
    }
}
