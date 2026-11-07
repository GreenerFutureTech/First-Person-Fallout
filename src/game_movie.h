#ifndef GAME_MOVIE_H
#define GAME_MOVIE_H

#include <functional>

#include "db.h"

namespace fallout {

enum class GameMovieFlags : unsigned short {
    None = 0,
    FadeIn = 0x01,
    FadeOut = 0x02,
    StopMusic = 0x04,
    PauseMusic = 0x08,
};

constexpr inline GameMovieFlags operator&(GameMovieFlags lhs, GameMovieFlags rhs)
{
    return static_cast<GameMovieFlags>(static_cast<unsigned short>(lhs) & static_cast<unsigned short>(rhs));
}

constexpr inline GameMovieFlags operator|(GameMovieFlags lhs, GameMovieFlags rhs)
{
    return static_cast<GameMovieFlags>((static_cast<unsigned short>(lhs) | static_cast<unsigned short>(rhs)));
}

enum GameMovie : int {
    MOVIE_INVALID = -1,
    MOVIE_IPLOGO,
    MOVIE_INTRO,
    MOVIE_ELDER,
    MOVIE_VSUIT,
    MOVIE_AFAILED,
    MOVIE_ADESTROY,
    MOVIE_CAR,
    MOVIE_CARTUCCI,
    MOVIE_TIMEOUT,
    MOVIE_TANKER,
    MOVIE_ENCLAVE,
    MOVIE_DERRICK,
    MOVIE_ARTIMER1,
    MOVIE_ARTIMER2,
    MOVIE_ARTIMER3,
    MOVIE_ARTIMER4,
    MOVIE_CREDITS,
    MOVIE_COUNT,
    MOVIE_FIRST = MOVIE_IPLOGO
};

inline GameMovie operator++(GameMovie& e, int)
{
    GameMovie result = e;
    e = static_cast<GameMovie>(static_cast<int>(e) + 1);
    return result;
}

constexpr int GAME_MOVIE_MAX_COUNT = 32;

constexpr inline bool gameMovieIsValid(int movie)
{
    return movie >= MOVIE_FIRST && movie < MOVIE_COUNT;
}

constexpr inline bool gameMovieExtendedIsValid(int movie)
{
    return movie >= MOVIE_FIRST && movie < GAME_MOVIE_MAX_COUNT;
}

int gameMoviesInit();
void gameMoviesReset();
// The seen-ledger half of the reset, owned by f2_core (game_movie_state.cc).
void gameMoviesResetSeen();
int gameMoviesLoad(File* stream);
int gameMoviesSave(File* stream);
int gameMoviePlay(GameMovie movie, GameMovieFlags flags);
const char* gameMovieGetDefaultFileName(GameMovie movie);
bool gameMovieSetPath(GameMovie movie, const char* fileName);
void gameMovieMarkSeen(GameMovie movie);
void gameMovieFadeOut();
bool gameMovieIsSeen(GameMovie movie);
void gameMovieMarkSeen(GameMovie movie);
bool gameMovieIsPlaying();

// Raw seen-ledger bytes (MOVIE_COUNT of them) for the co-op WORLD-STATE sync: the
// server ships this to every viewer so a late joiner's local ledger matches the
// world's. Read-only view of the array gameMovieMarkSeen writes. See
// [[vault-suit-appearance-gap]] — a client that never saw the vault-suit movie
// derives its own dude + inventory art as tribal.
const unsigned char* gameMoviesSeenData();

// ---- The dedicated server's MOVIE BARRIER (game_movie_state.cc, f2_core) ----
//
// A movie is a story beat, so the world must not advance underneath it: the server
// parks its tick while viewers watch, exactly as vanilla freezes the world and
// exactly as the dialog barrier already does (game_dialog.cc gDialogServerPump).
//
// ►► RELEASE POLICY: THE FIRST ACK RELEASES EVERYONE (owner ruling 2026-07-20).
// Whoever skips first skips it for the room. Chosen for ROBUSTNESS, not laziness:
// the release condition is a single event rather than an N-way join, so there is no
// "who is still watching" bookkeeping, no timeout, and — the real reason — a viewer
// that DISCONNECTS mid-movie cannot wedge the server. The cost is social, not
// technical. Per-viewer skip (wait for all-acked-or-timeout) is the upgrade if the
// room ever minds.
//
// Install the pump to make the barrier live; with none installed gameMoviePlay
// never blocks, which is what keeps the headless goldens and the single-player
// client byte-identical.

// Install the server's service pump. Returning false BAILS the barrier (no viewers,
// quit requested, …) — the barrier must never outlive the reason it exists.
void gameMovieSetServerPump(std::function<bool()> pump);

// A viewer finished or skipped the movie. First one through releases the barrier.
void gameMovieAck();

// Block until an ack lands or the pump bails. No-op (returns immediately) when no
// pump is installed.
void gameMovieServerBarrier();

} // namespace fallout

#endif /* GAME_MOVIE_H */
