#pragma once
//
// vent public sdk.
// main include header.
// ——————————————————————
//
// includes all public sdk headers for client use.
// clients should only ever include this single header.

// --- base types and platform ---
#include <_vent/vent_sdk.hpp>

// --- system access ---
#include <_vent/accessors.hpp>

// --- client interfaces (ic_*) ---
#include <_vent/core/ic_system_registry.hpp>
#include <_vent/job/ic_job.hpp>
#include <_vent/core/ic_log.hpp>
#include <_vent/core/ic_event_bus.hpp>
#include <_vent/platform/ic_platform.hpp>
#include <_vent/platform/ic_window.hpp>

// --- role interfaces (ir_*) ---
#include <_vent/core/ir_runnable.hpp>
#include <_vent/core/ir_client.hpp>
#include <_vent/core/ir_dependencies.hpp>

// --- client registration ---
#include <_vent/client_registration.hpp>