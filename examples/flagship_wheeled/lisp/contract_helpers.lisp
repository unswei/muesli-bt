; Shared helper functions for the v0.5 cross-transport flagship.
; Keep helper logic backend-neutral. Observation normalisation and actuator
; conversion belong in the backend wrappers, not here.

(define pi 3.141592653589793)

(define (make-shared-command linear-x angular-z)
  (list linear-x angular-z))

(define (make-planner-state goal-dist goal-bearing obstacle-front speed)
  (list goal-dist goal-bearing obstacle-front speed))

(define (goal-reached-q goal-dist goal-tolerance-m)
  (<= goal-dist goal-tolerance-m))

(define (collision-imminent-q obstacle-front collision-threshold)
  (>= obstacle-front collision-threshold))

(define (clamp-normalised x)
  (clamp x -1.0 1.0))

(define (direct-goal-command goal-bearing)
  (let ((linear-x 0.45)
        (angular-z (clamp-normalised (* 0.8 goal-bearing))))
    (make-shared-command linear-x angular-z)))
