#ifndef CHRONOKIT_C_API_H
#define CHRONOKIT_C_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque handle to a ChronoKit AppointmentEngine instance.
 * Enables seamless FFI embedding into Python (ctypes), Go (cgo), Rust, Node.js, or C#.
 */
typedef struct chronokit_engine chronokit_engine_t;

/* Lifecycle: pass NULL or "" for in-memory storage, or a file path for WAL SQLite3 storage */
chronokit_engine_t* chronokit_create(const char* sqlite_db_path);
void chronokit_destroy(chronokit_engine_t* eng);

/* Load a Lua 5.4 Blueprint file or hot-reload inline Lua code (returns 0 on success, non-zero on error) */
int chronokit_load_blueprint_file(chronokit_engine_t* eng, const char* path, int load_seed_data);
int chronokit_hot_reload_lua(chronokit_engine_t* eng, const char* lua_code, int reset_and_seed);

/* JSON-in / JSON-out operations (caller must free returned char* with chronokit_free_string) */
char* chronokit_book_json(chronokit_engine_t* eng, const char* request_json);
char* chronokit_simulate_json(chronokit_engine_t* eng, const char* request_json);
char* chronokit_cancel_json(chronokit_engine_t* eng, const char* booking_id, const char* actor_id, const char* reason);
char* chronokit_draw_lottery_json(chronokit_engine_t* eng, const char* resource_id_filter, uint64_t seed);
char* chronokit_set_env_json(chronokit_engine_t* eng, const char* env_json);
char* chronokit_stats_json(chronokit_engine_t* eng);
char* chronokit_export_ics(chronokit_engine_t* eng, const char* booking_id);
const char* chronokit_last_error(chronokit_engine_t* eng);

void chronokit_free_string(char* str);

#ifdef __cplusplus
}
#endif

#endif /* CHRONOKIT_C_API_H */
