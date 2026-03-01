(begin
  ;; Dijkstra shortest path on a weighted grid using the native pq.* builtins.
  ;; This intentionally uses duplicate queue inserts + stale-distance skipping.

  (define width 8)
  (define height 6)
  (define inf 1000000000.0)

  (define (xy->id x y)
    (+ x (* y width)))

  (define id->x (map.make))
  (define id->y (map.make))

  (define (init-coords x y)
    (if (>= y height)
        nil
        (if (>= x width)
            (init-coords 0 (+ y 1))
            (let ((id (xy->id x y)))
              (begin
                (map.set! id->x id x)
                (map.set! id->y id y)
                (init-coords (+ x 1) y))))))

  (init-coords 0 0)

  (define obstacles (map.make))
  (define (block! x y)
    (map.set! obstacles (xy->id x y) #t))

  ;; Wall with a gap and a few additional blocked cells.
  (block! 3 0)
  (block! 3 1)
  (block! 3 3)
  (block! 3 4)
  (block! 5 2)
  (block! 5 3)

  ;; Terrain costs: default 1.0, selected cells are heavier.
  (define terrain (map.make))
  (define (set-cost! x y c)
    (map.set! terrain (xy->id x y) c))

  (set-cost! 1 2 2.5)
  (set-cost! 2 2 2.0)
  (set-cost! 4 4 3.0)
  (set-cost! 6 1 2.2)
  (set-cost! 6 4 2.8)

  (define (cell-cost id)
    (map.get terrain id 1.0))

  (define (reverse-copy src idx dst)
    (if (< idx 0)
        dst
        (begin
          (vec.push! dst (vec.get src idx))
          (reverse-copy src (- idx 1) dst))))

  (define (reconstruct-rev prev current rev)
    (vec.push! rev current)
    (if (map.has? prev current)
        (let ((parent (map.get prev current current)))
          (if (= parent current)
              rev
              (reconstruct-rev prev parent rev)))
        rev))

  (define (reconstruct-path prev goal)
    (let ((rev (vec.make)))
      (begin
        (reconstruct-rev prev goal rev)
        (reverse-copy rev (- (vec.len rev) 1) (vec.make)))))

  (define (relax-neighbour nb current current-dist open dist prev)
    (if (map.has? obstacles nb)
        nil
        (let ((new-dist (+ current-dist (cell-cost nb))))
          (if (< new-dist (map.get dist nb inf))
              (begin
                (map.set! dist nb new-dist)
                (map.set! prev nb current)
                (pq.push! open new-dist nb))
              nil))))

  (define (expand-neighbours current current-dist open dist prev)
    (let ((x (map.get id->x current 0))
          (y (map.get id->y current 0)))
      (begin
        (if (> x 0)
            (relax-neighbour (xy->id (- x 1) y) current current-dist open dist prev)
            nil)
        (if (< (+ x 1) width)
            (relax-neighbour (xy->id (+ x 1) y) current current-dist open dist prev)
            nil)
        (if (> y 0)
            (relax-neighbour (xy->id x (- y 1)) current current-dist open dist prev)
            nil)
        (if (< (+ y 1) height)
            (relax-neighbour (xy->id x (+ y 1)) current current-dist open dist prev)
            nil)
        nil)))

  (define (dijkstra-loop goal open dist prev closed expansions)
    (if (pq.empty? open)
        (list #f dist prev expansions)
        (let ((entry (pq.pop! open)))
          (let ((current-dist (car entry))
                (current (car (cdr entry))))
            (if (map.has? closed current)
                (dijkstra-loop goal open dist prev closed expansions)
                (if (> current-dist (+ (map.get dist current inf) 0.000000001))
                    (dijkstra-loop goal open dist prev closed expansions)
                    (begin
                      (map.set! closed current #t)
                      (if (= current goal)
                          (list #t dist prev expansions)
                          (begin
                            (expand-neighbours current current-dist open dist prev)
                            (dijkstra-loop goal open dist prev closed (+ expansions 1)))))))))))

  (define (dijkstra-search start goal)
    (let ((open (pq.make))
          (dist (map.make))
          (prev (map.make))
          (closed (map.make)))
      (begin
        (map.set! dist start 0.0)
        (map.set! prev start start)
        (pq.push! open 0.0 start)
        (dijkstra-loop goal open dist prev closed 0))))

  (define (result-found? result)
    (car result))

  (define (result-dist result)
    (car (cdr result)))

  (define (result-prev result)
    (car (cdr (cdr result))))

  (define (result-expansions result)
    (car (cdr (cdr (cdr result)))))

  (define (print-path path idx)
    (if (>= idx (vec.len path))
        nil
        (let ((id (vec.get path idx)))
          (begin
            (print (list 'step idx (list (map.get id->x id -1) (map.get id->y id -1))))
            (print-path path (+ idx 1))))))

  (define start (xy->id 0 0))
  (define goal (xy->id 7 5))
  (define result (dijkstra-search start goal))

  (print (list 'dijkstra 'weighted-grid (list width height)))
  (print (list 'start (list 0 0) 'goal (list 7 5)))
  (print (list 'found (result-found? result)))
  (print (list 'expanded_nodes (result-expansions result)))

  (if (result-found? result)
      (let ((path (reconstruct-path (result-prev result) goal))
            (cost (map.get (result-dist result) goal inf)))
        (begin
          (print (list 'total_cost cost))
          (print (list 'path_len (vec.len path)))
          (print-path path 0)))
      (print 'no-path)))
