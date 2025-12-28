#include "json_helpers.h"
#include <string.h>

char *json_serialize(const cJSON *json) {
    if (!json) return NULL;
    return cJSON_PrintUnformatted(json);
}

cJSON *json_parse(const char *data, size_t len) {
    if (!data || len == 0) return NULL;

    /* cJSON_Parse expects null-terminated string */
    /* If data is already null-terminated within len, this is safe */
    cJSON *parsed = cJSON_Parse(data);
    if (!parsed) {
        /* Log error if needed */
        return NULL;
    }
    return parsed;
}

const char *json_get_string(const cJSON *obj, const char *key) {
    if (!obj || !key) return NULL;

    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsString(item)) return NULL;

    return item->valuestring;
}

int json_get_int(const cJSON *obj, const char *key, int default_val) {
    if (!obj || !key) return default_val;

    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsNumber(item)) return default_val;

    return item->valueint;
}

cJSON *json_get_object(const cJSON *obj, const char *key) {
    if (!obj || !key) return NULL;

    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsObject(item)) return NULL;

    return item;
}

cJSON *json_get_array(const cJSON *obj, const char *key) {
    if (!obj || !key) return NULL;

    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsArray(item)) return NULL;

    return item;
}

int json_get_bool(const cJSON *obj, const char *key, int default_val) {
    if (!obj || !key) return default_val;

    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsBool(item)) return default_val;

    return cJSON_IsTrue(item) ? 1 : 0;
}
