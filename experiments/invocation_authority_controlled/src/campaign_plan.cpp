#include "campaign_plan.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

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
    throw std::invalid_argument("invalid integer in campaign plan field " + std::string(field));
  }
  return result;
}

double parse_double(const std::string& value, std::string_view field)
{
  std::size_t consumed = 0;
  const double result = std::stod(value, &consumed);
  if (consumed != value.size())
  {
    throw std::invalid_argument("invalid number in campaign plan field " + std::string(field));
  }
  return result;
}

void require_fields(const std::vector<std::string>& fields, std::size_t count,
                    std::size_t line_number)
{
  if (fields.size() != count)
  {
    throw std::invalid_argument("campaign plan line " + std::to_string(line_number) +
                                " has the wrong field count");
  }
}

bool parse_boolean(std::string_view value, std::string_view field)
{
  if (value == "0")
  {
    return false;
  }
  if (value == "1")
  {
    return true;
  }
  throw std::invalid_argument("invalid boolean in campaign plan field " + std::string(field));
}

} // namespace

campaign_plan read_campaign_plan(const std::filesystem::path& path)
{
  std::ifstream input(path);
  if (!input)
  {
    throw std::invalid_argument("could not open campaign plan: " + path.string());
  }

  campaign_plan plan;
  schedule_definition* current_schedule = nullptr;
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
    if (fields.empty())
    {
      continue;
    }

    const std::string& kind = fields[0];
    if (kind == "plan_version")
    {
      require_fields(fields, 2, line_number);
      if (fields[1] != "controlled-authority.plan.v1")
      {
        throw std::invalid_argument("unsupported controlled-authority campaign plan version");
      }
      saw_version = true;
    }
    else if (kind == "protocol_id")
    {
      require_fields(fields, 2, line_number);
      plan.protocol_id = fields[1];
    }
    else if (kind == "catalogue_id")
    {
      require_fields(fields, 2, line_number);
      plan.catalogue_id = fields[1];
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
    else if (kind == "schedule")
    {
      require_fields(fields, 2, line_number);
      schedule_definition definition{.schedule_id = fields[1]};
      const auto inserted = plan.schedules.emplace(fields[1], std::move(definition));
      if (!inserted.second)
      {
        throw std::invalid_argument("duplicate schedule in campaign plan: " + fields[1]);
      }
      current_schedule = &inserted.first->second;
    }
    else if (kind == "event")
    {
      require_fields(fields, 9, line_number);
      if (!current_schedule)
      {
        throw std::invalid_argument("campaign event appears outside a schedule");
      }
      current_schedule->events.push_back(planned_event{
          .at = logical_time{parse_integer<std::int64_t>(fields[1], "event.at")},
          .event = fields[2],
          .request = fields[3],
          .context_id = fields[4],
          .count = fields[5].empty() ? 0 : parse_integer<std::size_t>(fields[5], "event.count"),
          .duplicate = fields[6].empty() ? false : parse_boolean(fields[6], "event.duplicate"),
          .ordering =
              fields[7].empty() ? -1 : parse_integer<std::int64_t>(fields[7], "event.ordering"),
      });
      if (!fields[8].empty())
      {
        throw std::invalid_argument("reserved campaign event field must be empty");
      }
    }
    else if (kind == "end_schedule")
    {
      require_fields(fields, 1, line_number);
      current_schedule = nullptr;
    }
    else if (kind == "run")
    {
      require_fields(fields, 5, line_number);
      plan.runs.push_back(planned_run{
          .schedule_id = fields[1],
          .variant_label = fields[2],
          .seed = parse_integer<std::uint64_t>(fields[3], "run.seed"),
          .expected_outcome = fields[4],
      });
    }
    else
    {
      throw std::invalid_argument("unknown campaign plan record: " + kind);
    }
  }

  if (!saw_version || plan.protocol_id.empty() || plan.catalogue_id.empty() ||
      plan.initial_context_id.empty() || plan.request_deadline.count() <= 0 ||
      plan.validation.frame_id.empty() || plan.common_task_path.empty() || plan.schedules.empty() ||
      plan.runs.empty())
  {
    throw std::invalid_argument("campaign plan is missing required metadata");
  }
  if (current_schedule)
  {
    throw std::invalid_argument("campaign plan has an unterminated schedule");
  }
  for (const auto& [schedule_id, schedule] : plan.schedules)
  {
    logical_time previous{0};
    bool first = true;
    for (const planned_event& event : schedule.events)
    {
      if ((!first && event.at < previous) || event.at.count() < 0)
      {
        throw std::invalid_argument("campaign schedule is not time ordered: " + schedule_id);
      }
      first = false;
      previous = event.at;
    }
  }
  std::unordered_set<std::string> run_keys;
  for (const planned_run& run : plan.runs)
  {
    if (plan.schedules.find(run.schedule_id) == plan.schedules.end())
    {
      throw std::invalid_argument("campaign run references an unknown schedule: " +
                                  run.schedule_id);
    }
    if (run.variant_label != "B0" && run.variant_label != "B1" && run.variant_label != "B2" &&
        run.variant_label != "B3")
    {
      throw std::invalid_argument("campaign run references an unknown variant: " +
                                  run.variant_label);
    }
    const std::string key =
        run.schedule_id + ':' + run.variant_label + ':' + std::to_string(run.seed);
    if (!run_keys.insert(key).second)
    {
      throw std::invalid_argument("duplicate run in campaign plan: " + key);
    }
  }
  return plan;
}

} // namespace muesli_bt::experiments::controlled_authority
