(begin
  (print 'memoryful-sequence-demo)

  (define seq-tree
    (bt.compile
      '(seq
         (act running-then-success 1)
         (act running-then-success 1))))
  (define mem-tree
    (bt.compile
      '(mem-seq
         (act running-then-success 1)
         (act running-then-success 1))))

  (define seq-inst (bt.new-instance seq-tree))
  (define mem-inst (bt.new-instance mem-tree))

  (print (list 'seq-t1 (bt.tick seq-inst)))
  (print (list 'seq-t2 (bt.tick seq-inst)))
  (print (list 'seq-t3 (bt.tick seq-inst)))
  (print (list 'seq-t4 (bt.tick seq-inst)))

  (print (list 'mem-t1 (bt.tick mem-inst)))
  (print (list 'mem-t2 (bt.tick mem-inst)))
  (print (list 'mem-t3 (bt.tick mem-inst)))

  (print (list 'seq-trace (bt.trace.snapshot seq-inst)))
  (print (list 'mem-trace (bt.trace.snapshot mem-inst))))
