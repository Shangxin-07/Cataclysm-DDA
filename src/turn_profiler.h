#pragma once
#ifndef CATA_SRC_TURN_PROFILER_H
#define CATA_SRC_TURN_PROFILER_H

#include <chrono>
#include <cstdio>

// Cheap wall-clock accounting for the phases of game::do_turn().
// Off unless the CATA_TURN_PROFILE environment variable is set; then each
// scope costs two steady_clock reads.
namespace turn_profiler
{
bool enabled();
void add( const char *phase, std::chrono::steady_clock::duration d );
void reset();
void report( std::FILE *out );

class scope
{
    public:
        explicit scope( const char *phase ) : phase_( phase ), on_( enabled() ) {
            if( on_ ) {
                start_ = std::chrono::steady_clock::now();
            }
        }
        ~scope() {
            if( on_ ) {
                add( phase_, std::chrono::steady_clock::now() - start_ );
            }
        }
        scope( const scope & ) = delete;
        scope &operator=( const scope & ) = delete;
    private:
        const char *phase_;
        bool on_;
        std::chrono::steady_clock::time_point start_;
};
} // namespace turn_profiler

#endif // CATA_SRC_TURN_PROFILER_H
