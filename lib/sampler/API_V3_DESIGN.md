# Sampler Library v3 Design - Merging v1 and v2

## Key Design Decisions

### From v1 (Original) ✅
1. **TAILQ Linked Lists** - Dynamic, no limits
2. **TSC-based Timing** - Efficient, automatic interval management
3. **Session Context in Callbacks** - More flexibility
4. **Simpler Process Loop** - Single call per session

### From v2 (Current) ✅
1. **Sample Structure** - Self-contained timestamp+name+id+value
2. **Batching Pattern** - begin/append/end for efficiency
3. **Filter Support** - Wildcard-based stat filtering
4. **Lifecycle Callbacks** - start/stop for initialization

### Improvements for v3 🎯
1. **Split into Multiple Files** - Better organization
2. **Remove Arbitrary Limits** - Use linked lists throughout
3. **Unified Timing** - TSC-based with clear semantics
4. **Global Session Management** - Track all sessions centrally

## File Structure

```
lib/sampler/
├── rte_sampler.h              # Main API header
├── rte_sampler.c              # Core implementation
├── rte_sampler_session.h      # Session management API
├── rte_sampler_session.c      # Session implementation  
├── rte_sampler_source.h       # Source API
├── rte_sampler_source.c       # Source implementation
├── rte_sampler_sink.h         # Sink API
├── rte_sampler_sink.c         # Sink implementation
├── rte_sampler_eventdev.h     # Eventdev source
├── rte_sampler_eventdev.c
└── rte_sampler_sink_*.{c,h}   # Various sinks
```

## Data Structures

```c
// Session with TAILQ
struct rte_sampler_session {
    TAILQ_ENTRY(rte_sampler_session) next;
    char name[RTE_SAMPLER_NAME_SIZE];
    
    // Timing (TSC-based like v1)
    uint64_t interval_tsc;
    uint64_t duration_tsc;
    uint64_t start_time_tsc;
    uint64_t next_sample_tsc;
    bool active;
    
    // Sources and sinks (linked lists like v1)
    TAILQ_HEAD(, rte_sampler_source) sources;
    TAILQ_HEAD(, rte_sampler_sink) sinks;
};

// Source with TAILQ
struct rte_sampler_source {
    TAILQ_ENTRY(rte_sampler_source) next;
    struct rte_sampler_session *session;  // Back pointer
    char name[RTE_SAMPLER_NAME_SIZE];
    struct rte_sampler_source_ops ops;
    void *user_data;
    
    // Filter support (from v2)
    char **filter_patterns;
    unsigned int num_filters;
};

// Sink with TAILQ
struct rte_sampler_sink {
    TAILQ_ENTRY(rte_sampler_sink) next;
    struct rte_sampler_session *session;  // Back pointer
    char name[RTE_SAMPLER_NAME_SIZE];
    struct rte_sampler_sink_ops ops;
    void *user_data;
};

// Keep v2's sample structure
struct rte_sampler_sample {
    uint64_t timestamp;
    char name[RTE_SAMPLER_NAME_SIZE];
    uint64_t id;
    uint64_t value;
};
```

## Callback Signatures

```c
// Source callbacks with session context
typedef int (*rte_sampler_source_start_t)(
    struct rte_sampler_source *source,
    struct rte_sampler_session *session);

typedef int (*rte_sampler_source_collect_t)(
    struct rte_sampler_source *source,
    struct rte_sampler_session *session,
    struct rte_sampler_sample *samples,
    unsigned int max_samples);

typedef int (*rte_sampler_source_stop_t)(
    struct rte_sampler_source *source,
    struct rte_sampler_session *session);

// Sink callbacks with session context
typedef int (*rte_sampler_sink_start_t)(
    struct rte_sampler_sink *sink,
    struct rte_sampler_session *session);

typedef int (*rte_sampler_sink_report_begin_t)(
    struct rte_sampler_sink *sink,
    struct rte_sampler_session *session,
    const char *source_name,
    unsigned int num_samples);

typedef int (*rte_sampler_sink_report_append_t)(
    struct rte_sampler_sink *sink,
    struct rte_sampler_session *session,
    const struct rte_sampler_sample *sample);

typedef int (*rte_sampler_sink_report_end_t)(
    struct rte_sampler_sink *sink,
    struct rte_sampler_session *session);

typedef int (*rte_sampler_sink_stop_t)(
    struct rte_sampler_sink *sink,
    struct rte_sampler_session *session);
```

## API Usage

```c
// Create session with interval/duration
struct rte_sampler_session *session = 
    rte_sampler_session_create("my_session", 1000000); // 1s interval

// Register source
struct rte_sampler_source *src = 
    rte_sampler_source_register(session, "eventdev0", &ops, user_data);

// Register sink
struct rte_sampler_sink *sink = 
    rte_sampler_sink_register(session, "console", &ops, user_data);

// Start session with duration
rte_sampler_session_start(session, 60000000); // 60s duration

// Process (automatic timing check)
while (running) {
    rte_sampler_session_process(session);  // Only samples when time
    rte_delay_ms(100);
}

// Stop and cleanup
rte_sampler_session_stop(session);
rte_sampler_session_free(session);
```

## Implementation Priority

1. ✅ Design complete
2. [ ] Implement core session management (rte_sampler_session.c)
3. [ ] Implement source management (rte_sampler_source.c)
4. [ ] Implement sink management (rte_sampler_sink.c)
5. [ ] Port eventdev source to new API
6. [ ] Port sinks to new API
7. [ ] Update examples
8. [ ] Testing and validation
