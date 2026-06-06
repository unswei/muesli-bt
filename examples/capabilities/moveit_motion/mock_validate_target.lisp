(begin
  (define position (map.make))
  (map.set! position 'x 0.40)
  (map.set! position 'y 0.10)
  (map.set! position 'z 0.25)

  (define orientation (map.make))
  (map.set! orientation 'qx 0.0)
  (map.set! orientation 'qy 0.0)
  (map.set! orientation 'qz 0.0)
  (map.set! orientation 'qw 1.0)

  (define target (map.make))
  (map.set! target 'frame "world")
  (map.set! target 'position position)
  (map.set! target 'orientation orientation)

  (define req (map.make))
  (map.set! req 'schema_version "cap.motion.request.v1")
  (map.set! req 'capability "cap.motion.v1")
  (map.set! req 'operation "validate-target")
  (map.set! req 'request_id "motion-example-1")
  (map.set! req 'group "arm")
  (map.set! req 'link "tool0")
  (map.set! req 'target target)
  (map.set! req 'timeout_ms 500)

  (cap.call req))
