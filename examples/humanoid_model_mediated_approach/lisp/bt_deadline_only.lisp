(defbt humanoid-model-mediated-approach-deadline-only
  (reactive-seq
    (act experiment-sync-state)
    (reactive-sel
      (seq
        (cond bb-truthy emergency)
        (act experiment-safe-stand))
      (seq
        (cond experiment-ball-unavailable)
        (act experiment-search))
      (seq
        (cond experiment-job-active)
        (act experiment-mark-model-wait)
        (sel
          (seq
            (vla-wait
              :name "approach-pose"
              :job_key approach-job
              :action_key approach-action
              :meta_key approach-meta)
            (act experiment-dispatch-target))
          (act experiment-result-rejected)))
      (seq
        (act experiment-mark-model-wait)
        (vla-request
          :name "approach-pose"
          :job_key approach-job
          :instruction "choose a bounded approach pose relative to the observed ball"
          :state_key ball-state
          :frame_id field
          :model_name "humanoid-delayed-fake"
          :model_version "deterministic-v1"
          :deadline_ms 3500
          :seed 424242
          :dims 3
          :bound_lo -1.0
          :bound_hi 1.0
          :max_delta 10.0
          :action_frame ball_context
          :acceptance_policy deadline_only
          :context_key ball-context-id))
      (act experiment-fallback))))
