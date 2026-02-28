(define goal 1.0)

(define (step x a)
  (let ((a2 (clamp a -1.0 1.0)))
    (let ((x2 (+ x (* 0.25 a2))))
      (- 0.0 (abs (- goal x2))))))

(define (pw-allow? n-visits n-children k alpha)
  (< n-children (* k (exp (* alpha (log (if (< n-visits 1) 1 n-visits)))))))

(define (ucb q n parent-n c)
  (if (= n 0)
      1.0e30
      (+ q (* c (sqrt (/ (log (if (< parent-n 1) 1 parent-n)) n))))))

(define (child.new a)
  (let ((m (map.make)))
    (begin
      (map.set! m 'a a)
      (map.set! m 'n 0)
      (map.set! m 'w 0.0)
      m)))

(define (child.q ch)
  (let ((n (map.get ch 'n 0))
        (w (map.get ch 'w 0.0)))
    (if (= n 0) 0.0 (/ w n))))

(define (node.new)
  (let ((m (map.make)))
    (begin
      (map.set! m 'n 0)
      (map.set! m 'w 0.0)
      (map.set! m 'children (vec.make 4))
      m)))

(define (select-child children i nch parent-n c best best-score)
  (if (>= i nch)
      best
      (let ((ch (vec.get children i)))
        (let ((score (ucb (child.q ch)
                          (map.get ch 'n 0)
                          parent-n
                          c)))
          (if (> score best-score)
              (select-child children (+ i 1) nch parent-n c ch score)
              (select-child children (+ i 1) nch parent-n c best best-score))))))

(define (simulate root x rng c k alpha)
  (let ((n (map.get root 'n 0))
        (children (map.get root 'children nil)))
    (let ((nch (vec.len children)))
      (if (pw-allow? n nch k alpha)
          (let ((a (rng.uniform rng -1.0 1.0)))
            (let ((ch (child.new a))
                  (v (step x a)))
              (begin
                (vec.push! children ch)
                (map.set! ch 'n (+ (map.get ch 'n 0) 1))
                (map.set! ch 'w (+ (map.get ch 'w 0.0) v))
                (map.set! root 'n (+ n 1))
                (map.set! root 'w (+ (map.get root 'w 0.0) v))
                v)))
          (let ((ch (select-child children 0 nch n c nil -1.0e30)))
            (let ((v (step x (map.get ch 'a 0.0))))
              (begin
                (map.set! ch 'n (+ (map.get ch 'n 0) 1))
                (map.set! ch 'w (+ (map.get ch 'w 0.0) v))
                (map.set! root 'n (+ n 1))
                (map.set! root 'w (+ (map.get root 'w 0.0) v))
                v)))))))

(define (search-loop root x rng i iters t0 time-ms c k alpha)
  (if (>= i iters)
      root
      (if (> (- (time.now-ms) t0) time-ms)
          root
          (begin
            (simulate root x rng c k alpha)
            (search-loop root x rng (+ i 1) iters t0 time-ms c k alpha)))))

(define (best-child children i nch best best-n)
  (if (>= i nch)
      best
      (let ((ch (vec.get children i))
            (cn (map.get (vec.get children i) 'n 0)))
        (if (> cn best-n)
            (best-child children (+ i 1) nch ch cn)
            (best-child children (+ i 1) nch best best-n)))))

(define (mcts.search x0 seed iters time-ms)
  (let ((rng (rng.make seed))
        (root (node.new))
        (t0 (time.now-ms))
        (c 1.2)
        (k 2.0)
        (alpha 0.5))
    (begin
      (search-loop root x0 rng 0 iters t0 time-ms c k alpha)
      (let ((children (map.get root 'children nil)))
        (let ((best (best-child children 0 (vec.len children) nil -1)))
          (map.get best 'a 0.0))))))

(define best-action (mcts.search 0.0 42 2000 1000))
(print best-action)
