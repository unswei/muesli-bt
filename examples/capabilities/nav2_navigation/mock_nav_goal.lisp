(begin
  (define target (map.make))
  (map.set! target 'frame "map")
  (map.set! target 'x 1.0)
  (map.set! target 'y 2.0)
  (map.set! target 'yaw 0.0)

  (define req (map.make))
  (map.set! req 'schema_version "cap.navigation.request.v1")
  (map.set! req 'capability "cap.navigation.v1")
  (map.set! req 'operation "navigate-to-pose")
  (map.set! req 'request_id "nav-example-1")
  (map.set! req 'target target)
  (map.set! req 'timeout_ms 1000)

  (cap.call req))
