(begin
  (define problem (map.make))
  (map.set! problem 'domain "pick-place")
  (map.set! problem 'object "block-a")
  (map.set! problem 'goal "placed")

  (define req (map.make))
  (map.set! req 'schema_version "cap.tamp.request.v1")
  (map.set! req 'capability "cap.tamp.v1")
  (map.set! req 'operation "solve")
  (map.set! req 'request_id "tamp-example-1")
  (map.set! req 'planner "pddlstream-pybullet")
  (map.set! req 'problem problem)
  (map.set! req 'timeout_ms 2000)

  (cap.call req))
