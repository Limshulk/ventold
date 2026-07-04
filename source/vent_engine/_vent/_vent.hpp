#pragma once
//
// vent public sdk.
// main include header.
// ——————————————————————
//
// includes all public sdk headers for client use.
// clients should only ever include this single header.

// --- base ---
#include <_vent/vent_sdk.hpp>

// --- accessors ---
#include <_vent/accessors.hpp>

// --- client registration ---
#include <_vent/client_registration.hpp>

// --- interfaces (ic_*) ---
#include <_vent/asset/ic_asset.hpp>
#include <_vent/event_bus/ic_event_bus.hpp>
#include <_vent/core/ic_system_registry.hpp>
#include <_vent/job/ic_job.hpp>
#include <_vent/log/ic_log.hpp>
#include <_vent/platform/ic_platform.hpp>
#include <_vent/platform/ic_window.hpp>
#include <_vent/renderer/ic_pipeline.hpp>
#include <_vent/renderer/ic_renderer.hpp>

// --- role interfaces (ir_*) ---
#include <_vent/core/ir_client.hpp>
#include <_vent/core/ir_dependencies.hpp>
#include <_vent/core/ir_runnable.hpp>

// --- types ---
#include <_vent/asset/shader.hpp>
#include <_vent/math/math.hpp>
#include <_vent/renderer/render_command.hpp>
#include <_vent/renderer/vertex.hpp>
#include <_vent/system/system_base.hpp>
#include <_vent/system/system_initialization_status.hpp>