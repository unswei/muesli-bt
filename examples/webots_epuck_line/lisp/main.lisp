(load "lisp/bt_line_follow.lisp")

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
      (list "root" "line_lost" "search_line")
      (if (= branch-id 2)
          (list "root" "line_found" "plan_follow")
          (list "root" "fallback_search"))))

(define (make-bt-map branch-id)
  (begin
    (define bt (map.make))
    (define status-by-node (map.make))
    (map.set! bt 'active_path (branch-path branch-id))
    (map.set! status-by-node "root" "running")
    (map.set! status-by-node "branch" (if (= branch-id 1) "search" (if (= branch-id 2) "follow" "fallback")))
    (map.set! bt 'status_by_node status-by-node)
    bt))

(define search-rng (rng.make 616161))

(define (make-search-action)
  (begin
    (define speed (rng.uniform search-rng 1.6 2.2))
    (define direction (rng.uniform search-rng -1.0 1.0))
    (if (> direction 0)
        (list speed (- speed))
        (list (- speed) speed))))

(define (compute-line-features ground)
  (begin
    (define g-left (nth ground 0))
    (define g-centre (nth ground 1))
    (define g-right (nth ground 2))

    (define d-left (clamp (- 1.0 g-left) 0.0 1.0))
    (define d-centre (clamp (- 1.0 g-centre) 0.0 1.0))
    (define d-right (clamp (- 1.0 g-right) 0.0 1.0))
    (define d-sum (+ (+ d-left d-centre) d-right))

    (define error (if (> d-sum 0.000001) (/ (- d-right d-left) d-sum) 0.0))
    (define strength (if (> d-left d-right) (if (> d-left d-centre) d-left d-centre) (if (> d-right d-centre) d-right d-centre)))
    (list error strength)))

(define (compute-follow-action line-error line-strength)
  (begin
    (define base (+ 2.7 (* 1.0 line-strength)))
    (define steer (clamp (* 3.8 line-error) -1.2 1.2))
    (list (clamp (+ base steer) -6.28 6.28)
          (clamp (- base steer) -6.28 6.28))))

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

(define (on_tick obs)
  (begin
    (define ground (map.get obs 'ground (list 1.0 1.0 1.0)))
    (define features (compute-line-features ground))
    (define line-error (nth features 0))
    (define line-strength (nth features 1))
    (define line-lost (< line-strength 0.08))
    (define line-found (if line-lost #f #t))
    (define search-action (make-search-action))
    (define follow-action (compute-follow-action line-error line-strength))

    (define tick-inputs
      (list
        (list 'line_lost line-lost)
        (list 'line_found line-found)
        (list 'act_search search-action)
        (list 'act_follow follow-action)
        (list 'planner_state (list line-error line-strength))
        (list 'planner_prior follow-action)))

    (bt.tick inst tick-inputs)

    (define bt-action (bt.blackboard.get inst 'action_vec search-action))
    (define branch-id (bt.blackboard.get inst 'active_branch 3))

    (define planner-meta-raw (bt.blackboard.get inst 'planner_meta "{}"))
    (define planner-meta (json.decode planner-meta-raw))
    (define planner-confidence (map.get planner-meta "confidence" 0.0))

    (define action-vec
      (if (= branch-id 2)
          (if (< planner-confidence 0.05)
              follow-action
              bt-action)
          bt-action))

    (define left (nth action-vec 0))
    (define right (nth action-vec 1))

    (define out (map.make))
    (map.set! out 'schema_version "epuck_demo.v1")
    (map.set! out 'action (make-action left right))
    (map.set! out 'bt (make-bt-map branch-id))
    (map.set! out 'planner (if (= branch-id 2) (planner-from-meta planner-meta) (planner-unused)))
    out))

(env.attach "webots")

(define env-cfg (map.make))
(map.set! env-cfg 'demo "line")
(map.set! env-cfg 'obs_schema "epuck.line.obs.v1")
(map.set! env-cfg 'tick_hz 20)
(map.set! env-cfg 'steps_per_tick 1)
(map.set! env-cfg 'realtime #t)
(env.configure env-cfg)

(define inst (bt.new-instance line-tree))
(bt.export-dot line-tree "out/tree.dot")

(define safe-action (make-action 0.0 0.0))
(define run-cfg (map.make))
(map.set! run-cfg 'tick_hz 20)
(map.set! run-cfg 'max_ticks 3000)
(map.set! run-cfg 'safe_action safe-action)
(map.set! run-cfg 'realtime #t)
(map.set! run-cfg 'log_path "logs/line.jsonl")
(map.set! run-cfg 'schema_version "epuck_demo.v1")

(env.run-loop run-cfg on_tick)
