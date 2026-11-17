#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MQTT topic structure of one panel, independent of the MQTT client.
 *
 * Every panel topic is "<base>/<suffix>". Inputs arrive as "set/..." (retained
 * configuration and display data) and "cmd/..." (non-retained commands); some
 * inputs also accept a legacy retained "state/..." alias. */

#define PANEL_FOOTER_BUTTON_COUNT 5
#define PANEL_MEDIA_FAVORITE_COUNT 5
#define PANEL_CHIP_COUNT 4
#define PANEL_GRID_BUTTON_COUNT 6

/** Return the suffix of topic below "<base>/", or NULL for a foreign topic. */
const char *panel_topic_suffix(const char *base, const char *topic);

/** Match "<prefix><N><tail>" for N in 1..count (count <= 9).
 * Stores the zero-based index on success. */
bool panel_topic_index(const char *suffix, const char *prefix, const char *tail, size_t count, size_t *index);

/** Match "set/<name>" or its legacy alias "state/<name>".
 * *canonical is true for the "set/" form, whose accepted value is echoed back
 * as retained state. */
bool panel_topic_input(const char *suffix, const char *name, bool *canonical);

/** As panel_topic_input() for "<prefix><N><tail>" below "set/" or "state/". */
bool panel_topic_input_index(const char *suffix, const char *prefix, const char *tail, size_t count,
                             size_t *index, bool *canonical);

typedef void (*panel_topic_visit_t)(const char *suffix, void *context);

/** Visit every subscription filter below the base topic: the "set/#" and
 * "cmd/#" wildcards and each legacy "state/..." alias. The panel does not
 * subscribe to "state/#", because it publishes its own state there. */
void panel_topics_for_each_subscription(panel_topic_visit_t visit, void *context);

#ifdef __cplusplus
}
#endif
