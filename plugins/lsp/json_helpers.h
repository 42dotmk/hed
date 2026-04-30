#ifndef JSON_HELPERS_H
#define JSON_HELPERS_H

#include "cjson/cJSON.h"
#include <stddef.h>

/* Helper functions for safe JSON operations */

/* Serialize cJSON object to string (caller must free) */
char *json_serialize(const cJSON *json);

/* Parse JSON string (returns NULL on error, caller must cJSON_Delete) */
cJSON *json_parse(const char *data, size_t len);

/* Safe string extraction (returns NULL if not found or not a string) */
const char *json_get_string(const cJSON *obj, const char *key);

/* Safe integer extraction (returns default_val if not found or not an int) */
int json_get_int(const cJSON *obj, const char *key, int default_val);

/* Safe object extraction (returns NULL if not found or not an object) */
cJSON *json_get_object(const cJSON *obj, const char *key);

/* Safe array extraction (returns NULL if not found or not an array) */
cJSON *json_get_array(const cJSON *obj, const char *key);

/* Safe boolean extraction (returns default_val if not found or not a bool) */
int json_get_bool(const cJSON *obj, const char *key, int default_val);

#endif /* JSON_HELPERS_H */
