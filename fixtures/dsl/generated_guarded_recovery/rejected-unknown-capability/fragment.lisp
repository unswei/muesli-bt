(reactive-sel
  (seq
    (cond blocked-path?)
    (vla-request
      :name "bad-recovery-policy"
      :job_key recovery-job
      :capability "unsupported.force"
      :instruction "recover"
      :deadline_ms 50)
    (act execute-recovery-turn))
  (act safe-stop))
