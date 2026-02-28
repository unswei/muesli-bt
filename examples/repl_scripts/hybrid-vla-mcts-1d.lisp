;; Hybrid sampling example:
;; 1) ask VLA stub for an action proposal
;; 2) warm-start planner.plan (MPPI backend) from that proposal

(define vreq (map.make))
(map.set! vreq 'task_id "hybrid-demo")
(map.set! vreq 'instruction "move toward +1.0")
(map.set! vreq 'deadline_ms 20)
(map.set! vreq 'seed 42)

(let ((obs (map.make)))
  (map.set! obs 'state (list 0.0))
  (map.set! obs 'timestamp_ms 100)
  (map.set! obs 'frame_id "base")
  (map.set! vreq 'observation obs))

(let ((space (map.make)))
  (map.set! space 'type ':continuous)
  (map.set! space 'dims 1)
  (map.set! space 'bounds (list (list -1.0 1.0)))
  (map.set! vreq 'action_space space))

(let ((constraints (map.make)))
  (map.set! constraints 'max_abs_value 1.0)
  (map.set! constraints 'max_delta 1.0)
  (map.set! vreq 'constraints constraints))

(let ((model (map.make)))
  (map.set! model 'name "rt2-stub")
  (map.set! model 'version "stub-1")
  (map.set! vreq 'model model))

(define job (vla.submit vreq))

(define (wait-final id remaining)
  (if (= remaining 0)
      (vla.poll id)
      (let ((p (vla.poll id)))
        (if (eq? (map.get p 'status ':none) ':done)
            p
            (wait-final id (- remaining 1))))))

(define vpoll (wait-final job 64))
(define prior
  (map.get (map.get (map.get vpoll 'final (map.make)) 'action (map.make)) 'u (list 0.0)))

(define preq (map.make))
(map.set! preq 'schema_version "planner.request.v1")
(map.set! preq 'planner "mppi")
(map.set! preq 'model_service "toy-1d")
(map.set! preq 'state (list 0.0))
(map.set! preq 'seed 42)
(map.set! preq 'budget_ms 8)
(map.set! preq 'work_max 500)
(map.set! preq 'horizon 16)

(define safe (map.make))
(map.set! safe 'action_schema "action.u.v1")
(map.set! safe 'u prior)
(map.set! preq 'safe_action safe)

(define mppi (map.make))
(map.set! mppi 'lambda 0.5)
(map.set! mppi 'sigma (list 0.15))
(map.set! mppi 'n_samples 500)
(map.set! mppi 'u_nominal prior)
(map.set! preq 'mppi mppi)

(print (planner.plan preq))
