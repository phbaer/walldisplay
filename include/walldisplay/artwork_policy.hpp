#pragma once
#include <cstdint>
#include <cstring>
/* Caller serializes access. Failed requests remain retryable; stale completions
 * never commit after newer intent (including clearing artwork). */
struct ArtworkPolicy {
    char url[384]{};
    uint32_t generation = 0;
    bool pending = false;
    bool complete = false;
    bool request(const char *next) {
        if (!std::strcmp(url, next) && (pending || complete)) return false;
        std::strcpy(url, next);
        ++generation;
        pending = next[0] != 0;
        complete = !pending;
        return true;
    }
    void finish(uint32_t id, bool success) {
        if (id == generation) { pending = false; complete = success; }
    }
};
