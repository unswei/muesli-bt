(load "lisp/bt_obstacle_wallfollow.lisp")

(define (nth xs idx)
  (if (= idx 0)
      (car xs)
      (nth (cdr xs) (- idx 1))))

(define (max2 a b)
  (if (> a b) a b))

(define (make-action left right)
  (begin
    (define action (map.make))
    (map.set! action 'action_schema "epuck.action.v1")
    (map.set! action 'u (list left right))
    action))

(define (branch-path branch-id)
  (if (= branch-id 1)
      (list "root" "collision" "avoid")
      (if (= branch-id 2)
          (list "root" "wall" "follow_wall")
          (list "root" "roam"))))

(define (make-bt-map branch-id)
  (begin
    (define bt (map.make))
    (define status-by-node (map.make))
    (map.set! bt 'active_path (branch-path branch-id))
    (map.set! status-by-node "root" "running")
    (map.set! status-by-node "branch" (if (= branch-id 1) "avoid" (if (= branch-id 2) "follow_wall" "roam")))
    (map.set! bt 'status_by_node status-by-node)
    bt))

(define roam-rng (rng.make 424242))

(define (compute-avoid-action proximity min-obstacle)
  (begin
    (define right-signal (+ (+ (nth proximity 0) (nth proximity 1)) (nth proximity 2)))
    (define left-signal (+ (+ (nth proximity 5) (nth proximity 6)) (nth proximity 7)))
    (define pmax (clamp (- 1.0 min-obstacle) 0.0 1.0))
    (define closeness (clamp (/ (- pmax 0.36) 0.14) 0.0 1.0))
    (define turn-gain (+ 1.2 (* 4.2 closeness)))
    (define forward (clamp (- 1.8 (* 2.2 closeness)) -1.0 1.2))
    (if (> right-signal left-signal)
        (list (- forward turn-gain) (+ forward turn-gain))
        (list (+ forward turn-gain) (- forward turn-gain)))))

(define (compute-wall-action proximity)
  (begin
    (define left-wall (/ (+ (+ (nth proximity 5) (nth proximity 6)) (nth proximity 7)) 3.0))
    (define right-wall (/ (+ (+ (nth proximity 0) (nth proximity 1)) (nth proximity 2)) 3.0))
    (define follow-left (> left-wall right-wall))
    (define side-dist (if follow-left left-wall right-wall))
    (define error (- side-dist 0.16))
    (define corr (clamp (* 7.0 error) -1.4 1.4))
    (define base 2.7)
    (if follow-left
        (list (+ base corr) (- base corr))
        (list (- base corr) (+ base corr)))))

(define (compute-roam-action)
  (begin
    (define bias (rng.uniform roam-rng -0.85 0.85))
    (define base 2.6)
    (list (+ base bias) (- base bias))))

(define (on_tick obs)
  (begin
    (define proximity (map.get obs 'proximity (list 0 0 0 0 0 0 0 0)))
    (define min-obstacle (map.get obs 'min_obstacle 1.0))
    (define pmax (clamp (- 1.0 min-obstacle) 0.0 1.0))

    (define right-signal (+ (+ (nth proximity 0) (nth proximity 1)) (nth proximity 2)))
    (define left-signal (+ (+ (nth proximity 5) (nth proximity 6)) (nth proximity 7)))
    (define side-signal (max2 right-signal left-signal))
    (define front-signal (max2 (+ (nth proximity 0) (nth proximity 1))
                               (+ (nth proximity 6) (nth proximity 7))))

    (define collision-imminent (or (> pmax 0.44) (> front-signal 0.60)))
    (define wall-detected (or (> side-signal 0.50) (> front-signal 0.50)))

    (define avoid-action (compute-avoid-action proximity min-obstacle))
    (define wall-action (compute-wall-action proximity))
    (define roam-action (compute-roam-action))

    (define tick-inputs
      (list
        (list 'collision_imminent collision-imminent)
        (list 'wall_detected wall-detected)
        (list 'act_avoid avoid-action)
        (list 'act_wall wall-action)
        (list 'act_roam roam-action)))

    (bt.tick inst tick-inputs)

    (define action-vec (bt.blackboard.get inst 'action_vec roam-action))
    (define left (nth action-vec 0))
    (define right (nth action-vec 1))
    (define branch-id (bt.blackboard.get inst 'active_branch 3))

    (define out (map.make))
    (map.set! out 'schema_version "epuck_demo.v1")
    (map.set! out 'action (make-action left right))
    (map.set! out 'bt (make-bt-map branch-id))
    out))

(env.attach "webots")

(define env-cfg (map.make))
(map.set! env-cfg 'demo "obstacle")
(map.set! env-cfg 'obs_schema "epuck.obstacle.obs.v1")
(map.set! env-cfg 'tick_hz 20)
(map.set! env-cfg 'steps_per_tick 1)
(map.set! env-cfg 'realtime #t)
(env.configure env-cfg)

(define inst (bt.new-instance obstacle-tree))
(bt.export-dot obstacle-tree "out/tree.dot")

(define safe-action (make-action 0.0 0.0))
(define run-cfg (map.make))
(map.set! run-cfg 'tick_hz 20)
(map.set! run-cfg 'max_ticks 3000)
(map.set! run-cfg 'safe_action safe-action)
(map.set! run-cfg 'realtime #t)
(map.set! run-cfg 'log_path "logs/obstacle.jsonl")
(map.set! run-cfg 'schema_version "epuck_demo.v1")

(env.run-loop run-cfg on_tick)
