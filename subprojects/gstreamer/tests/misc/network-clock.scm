#!/bin/bash
# -*- scheme -*-
exec guile --debug -l $0 -e main -- "$@"
!#

;; GStreamer
;; Copyright (C) 2005 Andy Wingo <wingo at pobox.com>
;;
;; This library is free software; you can redistribute it and/or
;; modify it under the terms of the GNU Lesser General Public
;; License as published by the Free Software Foundation; either
;; version 2.1 of the License, or (at your option) any later version.
;;
;; This library is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;; Lesser General Public License for more details.
;;
;; You should have received a copy of the GNU Lesser General Public
;; License along with this library; if not, write to the Free Software
;; Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA


;;; Commentary:
;;
;; Network clock simulator.
;;
;; Simulates the attempts of one clock to synchronize with another over
;; the network. Packets are sent out with a local timestamp, and come
;; back with the remote time added on to the packet. The remote time is
;; assumed to have been observed at the local time in between sending
;; the query and receiving the reply.
;;
;; The local clock will attempt to adjust its rate and offset by fitting
;; a line to the last N datapoints on hand, by default 32. A better fit,
;; as measured by the correlation coefficient, will result in a longer
;; time before the next query. Bad fits or a not-yet-full set of data
;; will result in many queries in quick succession.
;;
;; The rate and offset are set directly to the slope and intercept from
;; the linear regression. This results in discontinuities in the local
;; time. As clock times must be monotonically increasing, a jump down in
;; time will result instead in time standing still for a while. Smoothly
;; varying the rate such that no discontinuities are present has not
;; been investigated.
;;
;; Implementation-wise, this simulator processes events and calculates
;; times discretely. Times are represented as streams, also known as
;; lazy lists. This is an almost-pure functional simulator. The thing to
;; remember while reading is that stream-cons does not evaluate its
;; second argument, rather deferring that calculation until stream-cdr
;; is called. In that way all times are actually infinite series.
;;
;; Usage: See network-clock.scm --help.
;;
;;; Code:


(use-modules (ice-9 popen))


(load "network-clock-utils.scm")


(define (time->samples t)
  (iround (* t *sample-frequency*)))


(define (schedule-event events e time)
  (let lp ((response-time (time->samples time))
           (stream events))
    (if (zero? response-time)
        (if (not (stream-car stream))
            (stream-cons e (stream-cdr stream))
            (stream-cons (stream-car stream) (lp 0 (stream-cdr stream))))
        (stream-cons (stream-car stream) (lp (1- response-time) (stream-cdr stream))))))

(define (schedule-send-time-query events time)
  (schedule-event events (list 'send-time-query) time))

(define (schedule-time-query events l)
  (schedule-event events (list 'time-query l)
                  (+ *send-delay* (random *send-jitter*))))

(define (schedule-time-response events l r)
  (schedule-event events (list 'time-response l r)
                  (+ *recv-delay* (random *recv-jitter*))))

(define (network-time remote-time local-time events m b x y t)
  (let ((r (stream-car remote-time))
        (l (stream-car local-time))
        (event (stream-car events))
        (events (stream-cdr events)))

    (define (next events m b x y t)
      (stream-cons
       (+ (* m l) b)
       (network-time
        (stream-cdr remote-time) (stream-cdr local-time) events m b x y t)))

    (case (and=> event car)
      ((send-time-query)
       (cond
        ((< (random 1.0) *packet-loss*)
         (debug "; dropped time query: ~a\n" l)
         (print-event 'packet-lost l (+ (* m l) b))
         (next events m b x y (time->samples *timeout*)))
        (else
         (debug "; sending time query: ~a\n" l)
         (print-event 'packet-sent l (+ (* m l) b))
         (next (schedule-time-query events l) m b x y (time->samples *timeout*)))))

      ((time-query)
       (debug "; time query received, replying with ~a\n" r)
       (next (schedule-time-response events (cadr event) r) m b x y (and t (1- t))))

      ((time-response)
       (let ((x (q-push x (avg (cadr event) l)))
             (y (q-push y (caddr event))))
         (call-with-values
             (lambda () (least-squares (q-head x) (q-head y)))
           (lambda (m b r-squared)
             (define (next-time) 
               (max
                (if (< (length (q-head x)) *window-size*)
                    0
                    (/ 1 (- 1 (min r-squared 0.99999)) 1000))
                0.10))
             (debug "; new slope and offset: ~a ~a (~a)\n" m b r-squared)
             (print-event 'packet-observed (avg (cadr event) l) (caddr event))
             (print-event 'packet-received l (+ (* m l) b))
             (next (schedule-send-time-query events (next-time)) m b x y #f)))))

      (else
       (cond
        ((not t)
         ;; not waiting for a response
         (next events m b x y t))
        ((<= t 0)
         ;; we timed out
         (next (schedule-send-time-query events 0.0) m b x y 0))
        (else
         (next events m b x y (1- t))))))))

(define (run-simulation remote-speed local-speed)
  (let ((absolute-time (arithmetic-series 0.0 (/ 1.0 *sample-frequency*)))
        (event-stream (stream-of #f)))
    (let ((remote-time (scale-stream absolute-time remote-speed))
          (local-time (scale-stream absolute-time local-speed)))
      (values
       absolute-time
       remote-time
       local-time
       (network-time
        remote-time
        local-time
        (schedule-send-time-query event-stream 0.0)
        1.0
        (stream-car local-time)
        (make-q (list (stream-car local-time)))
        (make-q (list (stream-car remote-time)))
        #f)))))

(define (print-simulation)
  (display "Absolute time; Remote time; Local time; Network time\n")
  (call-with-values
      (lambda () (run-simulation *remote-rate* *local-rate*))
    (lambda streams
      (apply
       stream-while
       (lambda (a r l n) (<= a *total-time*))
       (lambda (a r l n) (format #t "~a ~a ~a ~a\n" a r l n))
       streams))))

(define (plot-simulation)
  (let ((port (open-output-pipe "./plot-data Network Clock Simulation")))
    (with-output-to-port port
      print-simulation)
    (close-pipe port)))

     
(define-parameter *sample-frequency* 40)
(define-parameter *send-delay* 0.1)
(define-parameter *recv-delay* 0.1)
(define-parameter *packet-loss* 0.01)
(define-parameter *send-jitter* 0.1)
(define-parameter *recv-jitter* 0.1)
(define-parameter *window-size* 32)
(define-parameter *local-rate* 1.0)
(define-parameter *remote-rate* 1.1)
(define-parameter *total-time* 5.0)
(define-parameter *timeout* 1.0)
(define-parameter *debug* #f)
(define-parameter *with-graph* #t)


(define (main args)
  (parse-parameter-arguments (cdr args))
  (if *with-graph*
      (plot-simulation)
      (print-simulation))
  (quit))
