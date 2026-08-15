(defbt air-hockey-model-mediated-defence
  (reactive-seq
    (act air-hockey-sync-state)
    (reactive-sel
      (seq
        (cond air-hockey-episode-ended)
        (act air-hockey-terminal-hold))
      (seq
        (cond air-hockey-defence-unavailable)
        (act air-hockey-fallback))
      (seq
        (cond air-hockey-job-active)
        (act air-hockey-fallback)
        (sel
          (seq
            (vla-wait
              :name "air-hockey-defence"
              :job_key defence-job
              :action_key defence-action
              :meta_key defence-meta
              :clear_job #f)
            (act air-hockey-dispatch))
          (act air-hockey-result-rejected)))
      (seq
        (cond air-hockey-proposal-required)
        (act air-hockey-fallback)
        (vla-request
          :name "air-hockey-defence"
          :job_key defence-job
          :instruction "choose one bounded normalised mallet target from the public observation"
          :state_key air-hockey-state
          :capability "cap.vla.action_chunk.v1"
          :frame_id "airhockey.public_observation.v1"
          :model_name "airhockey-delayed-fake"
          :model_version "deterministic-v1"
          :deadline_ms 120
          :seed 6302
          :dims 2
          :bound_lo -1.0
          :bound_hi 1.0
          :max_abs 1.0
          :max_delta 2.0
          :action_frame "airhockey.normalised_mallet_target.v1"
          :acceptance_policy invocation_scoped
          :context_key air-hockey-context-id))
      (act air-hockey-fallback))))
