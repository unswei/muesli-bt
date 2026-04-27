(reactive-sel
  (seq
    (vla-request
      :name "bad-policy"
      :job_key policy-job
      :capability "unsupported.force"
      :instruction "move"
      :deadline_ms 50)
    (running))
  (act stop))
