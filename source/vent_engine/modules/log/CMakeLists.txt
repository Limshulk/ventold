# vent build system.
# ——————————————————————
# the log module provides an async log system.
# output: sdk/lib/libvent_log.a

vent_create_module(
    NAME log
    SOURCES
        # vent sdk sources (task implementation)
        
        # log module sources
        src/log_system.cpp
    INTERNAL_DEPS
        core
)
