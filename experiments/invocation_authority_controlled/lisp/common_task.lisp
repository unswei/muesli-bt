(defbt controlled-invocation-authority-common-task
  (reactive-sel
    (seq
      (cond controlled-emergency?)
      (act controlled-safe-stand))
    (seq
      (act controlled-model-step)
      (act controlled-dispatch-step))
    (act controlled-fallback)))
