#include "bt/approach_pose_validator.hpp"
#include "bt/compiler.hpp"
#include "bt/runtime.hpp"
#include "bt/runtime_host.hpp"
#include "delayed_fake_service.hpp"
#if defined(MUESLI_BT_HAVE_BOOSTER_BRIDGE)
#include "booster/bridge_client.hpp"
#endif
#include "muslisp/gc.hpp"
#include "muslisp/printer.hpp"
#include "muslisp/reader.hpp"
#include "muslisp/value.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace
{

using namespace std::chrono_literals;

struct options
{
  std::filesystem::path tree_path;
  std::filesystem::path event_path;
  std::string run_id;
  std::string trial_id;
  std::string acceptance_policy;
  std::string intervention;
  std::string git_sha = "unknown";
  std::string platform = "sdk-independent";
  std::string backend_name = "humanoid-delayed-fake";
  std::string model_version = "deterministic-v1";
  std::string instruction = "choose a bounded approach pose relative to the observed ball";
  std::int64_t delay_ms = 2500;
  std::int64_t deadline_ms = 3500;
  std::int64_t intervention_ms = 1000;
  std::uint64_t seed = 424242;
  double tick_hz = 20.0;
  double pose_x_m = -0.45;
  double pose_y_m = 0.08;
  double pose_yaw_rad = 0.0;
  std::int64_t request_dims = 3;
  double request_bound_lo = -1.0;
  double request_bound_hi = 1.0;
  double request_max_delta = 10.0;
  std::string observation_frame = "field";
  std::string action_frame = "ball_context";
  double min_x_m = -1.0;
  double max_x_m = 1.0;
  double min_y_m = -1.0;
  double max_y_m = 1.0;
  double min_yaw_rad = -3.141593;
  double max_yaw_rad = 3.141593;
  std::string initial_context_id = "ball-0001";
  std::string moved_context_id = "ball-0002";
  std::vector<double> initial_ball_position_m{1.20, -0.35, 0.0};
  std::vector<double> moved_ball_position_m{0.75, 0.40, 0.0};
  double context_change_threshold_m = 0.15;
  bool physical_motion_enabled = false;
  bool unsafe_simulation_stale_dispatch = false;
  std::string booster_bridge_socket;
  std::int64_t booster_bridge_timeout_ms = 100;
};

[[noreturn]] void fail(std::string message)
{
  throw std::runtime_error(std::move(message));
}

std::string require_value(int argc, char** argv, int& index, std::string_view option)
{
  if (index + 1 >= argc)
  {
    fail(std::string(option) + " requires a value");
  }
  return argv[++index];
}

bool parse_bool(std::string_view value, std::string_view option)
{
  if (value == "true")
  {
    return true;
  }
  if (value == "false")
  {
    return false;
  }
  fail(std::string(option) + " expects true or false");
}

std::int64_t parse_integer(std::string value, std::string_view option)
{
  std::size_t consumed = 0;
  try
  {
    const std::int64_t parsed = std::stoll(value, &consumed);
    if (consumed == value.size())
    {
      return parsed;
    }
  }
  catch (const std::exception&)
  {
  }
  fail(std::string(option) + " expects a complete signed integer");
}

std::uint64_t parse_unsigned_integer(std::string value, std::string_view option)
{
  std::size_t consumed = 0;
  try
  {
    const std::uint64_t parsed = std::stoull(value, &consumed);
    if (consumed == value.size() && !value.starts_with('-'))
    {
      return parsed;
    }
  }
  catch (const std::exception&)
  {
  }
  fail(std::string(option) + " expects a complete unsigned integer");
}

double parse_number(std::string value, std::string_view option)
{
  std::size_t consumed = 0;
  try
  {
    const double parsed = std::stod(value, &consumed);
    if (consumed == value.size())
    {
      return parsed;
    }
  }
  catch (const std::exception&)
  {
  }
  fail(std::string(option) + " expects a complete number");
}

options parse_options(int argc, char** argv)
{
  options out;
  for (int i = 1; i < argc; ++i)
  {
    const std::string_view arg = argv[i];
    if (arg == "--tree")
    {
      out.tree_path = require_value(argc, argv, i, arg);
    }
    else if (arg == "--events")
    {
      out.event_path = require_value(argc, argv, i, arg);
    }
    else if (arg == "--run-id")
    {
      out.run_id = require_value(argc, argv, i, arg);
    }
    else if (arg == "--trial-id")
    {
      out.trial_id = require_value(argc, argv, i, arg);
    }
    else if (arg == "--acceptance-policy")
    {
      out.acceptance_policy = require_value(argc, argv, i, arg);
    }
    else if (arg == "--intervention")
    {
      out.intervention = require_value(argc, argv, i, arg);
    }
    else if (arg == "--git-sha")
    {
      out.git_sha = require_value(argc, argv, i, arg);
    }
    else if (arg == "--platform")
    {
      out.platform = require_value(argc, argv, i, arg);
    }
    else if (arg == "--backend-name")
    {
      out.backend_name = require_value(argc, argv, i, arg);
    }
    else if (arg == "--model-version")
    {
      out.model_version = require_value(argc, argv, i, arg);
    }
    else if (arg == "--instruction")
    {
      out.instruction = require_value(argc, argv, i, arg);
    }
    else if (arg == "--delay-ms")
    {
      out.delay_ms = parse_integer(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--deadline-ms")
    {
      out.deadline_ms = parse_integer(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--intervention-ms")
    {
      out.intervention_ms = parse_integer(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--seed")
    {
      out.seed = parse_unsigned_integer(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--tick-hz")
    {
      out.tick_hz = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--pose-x-m")
    {
      out.pose_x_m = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--pose-y-m")
    {
      out.pose_y_m = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--pose-yaw-rad")
    {
      out.pose_yaw_rad = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--request-dims")
    {
      out.request_dims = parse_integer(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--request-bound-lo")
    {
      out.request_bound_lo = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--request-bound-hi")
    {
      out.request_bound_hi = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--request-max-delta")
    {
      out.request_max_delta = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--action-frame")
    {
      out.action_frame = require_value(argc, argv, i, arg);
    }
    else if (arg == "--observation-frame")
    {
      out.observation_frame = require_value(argc, argv, i, arg);
    }
    else if (arg == "--min-x-m")
    {
      out.min_x_m = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--max-x-m")
    {
      out.max_x_m = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--min-y-m")
    {
      out.min_y_m = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--max-y-m")
    {
      out.max_y_m = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--min-yaw-rad")
    {
      out.min_yaw_rad = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--max-yaw-rad")
    {
      out.max_yaw_rad = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--initial-context-id")
    {
      out.initial_context_id = require_value(argc, argv, i, arg);
    }
    else if (arg == "--moved-context-id")
    {
      out.moved_context_id = require_value(argc, argv, i, arg);
    }
    else if (arg == "--initial-ball-x-m")
    {
      out.initial_ball_position_m[0] = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--initial-ball-y-m")
    {
      out.initial_ball_position_m[1] = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--initial-ball-z-m")
    {
      out.initial_ball_position_m[2] = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--moved-ball-x-m")
    {
      out.moved_ball_position_m[0] = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--moved-ball-y-m")
    {
      out.moved_ball_position_m[1] = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--moved-ball-z-m")
    {
      out.moved_ball_position_m[2] = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--context-change-threshold-m")
    {
      out.context_change_threshold_m = parse_number(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--physical-motion-enabled")
    {
      out.physical_motion_enabled = parse_bool(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--unsafe-simulation-stale-dispatch")
    {
      out.unsafe_simulation_stale_dispatch =
          parse_bool(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--booster-bridge-socket")
    {
      out.booster_bridge_socket = require_value(argc, argv, i, arg);
    }
    else if (arg == "--booster-bridge-timeout-ms")
    {
      out.booster_bridge_timeout_ms = parse_integer(require_value(argc, argv, i, arg), arg);
    }
    else if (arg == "--help")
    {
      std::cout << "usage: humanoid_model_mediated_trial --tree FILE --events FILE --run-id ID "
                   "--trial-id ID --acceptance-policy deadline_only|invocation_scoped "
                   "--intervention none|moved_ball|emergency "
                   "[--unsafe-simulation-stale-dispatch true|false] [options]\n";
      std::exit(0);
    }
    else
    {
      fail("unknown option: " + std::string(arg));
    }
  }

  if (out.tree_path.empty() || out.event_path.empty() || out.run_id.empty() || out.trial_id.empty())
  {
    fail("--tree, --events, --run-id and --trial-id are required");
  }
  if (out.acceptance_policy != "deadline_only" && out.acceptance_policy != "invocation_scoped")
  {
    fail("--acceptance-policy must be deadline_only or invocation_scoped");
  }
  if (out.intervention != "none" && out.intervention != "moved_ball" &&
      out.intervention != "emergency")
  {
    fail("--intervention must be none, moved_ball or emergency");
  }
  if (out.unsafe_simulation_stale_dispatch &&
      (out.trial_id != "T2a" || out.acceptance_policy != "deadline_only" ||
       out.intervention != "moved_ball" || out.platform != "booster-studio-sim_x86_64" ||
       out.booster_bridge_socket.empty() || !out.physical_motion_enabled))
  {
    fail("unsafe stale dispatch is restricted to motion-enabled T2a in Booster Studio simulation");
  }
  if (out.delay_ms <= 0 || out.deadline_ms <= 0 || out.intervention_ms < 0 ||
      out.booster_bridge_timeout_ms <= 0 ||
      !std::isfinite(out.tick_hz) || out.tick_hz <= 0.0)
  {
    fail("delay, deadline, intervention time and tick rate must be valid positive values");
  }
  if (out.intervention != "none" && out.intervention_ms >= out.delay_ms)
  {
    fail("intervention time must precede delayed completion");
  }
  if (!std::isfinite(out.pose_x_m) || !std::isfinite(out.pose_y_m) ||
      !std::isfinite(out.pose_yaw_rad) || !std::isfinite(out.request_bound_lo) ||
      !std::isfinite(out.request_bound_hi) || !std::isfinite(out.request_max_delta) ||
      !std::isfinite(out.min_x_m) ||
      !std::isfinite(out.max_x_m) || !std::isfinite(out.min_y_m) ||
      !std::isfinite(out.max_y_m) || !std::isfinite(out.min_yaw_rad) ||
      !std::isfinite(out.max_yaw_rad) || !std::isfinite(out.context_change_threshold_m) ||
      !std::all_of(out.initial_ball_position_m.begin(), out.initial_ball_position_m.end(),
                   [](double value) { return std::isfinite(value); }) ||
      !std::all_of(out.moved_ball_position_m.begin(), out.moved_ball_position_m.end(),
                   [](double value) { return std::isfinite(value); }))
  {
    fail("pose, bounds and perception configuration must contain finite values");
  }
  if (out.observation_frame.empty() || out.action_frame.empty() || out.initial_context_id.empty() ||
      out.moved_context_id.empty() || out.backend_name.empty() || out.model_version.empty() ||
      out.instruction.empty() || out.request_dims != 3 ||
      out.request_bound_lo > out.request_bound_hi ||
      out.request_max_delta <= 0.0 || out.min_x_m > out.max_x_m || out.min_y_m > out.max_y_m ||
      out.min_yaw_rad > out.max_yaw_rad || out.context_change_threshold_m < 0.0)
  {
    fail("frame, context, bounds and context-change threshold must be valid");
  }
  if (out.pose_x_m < out.min_x_m || out.pose_x_m > out.max_x_m ||
      out.pose_y_m < out.min_y_m || out.pose_y_m > out.max_y_m ||
      out.pose_yaw_rad < out.min_yaw_rad || out.pose_yaw_rad > out.max_yaw_rad ||
      out.pose_x_m < out.request_bound_lo || out.pose_x_m > out.request_bound_hi ||
      out.pose_y_m < out.request_bound_lo || out.pose_y_m > out.request_bound_hi ||
      out.pose_yaw_rad < out.request_bound_lo || out.pose_yaw_rad > out.request_bound_hi)
  {
    fail("fake-service approach pose is outside the configured request or host bounds");
  }
  if (out.intervention == "moved_ball")
  {
    const double dx = out.moved_ball_position_m[0] - out.initial_ball_position_m[0];
    const double dy = out.moved_ball_position_m[1] - out.initial_ball_position_m[1];
    const double dz = out.moved_ball_position_m[2] - out.initial_ball_position_m[2];
    if (out.initial_context_id == out.moved_context_id ||
        std::sqrt(dx * dx + dy * dy + dz * dz) <= out.context_change_threshold_m)
    {
      fail("moved-ball intervention must cross the context threshold and change context ID");
    }
  }
  if (out.physical_motion_enabled && out.booster_bridge_socket.empty())
  {
    fail("physical motion requires --booster-bridge-socket and an explicitly enabled Booster "
         "host adapter");
  }
  if (!out.booster_bridge_socket.empty() &&
      !std::filesystem::path(out.booster_bridge_socket).is_absolute())
  {
    fail("--booster-bridge-socket must be an absolute path");
  }
#if !defined(MUESLI_BT_HAVE_BOOSTER_BRIDGE)
  if (!out.booster_bridge_socket.empty())
  {
    fail("this runner was built without MUESLI_BT_BUILD_INTEGRATION_BOOSTER");
  }
#endif
  if (std::filesystem::exists(out.event_path))
  {
    fail("event file already exists: " + out.event_path.string());
  }
  return out;
}

std::string read_text(const std::filesystem::path& path)
{
  std::ifstream input(path);
  if (!input)
  {
    fail("failed to open BT source: " + path.string());
  }
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}

bt::definition load_defbt(std::string_view source)
{
  std::vector<muslisp::value> expressions = muslisp::read_all(source);
  muslisp::gc_root_scope roots(muslisp::default_gc());
  for (muslisp::value& expression : expressions)
  {
    roots.add(&expression);
  }
  if (expressions.size() != 1 || !muslisp::is_proper_list(expressions.front()))
  {
    fail("BT source must contain exactly one defbt form");
  }
  const std::vector<muslisp::value> form = muslisp::vector_from_list(expressions.front());
  if (form.size() != 3 || !muslisp::is_symbol(form[0]) ||
      muslisp::symbol_name(form[0]) != "defbt" || !muslisp::is_symbol(form[1]))
  {
    fail("BT source must have the form (defbt name tree)");
  }
  bt::definition definition = bt::compile_definition(form[2]);
  definition.source_hash = bt::event_log::hash64_hex(source);
  definition.canonical_dsl = muslisp::write_value(form[2]);
  definition.canonical_dsl_hash = bt::event_log::hash64_hex(definition.canonical_dsl);
  return definition;
}

std::string normalise_option_key(std::string key)
{
  if (!key.empty() && key.front() == ':')
  {
    key.erase(key.begin());
  }
  std::replace(key.begin(), key.end(), '-', '_');
  return key;
}

const bt::arg_value& require_request_option(const bt::node& request, std::string_view name)
{
  const bt::arg_value* found = nullptr;
  if ((request.args.size() % 2u) != 0u)
  {
    fail("compiled vla-request has an incomplete option pair");
  }
  for (std::size_t index = 0; index < request.args.size(); index += 2)
  {
    const bt::arg_value& key = request.args[index];
    if ((key.kind != bt::arg_kind::symbol && key.kind != bt::arg_kind::string) ||
        normalise_option_key(key.text) != name)
    {
      continue;
    }
    if (found)
    {
      fail("compiled vla-request repeats option :" + std::string(name));
    }
    found = &request.args[index + 1];
  }
  if (!found)
  {
    fail("compiled vla-request is missing option :" + std::string(name));
  }
  return *found;
}

std::string request_option_text(const bt::node& request, std::string_view name)
{
  const bt::arg_value& value = require_request_option(request, name);
  if (value.kind != bt::arg_kind::symbol && value.kind != bt::arg_kind::string)
  {
    fail("compiled vla-request option :" + std::string(name) + " must be text");
  }
  return value.text;
}

std::int64_t request_option_integer(const bt::node& request, std::string_view name)
{
  const bt::arg_value& value = require_request_option(request, name);
  if (value.kind != bt::arg_kind::integer)
  {
    fail("compiled vla-request option :" + std::string(name) + " must be an integer");
  }
  return value.int_v;
}

double request_option_number(const bt::node& request, std::string_view name)
{
  const bt::arg_value& value = require_request_option(request, name);
  if (value.kind == bt::arg_kind::integer)
  {
    return static_cast<double>(value.int_v);
  }
  if (value.kind == bt::arg_kind::floating)
  {
    return value.float_v;
  }
  fail("compiled vla-request option :" + std::string(name) + " must be numeric");
}

const bt::node& require_single_request_node(const bt::definition& definition)
{
  const bt::node* request = nullptr;
  for (const bt::node& node : definition.nodes)
  {
    if (node.kind != bt::node_kind::vla_request)
    {
      continue;
    }
    if (request)
    {
      fail("experiment BT must contain exactly one vla-request");
    }
    request = &node;
  }
  if (!request)
  {
    fail("experiment BT must contain exactly one vla-request");
  }
  return *request;
}

std::set<std::string> request_option_names(const bt::node& request)
{
  if ((request.args.size() % 2u) != 0u)
  {
    fail("compiled vla-request has an incomplete option pair");
  }
  std::set<std::string> names;
  for (std::size_t index = 0; index < request.args.size(); index += 2)
  {
    const bt::arg_value& key = request.args[index];
    if (key.kind != bt::arg_kind::symbol && key.kind != bt::arg_kind::string)
    {
      fail("compiled vla-request option key must be text");
    }
    if (!names.insert(normalise_option_key(key.text)).second)
    {
      fail("compiled vla-request repeats an option");
    }
  }
  return names;
}

void validate_request_contract(const bt::definition& definition, const options& opts)
{
  const bt::node& request = require_single_request_node(definition);
  const std::set<std::string> expected_options{
      "acceptance_policy", "action_frame", "bound_hi",     "bound_lo",
      "context_key",       "deadline_ms",  "dims",         "frame_id",
      "instruction",       "job_key",      "max_delta",    "model_name",
      "model_version",     "name",         "seed",         "state_key",
  };
  if (request_option_names(request) != expected_options)
  {
    fail("compiled vla-request option set does not match the frozen experiment contract");
  }
  if (request_option_text(request, "name") != "approach-pose" ||
      request_option_text(request, "job_key") != "approach-job" ||
      request_option_text(request, "state_key") != "ball-state" ||
      request_option_text(request, "context_key") != "ball-context-id" ||
      request_option_text(request, "instruction") != opts.instruction)
  {
    fail("compiled vla-request identity or blackboard keys do not match the experiment contract");
  }
  if (request_option_text(request, "model_name") != opts.backend_name ||
      request_option_text(request, "model_version") != opts.model_version)
  {
    fail("compiled vla-request model does not match the configured fake service");
  }
  if (request_option_text(request, "acceptance_policy") != opts.acceptance_policy ||
      request_option_text(request, "frame_id") != opts.observation_frame ||
      request_option_text(request, "action_frame") != opts.action_frame)
  {
    fail("compiled vla-request policy or frames do not match the experiment configuration");
  }
  const std::int64_t request_seed = request_option_integer(request, "seed");
  if (request_option_integer(request, "deadline_ms") != opts.deadline_ms || request_seed < 0 ||
      static_cast<std::uint64_t>(request_seed) != opts.seed ||
      request_option_integer(request, "dims") != opts.request_dims ||
      request_option_number(request, "bound_lo") != opts.request_bound_lo ||
      request_option_number(request, "bound_hi") != opts.request_bound_hi ||
      request_option_number(request, "max_delta") != opts.request_max_delta)
  {
    fail("compiled vla-request timing, seed or action-space parameters do not match configuration");
  }
}

struct experiment_host_state
{
  std::string ball_context_id;
  std::vector<double> ball_position_m;
  bool ball_available = true;
  bool robot_stable = true;
  bool emergency = false;
  bool bridge_available = false;
  std::string bridge_fault_reason = "not_configured";
};

#if defined(MUESLI_BT_HAVE_BOOSTER_BRIDGE)
void apply_bridge_snapshot(experiment_host_state& state,
                           const muesli_bt::booster::bridge_snapshot& snapshot)
{
  state.ball_context_id = snapshot.ball_context_id;
  state.ball_available = snapshot.ball_available && snapshot.ball_position_m.has_value();
  if (snapshot.ball_position_m)
  {
    state.ball_position_m.assign(snapshot.ball_position_m->begin(),
                                 snapshot.ball_position_m->end());
  }
  state.emergency = snapshot.emergency;
  state.robot_stable = snapshot.robot_stable && !snapshot.emergency;
  state.bridge_available = true;
  state.bridge_fault_reason.clear();
}

void fail_closed_bridge_state(experiment_host_state& state, std::string reason)
{
  state.ball_available = false;
  state.robot_stable = false;
  state.emergency = true;
  state.bridge_available = false;
  state.bridge_fault_reason = std::move(reason);
}
#endif

class deadline_only_baseline_validator final : public bt::vla_commit_validator
{
public:
  deadline_only_baseline_validator(experiment_host_state& host_state, const options& opts)
      : host_state_(host_state), action_frame_(opts.action_frame), min_x_m_(opts.min_x_m),
        max_x_m_(opts.max_x_m), min_y_m_(opts.min_y_m), max_y_m_(opts.max_y_m),
        min_yaw_rad_(opts.min_yaw_rad), max_yaw_rad_(opts.max_yaw_rad)
  {
  }

  bt::vla_commit_validation validate(const bt::vla_commit_context& context,
                                     const bt::vla_action& action) override
  {
    if (!host_state_.ball_available)
    {
      return {.accepted = false, .reason = "ball_stale"};
    }
    if (!host_state_.robot_stable)
    {
      return {.accepted = false, .reason = "robot_unstable"};
    }
    if (action.type != bt::vla_action_type::continuous || action.u.size() != 3)
    {
      return {.accepted = false, .reason = "invalid_schema"};
    }
    if (context.expected_action_frame != action_frame_ || action.frame_id != action_frame_)
    {
      return {.accepted = false, .reason = "invalid_frame"};
    }
    if (!std::isfinite(action.u[0]) || !std::isfinite(action.u[1]) || !std::isfinite(action.u[2]) ||
        action.u[0] < min_x_m_ || action.u[0] > max_x_m_ || action.u[1] < min_y_m_ ||
        action.u[1] > max_y_m_ || action.u[2] < min_yaw_rad_ || action.u[2] > max_yaw_rad_)
    {
      return {.accepted = false, .reason = "invalid_pose"};
    }

    // This omission is the independent variable in the research baseline.
    // The host dispatch boundary still performs its own context check.
    return {.accepted = true, .reason = {}};
  }

private:
  experiment_host_state& host_state_;
  std::string action_frame_;
  double min_x_m_;
  double max_x_m_;
  double min_y_m_;
  double max_y_m_;
  double min_yaw_rad_;
  double max_yaw_rad_;
};

class recording_walking_dispatcher final : public bt::walking_target_dispatcher
{
public:
  explicit recording_walking_dispatcher(experiment_host_state& host_state) : host_state_(host_state)
  {
  }

  bt::walking_target_dispatch_result dispatch(const bt::walking_target_dispatch_context& context,
                                              const bt::walking_target& target) override
  {
    ++calls;
    last_context = context;
    last_target = target;
    if (!host_state_.robot_stable)
    {
      return {.accepted = false, .reason = "robot_unstable"};
    }
    return {.accepted = true, .reason = {}};
  }

  std::size_t calls = 0;
  bt::walking_target_dispatch_context last_context;
  bt::walking_target last_target;

private:
  experiment_host_state& host_state_;
};

template <typename T>
void put_if_changed(bt::tick_context& context, std::string key, T value, std::string writer)
{
  if (const bt::bb_entry* existing = context.bb_get(key); existing)
  {
    if (const T* current = std::get_if<T>(&existing->value); current && *current == value)
    {
      return;
    }
  }
  context.bb_put(std::move(key), bt::bb_value{std::move(value)}, std::move(writer));
}

void mark_branch(bt::tick_context& context, std::string branch)
{
  put_if_changed(context, "active-branch", std::move(branch), "experiment-branch");
}

bt::vla_invocation* latest_invocation(bt::instance& instance,
                                      bt::vla_authority_state authority_state)
{
  bt::vla_invocation* latest = nullptr;
  for (auto& [_, invocation] : instance.vla_invocations)
  {
    if (invocation.authority_state == authority_state &&
        (!latest || invocation.generation > latest->generation))
    {
      latest = &invocation;
    }
  }
  return latest;
}

void register_experiment_callbacks(bt::runtime_host& host, experiment_host_state& host_state,
                                   const options& opts)
{
  bt::registry& callbacks = host.callbacks();

  callbacks.register_condition("experiment-ball-unavailable",
                               [&host_state](bt::tick_context&, std::span<const muslisp::value>)
                               { return !host_state.ball_available; });
  callbacks.register_condition("experiment-job-active",
                               [](bt::tick_context& context, std::span<const muslisp::value>)
                               {
                                 const bt::bb_entry* entry = context.bb_get("approach-job");
                                 const auto* job_id =
                                     entry ? std::get_if<std::int64_t>(&entry->value) : nullptr;
                                 return job_id && *job_id > 0;
                               });

  callbacks.register_action(
      "experiment-sync-state",
      [&host_state](bt::tick_context& context, bt::node_id, bt::node_memory&,
                    std::span<const muslisp::value>)
      {
        put_if_changed(context, "ball-context-id", host_state.ball_context_id,
                       "experiment-host-state");
        put_if_changed(context, "ball-state", host_state.ball_position_m, "experiment-host-state");
        put_if_changed(context, "ball-available", host_state.ball_available,
                       "experiment-host-state");
        put_if_changed(context, "robot-stable", host_state.robot_stable, "experiment-host-state");
        put_if_changed(context, "emergency", host_state.emergency, "experiment-host-state");
        put_if_changed(context, "bridge-available", host_state.bridge_available,
                       "experiment-host-state");
        put_if_changed(context, "bridge-fault-reason", host_state.bridge_fault_reason,
                       "experiment-host-state");
        return bt::status::success;
      });

  callbacks.register_action(
      "experiment-mark-model-wait",
      [](bt::tick_context& context, bt::node_id, bt::node_memory&, std::span<const muslisp::value>)
      {
        mark_branch(context, "model_wait");
        put_if_changed(context, "request-state", std::string("running"), "experiment-overlay");
        return bt::status::success;
      });

  callbacks.register_action(
      "experiment-safe-stand",
      [](bt::tick_context& context, bt::node_id, bt::node_memory&, std::span<const muslisp::value>)
      {
        mark_branch(context, "safe_stand");
        put_if_changed(context, "request-state", std::string("revoked"), "experiment-overlay");
        put_if_changed(context, "walking-target-state", std::string("none"), "experiment-overlay");
        return bt::status::running;
      },
      [](bt::tick_context&, bt::node_id, bt::node_memory&) {});

  callbacks.register_action(
      "experiment-search",
      [](bt::tick_context& context, bt::node_id, bt::node_memory&, std::span<const muslisp::value>)
      {
        mark_branch(context, "search");
        put_if_changed(context, "walking-target-state", std::string("none"), "experiment-overlay");
        return bt::status::running;
      },
      [](bt::tick_context&, bt::node_id, bt::node_memory&) {});

  callbacks.register_action(
      "experiment-dispatch-target",
      [&host, &opts](bt::tick_context& context, bt::node_id node, bt::node_memory&,
                     std::span<const muslisp::value>)
      {
        bt::vla_invocation* invocation =
            latest_invocation(context.inst, bt::vla_authority_state::accepted);
        const bt::bb_entry* action_entry = context.bb_get("approach-action");
        const auto* action =
            action_entry ? std::get_if<std::vector<double>>(&action_entry->value) : nullptr;
        if (!invocation || !action || action->size() != 3)
        {
          return bt::status::failure;
        }

        const bt::walking_target target{.frame_id = opts.action_frame,
                                        .x_m = (*action)[0],
                                        .y_m = (*action)[1],
                                        .yaw_rad = (*action)[2]};
        const bt::walking_target_dispatch_result result = host.dispatch_walking_target(
            context.inst.instance_handle, invocation->job_id, node, target,
            bt::walking_target_dispatch_options{
                .require_context_match = !opts.unsafe_simulation_stale_dispatch});

        put_if_changed(context, "result-decision", std::string("accepted"), "experiment-overlay");
        put_if_changed(context, "result-reason", std::string{}, "experiment-overlay");
        if (result.accepted)
        {
          put_if_changed(context, "current-walking-target", *action, "experiment-overlay");
        }
        else
        {
          put_if_changed(context, "candidate-walking-target", *action, "experiment-overlay");
          put_if_changed(context, "candidate-target-job-id",
                         static_cast<std::int64_t>(invocation->job_id), "experiment-overlay");
          put_if_changed(context, "candidate-target-generation",
                         static_cast<std::int64_t>(invocation->generation), "experiment-overlay");
        }
        put_if_changed(context, "dispatch-reason", result.reason, "experiment-overlay");
        put_if_changed(context, "walking-target-state",
                       std::string(result.accepted ? "current" : "obsolete"), "experiment-overlay");
        put_if_changed(context, "request-state", std::string("done"), "experiment-overlay");
        mark_branch(context, result.accepted ? "model_execute" : "fallback");
        return bt::status::success;
      });

  callbacks.register_action(
      "experiment-result-rejected",
      [](bt::tick_context& context, bt::node_id, bt::node_memory&, std::span<const muslisp::value>)
      {
        bt::vla_invocation* invocation =
            latest_invocation(context.inst, bt::vla_authority_state::rejected);
        const std::string reason =
            invocation ? invocation->authority_reason : "backend_terminal_failure";
        bool has_candidate = false;
        if (invocation && context.svc.vla)
        {
          const bt::vla_poll poll = context.svc.vla->poll(invocation->job_id);
          if (poll.final.has_value() && poll.final->status == bt::vla_status::ok &&
              poll.final->action.type == bt::vla_action_type::continuous &&
              poll.final->action.u.size() == 3)
          {
            put_if_changed(context, "candidate-walking-target", poll.final->action.u,
                           "experiment-overlay");
            put_if_changed(context, "candidate-target-job-id",
                           static_cast<std::int64_t>(invocation->job_id), "experiment-overlay");
            put_if_changed(context, "candidate-target-generation",
                           static_cast<std::int64_t>(invocation->generation),
                           "experiment-overlay");
            has_candidate = true;
          }
        }
        put_if_changed(context, "result-decision", std::string("rejected"), "experiment-overlay");
        put_if_changed(context, "result-reason", reason, "experiment-overlay");
        put_if_changed(context, "request-state", std::string("rejected_safe_wait"),
                       "experiment-overlay");
        put_if_changed(context, "walking-target-state",
                       std::string(has_candidate && reason == "context_changed" ? "obsolete"
                                                                                : "none"),
                       "experiment-overlay");
        mark_branch(context, "fallback");
        // A rejected proposal leaves this branch in an explicit safe wait. The
        // finite video runner stops after recording that recovery state.
        return bt::status::running;
      },
      [](bt::tick_context&, bt::node_id, bt::node_memory&) {});

  callbacks.register_action(
      "experiment-fallback",
      [](bt::tick_context& context, bt::node_id, bt::node_memory&, std::span<const muslisp::value>)
      {
        mark_branch(context, "fallback");
        put_if_changed(context, "walking-target-state", std::string("none"), "experiment-overlay");
        return bt::status::running;
      },
      [](bt::tick_context&, bt::node_id, bt::node_memory&) {});
}

bool event_has(const bt::event_log& events, std::string_view type,
               std::initializer_list<std::string_view> fields = {})
{
  const std::string type_field = "\"type\":\"" + std::string(type) + "\"";
  for (const std::string& line : events.snapshot())
  {
    if (line.find(type_field) == std::string::npos)
    {
      continue;
    }
    bool matches = true;
    for (const std::string_view field : fields)
    {
      matches = matches && line.find(field) != std::string::npos;
    }
    if (matches)
    {
      return true;
    }
  }
  return false;
}

void apply_intervention(experiment_host_state& state, const options& opts)
{
  if (opts.intervention == "moved_ball")
  {
    state.ball_context_id = opts.moved_context_id;
    state.ball_position_m = opts.moved_ball_position_m;
  }
  else if (opts.intervention == "emergency")
  {
    state.emergency = true;
    state.robot_stable = false;
  }
}

int run(const options& opts)
{
  const std::string tree_source = read_text(opts.tree_path);
  bt::definition definition = load_defbt(tree_source);
  validate_request_contract(definition, opts);

  experiment_host_state host_state{.ball_context_id = opts.initial_context_id,
                                    .ball_position_m = opts.initial_ball_position_m};
#if defined(MUESLI_BT_HAVE_BOOSTER_BRIDGE)
  std::unique_ptr<muesli_bt::booster::bridge_walking_target_dispatcher> bridge_dispatcher;
  std::string initial_bridge_context_id;
  if (!opts.booster_bridge_socket.empty())
  {
    bridge_dispatcher =
        std::make_unique<muesli_bt::booster::bridge_walking_target_dispatcher>(
            muesli_bt::booster::bridge_client_config{
                .socket_path = opts.booster_bridge_socket,
                .timeout = std::chrono::milliseconds(opts.booster_bridge_timeout_ms),
            });
    const muesli_bt::booster::snapshot_result initial_snapshot =
        bridge_dispatcher->client().snapshot();
    if (!initial_snapshot.ok)
    {
      fail("Booster bridge initial snapshot failed: " + initial_snapshot.reason);
    }
    if (initial_snapshot.snapshot.motion_enabled != opts.physical_motion_enabled)
    {
      fail("Booster bridge motion state does not match --physical-motion-enabled");
    }
    apply_bridge_snapshot(host_state, initial_snapshot.snapshot);
    initial_bridge_context_id = host_state.ball_context_id;
  }
#endif
  bt::runtime_host host;
  host.events().set_path(opts.event_path.string());
  host.events().set_file_enabled(true);
  host.events().set_run_id(opts.run_id);
  host.events().set_tick_hz(opts.tick_hz);
  host.events().set_git_sha(opts.git_sha);
  host.events().set_host_info("humanoid-model-mediated-trial", "experimental", opts.platform);
  host.events().ensure_run_started(
      definition.canonical_dsl_hash,
      std::string("{\"reset\":true,\"walking_target_dispatch\":true,\"physical_motion\":") +
          (opts.physical_motion_enabled ? "true}" : "false}"));

  const auto backend = std::make_shared<humanoid_experiment::delayed_fake_service>(
      humanoid_experiment::delayed_fake_service_config{
          .delay = std::chrono::milliseconds(opts.delay_ms),
          .x_m = opts.pose_x_m,
          .y_m = opts.pose_y_m,
          .yaw_rad = opts.pose_yaw_rad,
      });
  host.vla_ref().set_cache_ttl_ms(0);
  host.vla_ref().register_backend(opts.backend_name, backend);

  deadline_only_baseline_validator baseline_validator(host_state, opts);
  bt::approach_pose_validator full_validator(
      bt::approach_pose_validator_config{
          .frame_id = opts.action_frame,
          .bounds = {.min_x_m = opts.min_x_m,
                     .max_x_m = opts.max_x_m,
                     .min_y_m = opts.min_y_m,
                     .max_y_m = opts.max_y_m,
                     .min_yaw_rad = opts.min_yaw_rad,
                     .max_yaw_rad = opts.max_yaw_rad}},
      [&host_state]
      {
        return bt::approach_pose_host_state{
            .ball_context_id = host_state.ball_context_id,
            .robot_stable = host_state.robot_stable,
        };
      });
  host.set_vla_commit_validator(opts.acceptance_policy == "deadline_only"
                                    ? static_cast<bt::vla_commit_validator*>(&baseline_validator)
                                    : static_cast<bt::vla_commit_validator*>(&full_validator));

  recording_walking_dispatcher dispatcher(host_state);
#if defined(MUESLI_BT_HAVE_BOOSTER_BRIDGE)
  host.set_walking_target_dispatcher(bridge_dispatcher
                                         ? static_cast<bt::walking_target_dispatcher*>(
                                               bridge_dispatcher.get())
                                         : static_cast<bt::walking_target_dispatcher*>(&dispatcher));
#else
  host.set_walking_target_dispatcher(&dispatcher);
#endif
  register_experiment_callbacks(host, host_state, opts);

  const std::int64_t definition_handle = host.store_definition(std::move(definition));
  const std::int64_t instance_handle = host.create_instance(definition_handle);
  bt::instance* instance = host.find_instance(instance_handle);
  if (!instance)
  {
    fail("failed to create experiment BT instance");
  }

  const auto started_at = std::chrono::steady_clock::now();
  const auto tick_period = std::chrono::duration<double>(1.0 / opts.tick_hz);
  auto next_tick = started_at;
  const auto maximum_runtime =
      std::chrono::milliseconds(std::max(opts.deadline_ms, opts.delay_ms) + 2000);
  bool intervention_applied = opts.intervention == "none";
  bool complete = false;
  std::optional<std::chrono::steady_clock::time_point> request_submitted_at;

  while (std::chrono::steady_clock::now() - started_at <= maximum_runtime)
  {
    const auto now = std::chrono::steady_clock::now();
#if defined(MUESLI_BT_HAVE_BOOSTER_BRIDGE)
    if (bridge_dispatcher)
    {
      const muesli_bt::booster::snapshot_result live_snapshot =
          bridge_dispatcher->client().snapshot();
      if (live_snapshot.ok)
      {
        apply_bridge_snapshot(host_state, live_snapshot.snapshot);
      }
      else
      {
        fail_closed_bridge_state(host_state, live_snapshot.reason);
      }
    }
#endif
    if (!request_submitted_at.has_value() && !instance->vla_invocations.empty())
    {
      request_submitted_at = instance->vla_invocations.begin()->second.submitted_at;
      std::cout << "REQUEST_SUBMITTED trial=" << opts.trial_id << " delay_ms=" << opts.delay_ms
                << '\n'
                << std::flush;
    }
    const auto request_elapsed =
        request_submitted_at.has_value()
            ? std::chrono::duration_cast<std::chrono::milliseconds>(now - *request_submitted_at)
            : 0ms;
    bool intervention_observed = false;
#if defined(MUESLI_BT_HAVE_BOOSTER_BRIDGE)
    if (bridge_dispatcher && request_submitted_at.has_value())
    {
      intervention_observed =
          (opts.intervention == "moved_ball" && !host_state.ball_context_id.empty() &&
           host_state.ball_context_id != initial_bridge_context_id) ||
          (opts.intervention == "emergency" && host_state.emergency);
    }
    else
#endif
    if (request_submitted_at.has_value() && request_elapsed.count() >= opts.intervention_ms)
    {
      apply_intervention(host_state, opts);
      intervention_observed = true;
    }
    if (!intervention_applied && intervention_observed)
    {
      intervention_applied = true;
      std::cout << "INTERVENTION trial=" << opts.trial_id << " kind=" << opts.intervention << '\n'
                << std::flush;
    }

    (void)host.tick_instance(instance_handle);

    if (opts.intervention == "emergency")
    {
      complete =
          intervention_applied &&
          event_has(host.events(), "async_authority_revoked", {"\"reason\":\"branch_revoked\""}) &&
          event_has(host.events(), "async_completion_dropped",
                    {"\"reason\":\"completion_after_cancel\""});
    }
    else
    {
      complete =
          intervention_applied && event_has(host.events(), "vla_result", {"\"decision\":"});
    }
    if (complete)
    {
      break;
    }

    next_tick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(tick_period);
    std::this_thread::sleep_until(next_tick);
  }

  std::ostringstream run_end;
#if defined(MUESLI_BT_HAVE_BOOSTER_BRIDGE)
  const std::size_t dispatch_calls =
      bridge_dispatcher ? bridge_dispatcher->dispatch_count() : dispatcher.calls;
#else
  const std::size_t dispatch_calls = dispatcher.calls;
#endif
  run_end << "{\"status\":\"" << (complete ? "complete" : "timeout") << "\",\"trial_id\":\""
          << bt::event_log::json_escape(opts.trial_id) << "\",\"ticks\":" << instance->tick_index
          << ",\"recording_dispatch_calls\":" << dispatch_calls << '}';
  (void)host.events().emit("run_end", instance->tick_index, run_end.str());

  host.set_vla_commit_validator(nullptr);
  host.set_walking_target_dispatcher(nullptr);
  if (!complete)
  {
    std::cerr << "trial timed out before the required terminal evidence was observed\n";
    return 1;
  }
  std::cout << "TRIAL_COMPLETE trial=" << opts.trial_id
            << " events=" << host.events().snapshot().size() << '\n';
  return 0;
}

} // namespace

int main(int argc, char** argv)
{
  try
  {
    return run(parse_options(argc, argv));
  }
  catch (const std::exception& error)
  {
    std::cerr << "humanoid trial error: " << error.what() << '\n';
    return 2;
  }
}
