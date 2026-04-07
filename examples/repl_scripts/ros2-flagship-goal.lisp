; Thin ROS2 wrapper scaffold for the shared v0.5 flagship.
; Intended ownership:
; - load the shared flagship BT
; - derive shared blackboard keys from odometry plus fixed scenario config
; - publish shared command intent through ros2.action.v1

(load "../flagship_wheeled/lisp/contract_helpers.lisp")
(load "../flagship_wheeled/lisp/bt_goal_flagship.lisp")

(define flagship-wrapper-status "draft")

(list 'backend "ros2" 'flagship_wrapper flagship-wrapper-status)
