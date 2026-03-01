(load "lisp/bt_tag.lisp")

(define (nth xs idx)
  (if (= idx 0)
      (car xs)
      (nth (cdr xs) (- idx 1))))

(define (make-action left right)
  (begin
    (define action (map.make))
    (map.set! action 'action_schema "epuck.action.v1")
    (map.set! action 'u (list left right))
    action))

(define (branch-path branch-id)
  (if (= branch-id 1)
      (list "root" "search")
      (if (= branch-id 2)
          (list "root" "pursue")
          (if (= branch-id 3)
              (list "root" "block")
              (list "root" "recover")))))

(define (make-bt-map branch-id)
  (begin
    (define bt (map.make))
    (define status-by-node (map.make))
    (map.set! bt 'active_path (branch-path branch-id))
    (map.set! status-by-node "root" "running")
    (map.set! status-by-node "branch"
      (if (= branch-id 1)
          "search"
          (if (= branch-id 2)
              "pursue"
              (if (= branch-id 3) "block" "recover"))))
    (map.set! bt 'status_by_node status-by-node)
    bt))

(define (topk-entry->u row)
  (begin
    (define out (map.make))
    (map.set! out 'u (map.get row "action" (list 0.0 0.0)))
    (map.set! out 'visits (map.get row "visits" 0))
    (map.set! out 'q (map.get row "q" 0.0))
    out))

(define (topk->u rows)
  (if (null? rows)
      nil
      (cons (topk-entry->u (car rows)) (topk->u (cdr rows)))))

(define (planner-unused)
  (begin
    (define planner (map.make))
    (map.set! planner 'used #f)
    (map.set! planner 'confidence 0.0)
    planner))

(define (planner-from-meta planner-meta)
  (begin
    (define planner (map.make))
    (map.set! planner 'used #t)
    (map.set! planner 'budget_ms (map.get planner-meta "budget_ms" 0))
    (map.set! planner 'time_used_ms (map.get planner-meta "time_used_ms" 0.0))
    (map.set! planner 'iters (map.get planner-meta "iters" 0))
    (map.set! planner 'root_visits (map.get planner-meta "root_visits" 0))
    (map.set! planner 'root_children (map.get planner-meta "root_children" 0))
    (map.set! planner 'widen_added (map.get planner-meta "widen_added" 0))
    (map.set! planner 'confidence (map.get planner-meta "confidence" 0.0))
    (map.set! planner 'top_k (topk->u (map.get planner-meta "top_k" nil)))
    planner))

(define search-rng (rng.make 919293))

(define (make-search-action)
  (begin
    (define speed (rng.uniform search-rng 1.6 2.3))
    (define direction (rng.uniform search-rng -1.0 1.0))
    (if (> direction 0)
        (list speed (- speed))
        (list (- speed) speed))))

(define (compute-recover-action obstacle-front)
  (begin
    (define turn (+ 2.0 (* 2.0 obstacle-front)))
    (list (- turn) turn)))

(define (compute-block-action evader-bearing)
  (begin
    (define steer (clamp (* 3.0 evader-bearing) -2.4 2.4))
    (define base 1.6)
    (list (clamp (+ base steer) -6.28 6.28)
          (clamp (- base steer) -6.28 6.28))))

(define (compute-pursue-action evader-dist evader-bearing obstacle-front)
  (begin
    (define base (clamp (+ 2.3 (* 1.9 (clamp evader-dist 0.0 1.1))) 1.4 5.2))
    (define steer (clamp (+ (* 3.0 evader-bearing) (* 1.3 obstacle-front)) -2.1 2.1))
    (list (clamp (+ base steer) -6.28 6.28)
          (clamp (- base steer) -6.28 6.28))))

(define (on_tick obs)
  (begin
    (define evader-seen (map.get obs 'evader_seen #f))
    (define evader-dist (map.get obs 'evader_dist 2.0))
    (define evader-bearing (map.get obs 'evader_bearing 0.0))
    (define obstacle-front (map.get obs 'obstacle_front (- 1.0 (map.get obs 'min_obstacle 1.0))))
    (define intercepts (map.get obs 'intercepts 0))

    (define collision-imminent (> obstacle-front 0.52))
    (define block-mode (and evader-seen (< evader-dist 0.16)))

    (define search-action (make-search-action))
    (define recover-action (compute-recover-action obstacle-front))
    (define block-action (compute-block-action evader-bearing))
    (define pursue-action (compute-pursue-action evader-dist evader-bearing obstacle-front))

    (define tick-inputs
      (list
        (list 'collision_imminent collision-imminent)
        (list 'block_mode block-mode)
        (list 'evader_seen evader-seen)
        (list 'act_search search-action)
        (list 'act_recover recover-action)
        (list 'act_block block-action)
        (list 'planner_state (list evader-dist evader-bearing obstacle-front))
        (list 'planner_prior pursue-action)))

    (bt.tick inst tick-inputs)

    (define bt-action (bt.blackboard.get inst 'action_vec search-action))
    (define branch-id (bt.blackboard.get inst 'active_branch 1))

    (define planner-meta-raw (bt.blackboard.get inst 'planner_meta "{}"))
    (define planner-meta (json.decode planner-meta-raw))
    (define planner-confidence (map.get planner-meta "confidence" 0.0))

    (define action-vec
      (if (= branch-id 2)
          (if (< planner-confidence 0.04)
              pursue-action
              bt-action)
          bt-action))

    (define out (map.make))
    (map.set! out 'schema_version "epuck_demo.v1")
    (map.set! out 'action (make-action (nth action-vec 0) (nth action-vec 1)))
    (map.set! out 'bt (make-bt-map branch-id))
    (map.set! out 'planner (if (= branch-id 2) (planner-from-meta planner-meta) (planner-unused)))
    (map.set! out 'done (>= intercepts 5))
    out))

(env.attach "webots")

(define env-cfg (map.make))
(map.set! env-cfg 'demo "tag")
(map.set! env-cfg 'obs_schema "epuck.tag.obs.v1")
(map.set! env-cfg 'tick_hz 20)
(map.set! env-cfg 'steps_per_tick 1)
(map.set! env-cfg 'realtime #t)
(env.configure env-cfg)

(define inst (bt.new-instance tag-tree))
(bt.export-dot tag-tree "out/tree.dot")

(define safe-action (make-action 0.0 0.0))
(define run-cfg (map.make))
(map.set! run-cfg 'tick_hz 20)
(map.set! run-cfg 'max_ticks 3600)
(map.set! run-cfg 'safe_action safe-action)
(map.set! run-cfg 'realtime #t)
(map.set! run-cfg 'log_path "logs/tag.jsonl")
(map.set! run-cfg 'schema_version "epuck_demo.v1")

(env.run-loop run-cfg on_tick)
