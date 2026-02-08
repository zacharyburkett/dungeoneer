#include "dungeoneer/dungeoneer.h"

const char *dg_status_string(dg_status_t status)
{
    switch (status) {
    case DG_STATUS_OK:
        return "ok";
    case DG_STATUS_INVALID_ARGUMENT:
        return "invalid argument";
    case DG_STATUS_ALLOCATION_FAILED:
        return "allocation failed";
    case DG_STATUS_GENERATION_FAILED:
        return "generation failed";
    case DG_STATUS_IO_ERROR:
        return "io error";
    case DG_STATUS_UNSUPPORTED_FORMAT:
        return "unsupported format";
    default:
        return "unknown status";
    }
}
