(begin
  ;; PRM in pure muslisp: 2D point robot with circular obstacles.
  ;; Uses pq.* for roadmap shortest path (Dijkstra).

  (define seed 17)
  (define target-nodes 56)
  (define max-sample-attempts 6000)
  (define k-neighbours 8)
  (define edge-steps 14)
  (define inf 1000000000.0)

  (define x-min 0.0)
  (define x-max 10.0)
  (define y-min 0.0)
  (define y-max 8.0)

  (define start (list 0.7 0.8))
  (define goal (list 9.2 7.1))

  (define (pt-x p)
    (car p))

  (define (pt-y p)
    (car (cdr p)))

  (define (dist2 x1 y1 x2 y2)
    (+ (* (- x1 x2) (- x1 x2))
       (* (- y1 y2) (- y1 y2))))

  (define (distance x1 y1 x2 y2)
    (sqrt (dist2 x1 y1 x2 y2)))

  (define obstacles (vec.make))
  (define (add-obstacle! cx cy r)
    (vec.push! obstacles (list cx cy r)))

  (add-obstacle! 3.0 2.0 1.0)
  (add-obstacle! 5.2 4.6 1.2)
  (add-obstacle! 7.4 2.2 1.0)
  (add-obstacle! 2.5 6.0 0.9)
  (add-obstacle! 7.8 6.1 0.9)

  (define (outside-obstacles? x y idx)
    (if (>= idx (vec.len obstacles))
        #t
        (let ((obs (vec.get obstacles idx)))
          (let ((cx (car obs))
                (cy (car (cdr obs)))
                (r (car (cdr (cdr obs)))))
            (if (< (dist2 x y cx cy) (* r r))
                #f
                (outside-obstacles? x y (+ idx 1)))))))

  (define (state-valid? x y)
    (if (< x x-min)
        #f
        (if (> x x-max)
            #f
            (if (< y y-min)
                #f
                (if (> y y-max)
                    #f
                    (outside-obstacles? x y 0))))))

  (define (edge-valid-loop x1 y1 x2 y2 i)
    (if (> i edge-steps)
        #t
        (let ((t (/ i edge-steps)))
          (let ((x (+ x1 (* t (- x2 x1))))
                (y (+ y1 (* t (- y2 y1)))))
            (if (state-valid? x y)
                (edge-valid-loop x1 y1 x2 y2 (+ i 1))
                #f)))))

  (define (edge-valid? x1 y1 x2 y2)
    (edge-valid-loop x1 y1 x2 y2 0))

  (define (sample-valid-nodes! rng nodes accepted attempts)
    (if (>= accepted target-nodes)
        accepted
        (if (>= attempts max-sample-attempts)
            accepted
            (let ((x (rng.uniform rng x-min x-max))
                  (y (rng.uniform rng y-min y-max)))
              (if (state-valid? x y)
                  (begin
                    (vec.push! nodes (list x y))
                    (sample-valid-nodes! rng nodes (+ accepted 1) (+ attempts 1)))
                  (sample-valid-nodes! rng nodes accepted (+ attempts 1)))))))

  (define (worst-index distv idx worst-idx worst-value)
    (if (>= idx (vec.len distv))
        worst-idx
        (let ((v (vec.get distv idx)))
          (if (> v worst-value)
              (worst-index distv (+ idx 1) idx v)
              (worst-index distv (+ idx 1) worst-idx worst-value)))))

  (define (consider-neighbour-candidate! idxv distv j d)
    (if (< (vec.len idxv) k-neighbours)
        (begin
          (vec.push! idxv j)
          (vec.push! distv d))
        (let ((wi (worst-index distv 1 0 (vec.get distv 0))))
          (if (< d (vec.get distv wi))
              (begin
                (vec.set! idxv wi j)
                (vec.set! distv wi d))
              nil))))

  (define (collect-candidates! nodes i j idxv distv)
    (if (>= j (vec.len nodes))
        nil
        (if (= i j)
            (collect-candidates! nodes i (+ j 1) idxv distv)
            (let ((pi (vec.get nodes i))
                  (pj (vec.get nodes j)))
              (let ((d (distance (pt-x pi) (pt-y pi) (pt-x pj) (pt-y pj))))
                (begin
                  (consider-neighbour-candidate! idxv distv j d)
                  (collect-candidates! nodes i (+ j 1) idxv distv)))))))

  (define (add-edge! adj a b cost)
    (begin
      (vec.push! (map.get adj a nil) (list b cost))
      (vec.push! (map.get adj b nil) (list a cost))
      nil))

  (define (connect-candidates! nodes adj i idxv idx edges)
    (if (>= idx (vec.len idxv))
        edges
        (let ((j (vec.get idxv idx)))
          (if (> j i)
              (let ((pi (vec.get nodes i))
                    (pj (vec.get nodes j)))
                (let ((x1 (pt-x pi))
                      (y1 (pt-y pi))
                      (x2 (pt-x pj))
                      (y2 (pt-y pj)))
                  (let ((d (distance x1 y1 x2 y2)))
                    (if (edge-valid? x1 y1 x2 y2)
                        (begin
                          (add-edge! adj i j d)
                          (connect-candidates! nodes adj i idxv (+ idx 1) (+ edges 1)))
                        (connect-candidates! nodes adj i idxv (+ idx 1) edges)))))
              (connect-candidates! nodes adj i idxv (+ idx 1) edges)))))

  (define (init-adj! adj i n)
    (if (>= i n)
        nil
        (begin
          (map.set! adj i (vec.make))
          (init-adj! adj (+ i 1) n))))

  (define (build-roadmap! nodes adj i edges)
    (if (>= i (vec.len nodes))
        edges
        (let ((idxv (vec.make))
              (distv (vec.make)))
          (begin
            (collect-candidates! nodes i 0 idxv distv)
            (build-roadmap! nodes adj (+ i 1) (connect-candidates! nodes adj i idxv 0 edges))))))

  (define (relax-edges! edgev idx current current-dist pq dist prev)
    (if (>= idx (vec.len edgev))
        nil
        (let ((edge (vec.get edgev idx)))
          (let ((nb (car edge))
                (w (car (cdr edge))))
            (let ((new-dist (+ current-dist w)))
              (begin
                (if (< new-dist (map.get dist nb inf))
                    (begin
                      (map.set! dist nb new-dist)
                      (map.set! prev nb current)
                      (pq.push! pq new-dist nb))
                    nil)
                (relax-edges! edgev (+ idx 1) current current-dist pq dist prev)))))))

  (define (graph-dijkstra-loop goal-id adj pq dist prev closed expansions)
    (if (pq.empty? pq)
        (list #f dist prev expansions)
        (let ((entry (pq.pop! pq)))
          (let ((current-dist (car entry))
                (current (car (cdr entry))))
            (if (map.has? closed current)
                (graph-dijkstra-loop goal-id adj pq dist prev closed expansions)
                (if (> current-dist (+ (map.get dist current inf) 0.000000001))
                    (graph-dijkstra-loop goal-id adj pq dist prev closed expansions)
                    (begin
                      (map.set! closed current #t)
                      (if (= current goal-id)
                          (list #t dist prev expansions)
                          (begin
                            (relax-edges! (map.get adj current (vec.make)) 0 current current-dist pq dist prev)
                            (graph-dijkstra-loop goal-id adj pq dist prev closed (+ expansions 1)))))))))))

  (define (graph-dijkstra adj start-id goal-id)
    (let ((pq (pq.make))
          (dist (map.make))
          (prev (map.make))
          (closed (map.make)))
      (begin
        (map.set! dist start-id 0.0)
        (map.set! prev start-id start-id)
        (pq.push! pq 0.0 start-id)
        (graph-dijkstra-loop goal-id adj pq dist prev closed 0))))

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

  (define (reconstruct-path prev goal-id)
    (let ((rev (vec.make)))
      (begin
        (reconstruct-rev prev goal-id rev)
        (reverse-copy rev (- (vec.len rev) 1) (vec.make)))))

  (define (print-path nodes path idx)
    (if (>= idx (vec.len path))
        nil
        (let ((node-id (vec.get path idx)))
          (let ((pt (vec.get nodes node-id)))
            (begin
              (print (list 'step idx (list (pt-x pt) (pt-y pt))))
              (print-path nodes path (+ idx 1)))))))

  (define rng (rng.make seed))
  (define nodes (vec.make))
  (vec.push! nodes start)
  (vec.push! nodes goal)

  (define accepted (sample-valid-nodes! rng nodes 2 0))

  (define adj (map.make))
  (init-adj! adj 0 accepted)
  (define edge-count (build-roadmap! nodes adj 0 0))

  (define result (graph-dijkstra adj 0 1))
  (define found (car result))
  (define dist (car (cdr result)))
  (define prev (car (cdr (cdr result))))
  (define expansions (car (cdr (cdr (cdr result)))))

  (print (list 'prm2d 'seed seed))
  (print (list 'accepted_nodes accepted 'target_nodes target-nodes))
  (print (list 'edge_count edge-count 'k k-neighbours))
  (print (list 'found found 'expanded_nodes expansions))

  (if found
      (let ((path (reconstruct-path prev 1))
            (cost (map.get dist 1 inf)))
        (begin
          (print (list 'path_len (vec.len path) 'path_cost cost))
          (print-path nodes path 0)))
      (print 'no-path)))
