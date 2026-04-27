(seq
  (vla-request
    :name "policy"
    :job_key policy-job
    :capability "vla.rt2"
    :instruction "move"
    :deadline_ms 50)
  (running))
