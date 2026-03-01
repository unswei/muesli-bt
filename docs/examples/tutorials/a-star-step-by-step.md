# A* Tutorial (Step By Step)

This tutorial walks through `examples/repl_scripts/a-star-grid.lisp` in execution order.

Each code block below is copied from the actual script. Read from Step 1 to Step 8 and you will have the full file, with commentary on what each section does.

## Step 1: Grid Setup And Coordinate Indexing

This section creates the grid dimensions, node ID encoding, reverse coordinate maps, and initialises the `id->x`/`id->y` lookup tables.

```lisp
(begin
  ;; Simple A* search over a 2D occupancy grid.
  ;; Nodes are encoded as integer ids: id = x + y * width.

  (define width 8)
  (define height 6)

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
```

## Step 2: Obstacles And Heuristic

This section marks blocked cells and defines Manhattan distance as the A* heuristic.

```lisp
  (define obstacles (map.make))
  (define (block! x y)
    (map.set! obstacles (xy->id x y) #t))

  ;; Wall with a gap plus a few extra blockers.
  (block! 3 0)
  (block! 3 1)
  (block! 3 3)
  (block! 3 4)
  (block! 5 2)
  (block! 5 3)

  (define (heuristic a b)
    (let ((ax (map.get id->x a 0))
          (ay (map.get id->y a 0))
          (bx (map.get id->x b 0))
          (by (map.get id->y b 0)))
      (+ (abs (- ax bx)) (abs (- ay by)))))
```

`heuristic` is admissible for a 4-connected unit-cost grid, so A* remains optimal.

## Step 3: Path Reconstruction Helpers

After the goal is found, these functions backtrack through `came` and return the path in start-to-goal order.

```lisp
  (define (reverse-copy src idx dst)
    (if (< idx 0)
        dst
        (begin
          (vec.push! dst (vec.get src idx))
          (reverse-copy src (- idx 1) dst))))

  (define (reconstruct-rev came current rev)
    (vec.push! rev current)
    (if (map.has? came current)
        (let ((parent (map.get came current current)))
          (if (= parent current)
              rev
              (reconstruct-rev came parent rev)))
        rev))

  (define (reconstruct-path came goal)
    (let ((rev (vec.make)))
      (begin
        (reconstruct-rev came goal rev)
        (reverse-copy rev (- (vec.len rev) 1) (vec.make)))))
```

## Step 4: Open-Set Selection And Removal

This A* version intentionally uses a vector open set. It scans for the lowest `f` score and removes by swap-pop.

```lisp
  (define (best-open-index open f idx best-idx best-score)
    (if (>= idx (vec.len open))
        best-idx
        (let ((node (vec.get open idx)))
          (let ((score (map.get f node 1000000000.0)))
            (if (< score best-score)
                (best-open-index open f (+ idx 1) idx score)
                (best-open-index open f (+ idx 1) best-idx best-score))))))

  (define (pick-current-index open f)
    (if (= (vec.len open) 0)
        -1
        (let ((first (vec.get open 0)))
          (best-open-index open f 1 0 (map.get f first 1000000000.0)))))

  (define (vec-remove-swap! v idx)
    (let ((last-idx (- (vec.len v) 1)))
      (begin
        (if (< idx last-idx)
            (vec.set! v idx (vec.get v last-idx))
            nil)
        (vec.pop! v))))
```

## Step 5: Neighbour Relaxation

`consider-neighbour` implements the A* update rule (`tentative g`, then update `came/g/f` if improved). `expand-neighbours` applies it to four-connected neighbours.

```lisp
  (define (consider-neighbour nb current goal open open-set closed came g f)
    (if (map.has? closed nb)
        nil
        (if (map.has? obstacles nb)
            nil
            (let ((tentative (+ (map.get g current 1000000000.0) 1.0)))
              (if (< tentative (map.get g nb 1000000000.0))
                  (begin
                    (map.set! came nb current)
                    (map.set! g nb tentative)
                    (map.set! f nb (+ tentative (heuristic nb goal)))
                    (if (map.has? open-set nb)
                        nil
                        (begin
                          (vec.push! open nb)
                          (map.set! open-set nb #t))))
                  nil)))))

  (define (expand-neighbours current goal open open-set closed came g f)
    (let ((x (map.get id->x current 0))
          (y (map.get id->y current 0)))
      (begin
        (if (> x 0)
            (consider-neighbour (xy->id (- x 1) y) current goal open open-set closed came g f)
            nil)
        (if (< (+ x 1) width)
            (consider-neighbour (xy->id (+ x 1) y) current goal open open-set closed came g f)
            nil)
        (if (> y 0)
            (consider-neighbour (xy->id x (- y 1)) current goal open open-set closed came g f)
            nil)
        (if (< (+ y 1) height)
            (consider-neighbour (xy->id x (+ y 1)) current goal open open-set closed came g f)
            nil)
        nil)))
```

## Step 6: Main A* Loop

This loop keeps selecting the current node, stopping on empty open set (failure) or goal (success), otherwise expanding and recurring.

```lisp
  (define (astar-loop goal open open-set closed came g f expansions)
    (if (= (vec.len open) 0)
        (list #f (vec.make) expansions)
        (let ((idx (pick-current-index open f)))
          (let ((current (vec.get open idx)))
            (begin
              (vec-remove-swap! open idx)
              (map.del! open-set current)
              (if (= current goal)
                  (list #t (reconstruct-path came current) expansions)
                  (begin
                    (map.set! closed current #t)
                    (expand-neighbours current goal open open-set closed came g f)
                    (astar-loop goal open open-set closed came g f (+ expansions 1)))))))))
```

## Step 7: Search Wrapper And Result Accessors

`astar-search` initialises all working structures and starts the loop. The small accessor helpers make the result tuple easier to consume.

```lisp
  (define (astar-search start goal)
    (let ((open (vec.make))
          (open-set (map.make))
          (closed (map.make))
          (came (map.make))
          (g (map.make))
          (f (map.make)))
      (begin
        (vec.push! open start)
        (map.set! open-set start #t)
        (map.set! came start start)
        (map.set! g start 0.0)
        (map.set! f start (heuristic start goal))
        (astar-loop goal open open-set closed came g f 0))))

  (define (result-found? result)
    (car result))

  (define (result-path result)
    (car (cdr result)))

  (define (result-expansions result)
    (car (cdr (cdr result))))
```

## Step 8: Run And Print Output

The final section chooses start/goal, executes A*, and prints either a full path or `no-path`.

```lisp
  (define (print-path path idx)
    (if (>= idx (vec.len path))
        nil
        (let ((id (vec.get path idx)))
          (begin
            (print (list 'step idx (list (map.get id->x id -1) (map.get id->y id -1))))
            (print-path path (+ idx 1))))))

  (define start (xy->id 0 0))
  (define goal (xy->id 7 5))
  (define result (astar-search start goal))

  (print (list 'a-star 'grid (list width height)))
  (print (list 'start (list 0 0) 'goal (list 7 5)))
  (print (list 'found (result-found? result)))
  (print (list 'expanded_nodes (result-expansions result)))

  (if (result-found? result)
      (let ((path (result-path result)))
        (begin
          (print (list 'path_len (vec.len path)))
          (print-path path 0)))
      (print 'no-path)))
```

## See Also

- [A* Example Overview](../a-star-search.md)
- [Dijkstra Tutorial](dijkstra-step-by-step.md)

## Full Source

```lisp
--8<-- "examples/repl_scripts/a-star-grid.lisp"
```
