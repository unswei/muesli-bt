#pragma once

#include <string_view>

namespace muesli_bt::contract {

inline constexpr std::string_view kSchemaName = "mbt.evt.v1";

inline constexpr std::string_view kEventRunStart = "run_start";
inline constexpr std::string_view kEventRunEnd = "run_end";
inline constexpr std::string_view kEventEpisodeBegin = "episode_begin";
inline constexpr std::string_view kEventEpisodeEnd = "episode_end";
inline constexpr std::string_view kEventBtDef = "bt_def";
inline constexpr std::string_view kEventTickBegin = "tick_begin";
inline constexpr std::string_view kEventTickEnd = "tick_end";
inline constexpr std::string_view kEventTickAudit = "tick_audit";
inline constexpr std::string_view kEventGcBegin = "gc_begin";
inline constexpr std::string_view kEventGcEnd = "gc_end";
inline constexpr std::string_view kEventNodeEnter = "node_enter";
inline constexpr std::string_view kEventNodeExit = "node_exit";
inline constexpr std::string_view kEventNodeStatus = "node_status";
inline constexpr std::string_view kEventBudgetWarning = "budget_warning";
inline constexpr std::string_view kEventDeadlineExceeded = "deadline_exceeded";
inline constexpr std::string_view kEventPlannerCallStart = "planner_call_start";
inline constexpr std::string_view kEventPlannerCallEnd = "planner_call_end";
inline constexpr std::string_view kEventAsyncCancelRequested = "async_cancel_requested";
inline constexpr std::string_view kEventAsyncCancelAcknowledged = "async_cancel_acknowledged";
inline constexpr std::string_view kEventAsyncCompletionDropped = "async_completion_dropped";

}  // namespace muesli_bt::contract
