
# Coding Conventions

All agents working in this workspace must adhere to the following coding conventions at all times. Failure to do so is unacceptable.

## 1. Comments
- EVERY comment must be fully lowercase.
- EVERY comment must end with a period.
- Comment liberally. Err on the side of too many comments rather than too few, as this is a learning project.
- Write comments as if the developer wrote them. They should serve as personal notes, reminders of past work, and api explanations.

## 2. Includes Sorting
Includes MUST be sorted by accessibility in the following order and be ordered alphabetically in each group, seperated with a blank line:
1. Local (module-private).
2. Semi-global (other modules).
3. Global (e.g., \_vent\ folders).
4. Externals (e.g., vulkan, glfw, etc.).
5. C/C++ standard library.
Exceptions of that are _vent/_vent.hpp as a general single-include for all client usage of the engine and _vent/vent_sdk.hpp for general sdk usage. They must be ordered as the very first includes.

## 3. Doxygen Comments
- EVERY function must have a doxygen-style comment block above it.
- Doxygen comments must also be fully lowercase and end with a period.

