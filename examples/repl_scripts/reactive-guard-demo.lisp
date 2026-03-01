(begin
  (print 'reactive-guard-demo)

  (define tree
    (bt.compile
      '(reactive-seq
         (invert (cond bb-has stop))
         (async-seq
           (act async-sleep-ms 120)
           (act async-sleep-ms 120)))))
  (define inst (bt.new-instance tree))

  (print (list 'tick-1 (bt.tick inst)))
  (print (list 'tick-2-stop (bt.tick inst '((stop #t)))))
  (print (bt.trace.snapshot inst)))
