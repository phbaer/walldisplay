#include "walldisplay/artwork_policy.hpp"
#include <cassert>
int main() {
    ArtworkPolicy policy;
    assert(policy.request("first"));
    auto first = policy.generation;
    assert(!policy.request("first"));
    policy.finish(first, false);
    assert(policy.request("first")); // failure is retryable
    first = policy.generation;
    assert(policy.request("second"));
    policy.finish(first, true); // stale completion cannot cache second
    assert(policy.pending && !policy.complete);
    policy.finish(policy.generation, true);
    assert(!policy.request("second"));
    assert(policy.request(""));
    policy.finish(first, true);
    assert(policy.complete && !policy.url[0]);
}
