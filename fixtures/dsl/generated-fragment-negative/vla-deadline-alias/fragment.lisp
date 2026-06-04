(reactive-sel
  (seq
    (vla-request
      :name "alias"
      :job_key alias-job
      :instruction "move"
      :state_key state
      :budget_ms 20
      :capability "vla.rt2")
    (vla-wait
      :name "alias"
      :job_key alias-job
      :action_key action))
  (act safe-stop))
