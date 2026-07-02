# vent build system.
# ——————————————————————
# the job module provides the work-stealing job system.
# output: sdk/lib/libvent_job.a

vent_create_module(
    NAME job
    SOURCES
        # vent sdk sources (task implementation)
        src/_vent/task.cpp
        
        # job module sources
        src/job_system.cpp
    INTERNAL_DEPS
        core
)
