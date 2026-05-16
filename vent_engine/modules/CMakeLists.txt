# vent build system.
# ——————————————————————
# builds all engine modules as static libraries.
# each module outputs to sdk/lib/libvent_<name>.a

vent_log("configuring modules...")

add_subdirectory(core)
#add_subdirectory(log)
#add_subdirectory(job)
#add_subdirectory(platform)
#add_subdirectory(renderer)

vent_log("modules configured.")
