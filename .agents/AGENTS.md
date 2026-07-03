# Workspace Agent Rules

## Strict Path Casing
When editing or replacing content in files using absolute paths, you MUST use the exact case provided by the workspace configuration (e.g. `C:\` vs `c:\`). Ninja builds on Windows are strictly case-sensitive for drive letters and paths. Using mismatched casing in your tool calls will cause the build system to ignore file modifications.
