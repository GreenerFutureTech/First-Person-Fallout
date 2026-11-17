#ifndef FALLOUT_SFALL_METARULES_H_
#define FALLOUT_SFALL_METARULES_H_

#include "opcode_context.h"

#include <functional>

namespace fallout {

typedef void(MetaruleHandler)(OpcodeContext& ctx);

// The type of argument, not the same as actual data type. Useful for validation.
enum OpcodeArgumentType {
    ARG_ANY = 0, // no validation (default)
    ARG_INT, // integer only
    ARG_OBJECT, // non-null pointer/object
    ARG_STRING, // string only
    ARG_INTSTR, // integer OR string
    ARG_NUMBER, // float OR integer
};

struct MetaruleInfo {
    const char* name;
    MetaruleHandler* handler;
    int minArgs;
    int maxArgs;
    int errorReturn;
    OpcodeArgumentType argumentTypes[METARULE_MAX_ARGS];
};

extern const MetaruleInfo kMetarules[];
extern const std::size_t kMetarulesCount;

class Program;

void sfall_metarule(Program* program, int args);
void mf_string_format(OpcodeContext& ctx);

// Dedicated-server bridge for sfall `message_box`; the pump services viewer
// answers while the simulation is parked. Without a pump, use the local dialog
// path (or the headless stub's default answer).
void sfallMessageBoxSetServerPump(std::function<bool()> pump);
bool sfallMessageBoxAnswer(int promptId, int result);

} // namespace fallout

#endif /* FALLOUT_SFALL_METARULES_H_ */
