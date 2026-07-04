# vent build system.
# ——————————————————————
# includes all vent applications. each app uses vent_create_client() to
# create a complete application with launcher, engine, and client libraries.

vent_log("configuring applications...")

# --- aggregate applications ---
# this target is used by VENT_APPS to build all applications.

add_custom_target(vent_apps COMMENT "building all vent applications")

# --- individual applications ---

add_subdirectory(minimal)

vent_log("applications configured.")
