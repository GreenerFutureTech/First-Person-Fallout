#include "pipboy.h"
#include <algorithm>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "art.h"
#include "automap.h"
#include "color.h"
#include "combat.h"
#include "config.h"
#include "content_config.h"
#include "cycle.h"
#include "dbox.h"
#include "delay.h"
#include "draw.h"
#include "game_mouse.h"
#include "game_movie.h"
#include "game_sound.h"
#include "geometry.h"
#include "input.h"
#include "interface.h"
#include "kb.h"
#include "map.h"
#include "memory.h"
#include "message.h"
#include "mouse.h"
#include "object.h"
#include "platform_compat.h"
#include "queue.h"
#include "random.h"
#include "settings.h"
#include "sfall_script_hooks.h"
#include "svga.h"
#include "text_font.h"
#include "touch.h"
#include "window_manager.h"
#include "word_wrap.h"
#include "worldmap.h"
#include <string>
#include <vector>
#include "client_net.h" // clientViewerActive — a viewer does not own the clock
#include "rest.h" // restPerform — the shared rest simulation
#include "server_loop.h"
#include "sfall_config.h"
#include "rest.h"

#include "critter.h"
#include "debug.h"
#include "game.h"
#include "party_member.h"
#include "scripts.h"
#include "stat.h"

namespace fallout {

// ─── THE REST LOOP ────────────────────────────────────────────────────────────
// Lifted out of the pipboy's rest screen (pipboy.cc, f2_client) so the DEDICATED
// SERVER can drive it: resting is a simulation — it advances the game clock, accrues
// healing for the whole party, and lets queued events fire — and the authority is the
// only process allowed to do any of that.
//
// Before this existed, resting was dead in co-op three ways at once: the server's
// own `rest` verb hit an abort stub, a viewer was refused by vanilla's "You cannot
// rest at this location!" (correctly — a client must not advance the clock), and
// there was no wire verb to ask with. So nobody could pass time or heal on purpose,
// which also blocks every quest with a "come back in N days" gate.
//
// ►► THE SIM IS THE SHARED PART, and that is the whole point of the split. The
// per-frame heal accrual is divided by the same frame counts the CLOCK
// interpolation uses (restSimPacing), so a second implementation with its own
// pacing would heal a different amount for the same rest. One loop, two
// presentations: the pipboy passes a frame proc that draws and polls ESC; the server
// passes none and the loop runs as fast as the sim allows.
//
// The frame proc returning true means "the player stopped it" (ESC / quit) — the
// difference between that and a queued event firing is reported back, because the
// pipboy screen bails out of itself only on the event.

RestOutcome restPerform(int hours, int minutes, int duration, RestFrameProc* onFrame)
{
    RestOutcome outcome = kRestCompleted;

    if (duration == 0) {
        // Ledger H-40: rest-sim pacing (the animation frame counts both phases
        // divide their clock interpolation and heal accrual by).
        double minutesPhaseFrames;
        double hoursPhaseFrames;
        restSimPacing(hours, minutes, &minutesPhaseFrames, &hoursPhaseFrames);

        if (minutes != 0) {
            unsigned int gameTime = gameTimeGetTime();

            for (int frame = 0; frame < (int)minutesPhaseFrames; frame++) {
                if (outcome != kRestCompleted) {
                    break;
                }

                int tick = restSimMinutesTick(gameTime, frame, minutesPhaseFrames, minutes);
                if (tick == REST_SIM_TICK_EVENT) {
                    debugPrint("REST: Returning from Queue trigger...\n");
                    outcome = kRestInterrupted;
                    break;
                }

                if (tick == REST_SIM_TICK_QUIT) {
                    outcome = kRestAborted;
                    break;
                }

                if (onFrame != nullptr && onFrame()) {
                    outcome = kRestAborted;
                }
            }

            if (outcome == kRestCompleted) {
                // Ledger H-40: final clock snap + heal-cadence accrual.
                restSimMinutesFinish(gameTime, minutes);
            }
        }

        if (hours != 0 && outcome == kRestCompleted) {
            unsigned int gameTime = gameTimeGetTime();

            for (int frame = 0; frame < (int)hoursPhaseFrames; frame++) {
                if (outcome != kRestCompleted) {
                    break;
                }

                int tick = restSimHoursTick(gameTime, frame, hoursPhaseFrames, hours);
                if (tick == REST_SIM_TICK_EVENT) {
                    debugPrint("REST: Returning from Queue trigger...\n");
                    outcome = kRestInterrupted;
                    break;
                }

                if (tick == REST_SIM_TICK_QUIT) {
                    outcome = kRestAborted;
                    break;
                }

                if (onFrame != nullptr && onFrame()) {
                    outcome = kRestAborted;
                }
            }

            if (outcome == kRestCompleted) {
                restSimHoursFinish(gameTime, hours);
            }
        }
    } else if (duration == PIPBOY_REST_DURATION_UNTIL_HEALED
        || duration == PIPBOY_REST_DURATION_UNTIL_PARTY_HEALED) {
        int currentHp = critterGetHitPoints(gDude);
        int maxHp = critterGetStat(gDude, STAT_MAXIMUM_HIT_POINTS);
        if (currentHp == maxHp
            && !(duration == PIPBOY_REST_DURATION_UNTIL_PARTY_HEALED && partyIsAnyoneCanBeHealedByRest())) {
            return kRestCompleted; // nobody needs healing
        }

        // First pass — healing the dude is the top priority.
        // Ledger H-40: rest-until-healed duration math.
        int hoursToHeal = restUntilHealedDuration();
        while (outcome == kRestCompleted && hoursToHeal != 0) {
            if (hoursToHeal <= 24) {
                outcome = restPerform(hoursToHeal, 0, 0, onFrame);
                hoursToHeal = 0;
            } else {
                outcome = restPerform(24, 0, 0, onFrame);
                hoursToHeal -= 24;
            }
        }

        // Second pass — delayed damage to the dude (poison, radiation) and the
        // remaining party members, in 3 hour increments.
        int hpToHeal = critterGetStat(gDude, STAT_MAXIMUM_HIT_POINTS) - critterGetHitPoints(gDude);
        if (duration == PIPBOY_REST_DURATION_UNTIL_PARTY_HEALED) {
            int partyHpToHeal = partyGetMaxWoundToHealByRest();
            if (partyHpToHeal > hpToHeal) {
                hpToHeal = partyHpToHeal;
            }
        }

        while (outcome == kRestCompleted && hpToHeal != 0) {
            hpToHeal = critterGetStat(gDude, STAT_MAXIMUM_HIT_POINTS) - critterGetHitPoints(gDude);

            if (duration == PIPBOY_REST_DURATION_UNTIL_PARTY_HEALED) {
                int partyHpToHeal = partyGetMaxWoundToHealByRest();
                if (partyHpToHeal > hpToHeal) {
                    hpToHeal = partyHpToHeal;
                }
            }

            outcome = restPerform(3, 0, 0, onFrame);
        }
    }

    // Ledger H-40: end-of-rest overdue-queue-event flush.
    if (restSimOverdueEvents()) {
        debugPrint("REST: Returning from Queue trigger...\n");
        outcome = kRestInterrupted;
    }

    return outcome;
}

} // namespace fallout
