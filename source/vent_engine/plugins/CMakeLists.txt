# vent build system.
# ——————————————————————
# builds all engine plugins as shared libraries.
# each module outputs to sdk/plugins/libvent_<name>.so

vent_log("configuring plugins...")

add_subdirectory(vulkan_backend)

vent_log("plugins configured.")