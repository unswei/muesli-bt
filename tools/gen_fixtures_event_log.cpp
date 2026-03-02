#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

void write_lines(const std::filesystem::path& out_path, const std::vector<std::string>& lines)
{
  std::filesystem::create_directories(out_path.parent_path());
  std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
  if (!out)
  {
    throw std::runtime_error("failed to open fixture file: " + out_path.string());
  }
  for (const std::string& line : lines)
  {
    out << line << '\n';
  }
  if (!out)
  {
    throw std::runtime_error("failed while writing fixture file: " + out_path.string());
  }
}

std::vector<std::string> minimal_run_lines()
{
  return {
      R"EVT({"schema":"mbt.evt.v1","type":"run_start","run_id":"fixture-minimal","unix_ms":1735689600000,"seq":1,"data":{"git_sha":"fixture","host":{"name":"muesli-bt","version":"0.1.0","platform":"linux"},"tick_hz":20.0,"tree_hash":"fnv1a64:aaaaaaaaaaaaaaaa","capabilities":{"reset":true}}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"bt_def","run_id":"fixture-minimal","unix_ms":1735689600001,"seq":2,"data":{"tree_name":"bt","dsl":"(bt (seq (cond always-true) (act always-success)))","tree_hash":"fnv1a64:aaaaaaaaaaaaaaaa","nodes":[{"id":1,"kind":"seq","name":"root"},{"id":2,"kind":"cond","name":"always-true"},{"id":3,"kind":"act","name":"always-success"}],"edges":[{"parent":1,"child":2,"index":0},{"parent":1,"child":3,"index":1}]}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"tick_begin","run_id":"fixture-minimal","unix_ms":1735689600010,"seq":3,"tick":1,"data":{"root":1}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"bb_write","run_id":"fixture-minimal","unix_ms":1735689600011,"seq":4,"tick":1,"data":{"key":"state","value_digest":"fnv1a64:1111111111111111","preview":"[0.0,0.0]","source":"fixture"}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"tick_end","run_id":"fixture-minimal","unix_ms":1735689600012,"seq":5,"tick":1,"data":{"status":"running","active_path":[1,2],"budget":{"tick_budget_ms":20.0,"tick_time_ms":2.1}}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"tick_begin","run_id":"fixture-minimal","unix_ms":1735689600020,"seq":6,"tick":2,"data":{"root":1}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"bb_write","run_id":"fixture-minimal","unix_ms":1735689600021,"seq":7,"tick":2,"data":{"key":"action","value_digest":"fnv1a64:2222222222222222","preview":"[0.1,0.0]","source":"fixture"}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"tick_end","run_id":"fixture-minimal","unix_ms":1735689600022,"seq":8,"tick":2,"data":{"status":"success","active_path":[1,3],"budget":{"tick_budget_ms":20.0,"tick_time_ms":1.3}}})EVT"};
}

std::vector<std::string> planner_run_lines()
{
  return {
      R"EVT({"schema":"mbt.evt.v1","type":"run_start","run_id":"fixture-planner","unix_ms":1735689600100,"seq":1,"data":{"git_sha":"fixture","host":{"name":"muesli-bt","version":"0.1.0","platform":"linux"},"tick_hz":20.0,"tree_hash":"fnv1a64:bbbbbbbbbbbbbbbb","capabilities":{"reset":true}}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"planner_v1","run_id":"fixture-planner","unix_ms":1735689600101,"seq":2,"tick":1,"data":{"record":{"schema_version":"planner.v1","planner":"mcts","status":"ok","node_name":"plan-action","budget_ms":10,"elapsed_ms":3.2,"action":{"action_schema":"action.u.v1","u":[0.2,0.0]}}}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"tick_end","run_id":"fixture-planner","unix_ms":1735689600102,"seq":3,"tick":1,"data":{"status":"success","budget":{"tick_budget_ms":20.0,"tick_time_ms":4.1}}})EVT"};
}

std::vector<std::string> scheduler_run_lines()
{
  return {
      R"EVT({"schema":"mbt.evt.v1","type":"run_start","run_id":"fixture-scheduler","unix_ms":1735689600200,"seq":1,"data":{"git_sha":"fixture","host":{"name":"muesli-bt","version":"0.1.0","platform":"linux"},"tick_hz":50.0,"tree_hash":"fnv1a64:cccccccccccccccc","capabilities":{"reset":true}}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"sched_submit","run_id":"fixture-scheduler","unix_ms":1735689600201,"seq":2,"tick":4,"data":{"job_id":101,"node_id":12,"queue_depth":1}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"sched_start","run_id":"fixture-scheduler","unix_ms":1735689600202,"seq":3,"tick":4,"data":{"job_id":101,"node_id":12,"worker":"pool-0"}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"sched_finish","run_id":"fixture-scheduler","unix_ms":1735689600203,"seq":4,"tick":4,"data":{"job_id":101,"node_id":12,"status":"ok","run_time_ns":1200000}})EVT"};
}

std::vector<std::string> vla_run_lines()
{
  return {
      R"EVT({"schema":"mbt.evt.v1","type":"run_start","run_id":"fixture-vla","unix_ms":1735689600300,"seq":1,"data":{"git_sha":"fixture","host":{"name":"muesli-bt","version":"0.1.0","platform":"linux"},"tick_hz":20.0,"tree_hash":"fnv1a64:dddddddddddddddd","capabilities":{"reset":true}}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"vla_submit","run_id":"fixture-vla","unix_ms":1735689600301,"seq":2,"tick":2,"data":{"job_id":"job-1","node_id":7,"capability":"vision"}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"vla_poll","run_id":"fixture-vla","unix_ms":1735689600302,"seq":3,"tick":3,"data":{"job_id":"job-1","node_id":7,"status":"running"}})EVT",
      R"EVT({"schema":"mbt.evt.v1","type":"vla_result","run_id":"fixture-vla","unix_ms":1735689600303,"seq":4,"tick":4,"data":{"job_id":"job-1","node_id":7,"status":"done","digest":"fnv1a64:eeeeeeeeeeeeeeee","record":{"schema_version":"vla.result.v1","action":{"action_schema":"action.u.v1","u":[0.0]}}}})EVT"};
}

} // namespace

int main(int argc, char** argv)
{
  try
  {
    std::filesystem::path out_dir = "tests/fixtures/mbt.evt.v1";
    if (argc > 1)
    {
      out_dir = argv[1];
    }

    write_lines(out_dir / "minimal_run.jsonl", minimal_run_lines());
    write_lines(out_dir / "planner_run.jsonl", planner_run_lines());
    write_lines(out_dir / "scheduler_run.jsonl", scheduler_run_lines());
    write_lines(out_dir / "vla_run.jsonl", vla_run_lines());

    std::cout << "wrote fixtures to " << out_dir << '\n';
    return 0;
  }
  catch (const std::exception& e)
  {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
}
