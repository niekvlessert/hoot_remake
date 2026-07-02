#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum HootResult {
    HOOT_OK = 0,
    HOOT_ERROR_INVALID_ARGUMENT = 1,
    HOOT_ERROR_IO = 2,
    HOOT_ERROR_PARSE = 3,
    HOOT_ERROR_NOT_FOUND = 4,
    HOOT_ERROR_NOT_LOADED = 5,
    HOOT_ERROR_UNSUPPORTED = 6
} HootResult;

#ifdef __cplusplus
}
#endif
