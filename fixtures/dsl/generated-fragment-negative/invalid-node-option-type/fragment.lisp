(reactive-sel
  (vla-wait
    :name "bad-type"
    :job_key vla-job
    :early_commit "yes")
  (act safe-stop))
