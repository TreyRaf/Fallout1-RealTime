#ifndef FALLOUT_INT_SUPPORT_INTEXTRA_H_
#define FALLOUT_INT_SUPPORT_INTEXTRA_H_

#include "game/object_types.h"
#include "int/intrpret.h"

namespace fallout {

typedef enum ScriptError {
    SCRIPT_ERROR_NOT_IMPLEMENTED,
    SCRIPT_ERROR_OBJECT_IS_NULL,
    SCRIPT_ERROR_CANT_MATCH_PROGRAM_TO_SID,
    SCRIPT_ERROR_FOLLOWS,
    SCRIPT_ERROR_COUNT,
} ScriptError;

void dbg_error(Program* program, const char* name, int error);
int correctDeath(Object* critter, int anim, bool a3);
void intExtraClose();
void initIntExtra();
void updateIntExtra();
void intExtraRemoveProgramReferences(Program* program);

// RT: called before the engine runs a script procedure (see `op_party_add`).
void intextra_rt_begin_procedure(Program* program);
int intextra_rt_debug_party_add(Object* object, int variable, int value);

} // namespace fallout

#endif /* FALLOUT_INT_SUPPORT_INTEXTRA_H_ */
