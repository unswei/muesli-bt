#include "timing_plan.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{
namespace
{

std::vector<std::string> split_tabs(std::string_view line)
{
  std::vector<std::string> fields;
  std::size_t begin = 0;
  while (begin <= line.size())
  {
    const std::size_t end = line.find('\t', begin);
    fields.emplace_back(
        line.substr(begin, end == std::string_view::npos ? line.size() - begin : end - begin));
    if (end == std::string_view::npos)
    {
      break;
    }
    begin = end + 1;
  }
  return fields;
}

template <typename Integer> Integer parse_integer(std::string_view value, std::string_view field)
{
  Integer result{};
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
  {
    throw std::invalid_argument("invalid integer in timing plan field " + std::string(field));
  }
  return result;
}

double parse_double(const std::string& value, std::string_view field)
{
  std::size_t consumed = 0;
  const double result = std::stod(value, &consumed);
  if (consumed != value.size())
  {
    throw std::invalid_argument("invalid number in timing plan field " + std::string(field));
  }
  return result;
}

void require_fields(const std::vector<std::string>& fields, std::size_t count,
                    std::size_t line_number)
{
  if (fields.size() != count)
  {
    throw std::invalid_argument("timing plan line " + std::to_string(line_number) +
                                " has the wrong field count");
  }
}

bool parse_boolean(std::string_view value)
{
  if (value == "0")
  {
    return false;
  }
  if (value == "1")
  {
    return true;
  }
  throw std::invalid_argument("invalid boolean in timing plan field recorded");
}

bool known_variant(std::string_view value)
{
  return value == "B0" || value == "B1" || value == "B2" || value == "B3";
}

bool known_axis(std::string_view value)
{
  return value == "primary" || value == "service_delay" || value == "tick_rate" ||
         value == "concurrency" || value == "distribution";
}

bool known_distribution(std::string_view value)
{
  return value == "fixed" || value == "uniform" || value == "bimodal" ||
         value == "long_tail";
}

} // namespace

timing_plan read_timing_plan(const std::filesystem::path& path)
{
  std::ifstream input(path);
  if (!input)
  {
    throw std::invalid_argument("could not open timing plan: " + path.string());
  }

  timing_plan plan;
  bool saw_version = false;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line))
  {
    ++line_number;
    if (line.empty() || line.front() == '#')
    {
      continue;
    }
    const std::vector<std::string> fields = split_tabs(line);
    const std::string& kind = fields.front();
    if (kind == "plan_version")
    {
      require_fields(fields, 2, line_number);
      if (fields[1] != "controlled-authority.timing-plan.v1")
      {
        throw std::invalid_argument("unsupported controlled-authority timing plan version");
      }
      saw_version = true;
    }
    else if (kind == "protocol_id")
    {
      require_fields(fields, 2, line_number);
      plan.protocol_id = fields[1];
    }
    else if (kind == "timing_contract_id")
    {
      require_fields(fields, 2, line_number);
      plan.timing_contract_id = fields[1];
    }
    else if (kind == "initial_context_id")
    {
      require_fields(fields, 2, line_number);
      plan.initial_context_id = fields[1];
    }
    else if (kind == "request_deadline_ms")
    {
      require_fields(fields, 2, line_number);
      plan.request_deadline = logical_time{parse_integer<std::int64_t>(fields[1], kind)};
    }
    else if (kind == "frame_id")
    {
      require_fields(fields, 2, line_number);
      plan.validation.frame_id = fields[1];
    }
    else if (kind == "minimum" || kind == "maximum")
    {
      require_fields(fields, 4, line_number);
      std::array<double, 3>& target =
          kind == "minimum" ? plan.validation.minimum : plan.validation.maximum;
      for (std::size_t index = 0; index < target.size(); ++index)
      {
        target[index] = parse_double(fields[index + 1], kind);
      }
    }
    else if (kind == "common_task_path")
    {
      require_fields(fields, 2, line_number);
      plan.common_task_path = fields[1];
    }
    else if (kind == "run")
    {
      require_fields(fields, 12, line_number);
      timing_run run{
          .condition_id = fields[1],
          .axis = fields[2],
          .reader_label = fields[3],
          .variant_label = fields[4],
          .seed = parse_integer<std::uint64_t>(fields[5], "run.seed"),
          .repetition = parse_integer<std::size_t>(fields[6], "run.repetition"),
          .service_delay = logical_time{parse_integer<std::int64_t>(fields[7], "run.delay")},
          .tick_rate_hz = parse_integer<std::size_t>(fields[8], "run.tick_rate"),
          .concurrent_jobs = parse_integer<std::size_t>(fields[9], "run.concurrent_jobs"),
          .distribution = fields[10],
          .recorded = parse_boolean(fields[11]),
      };
      if (run.condition_id.empty() || run.reader_label.empty() || !known_axis(run.axis) ||
          !known_variant(run.variant_label) || run.service_delay.count() < 0 ||
          run.tick_rate_hz == 0 || run.concurrent_jobs == 0 ||
          !known_distribution(run.distribution))
      {
        throw std::invalid_argument("invalid run in controlled-authority timing plan");
      }
      plan.runs.push_back(std::move(run));
    }
    else
    {
      throw std::invalid_argument("unknown timing plan record: " + kind);
    }
  }

  if (!saw_version || plan.protocol_id.empty() || plan.timing_contract_id.empty() ||
      plan.initial_context_id.empty() || plan.request_deadline.count() <= 0 ||
      plan.validation.frame_id.empty() || plan.common_task_path.empty() || plan.runs.empty())
  {
    throw std::invalid_argument("timing plan is missing required metadata");
  }

  std::unordered_set<std::string> run_keys;
  for (const timing_run& run : plan.runs)
  {
    const std::string key = run.condition_id + ':' + run.variant_label + ':' +
                            std::to_string(run.repetition) + ':' +
                            (run.recorded ? "recorded" : "warmup");
    if (!run_keys.insert(key).second)
    {
      throw std::invalid_argument("duplicate run in timing plan: " + key);
    }
  }
  return plan;
}

} // namespace muesli_bt::experiments::controlled_authority
