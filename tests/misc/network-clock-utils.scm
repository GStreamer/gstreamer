;; GStreamer
;; Copyright (C) 2005 Andy Wingo <wingo at pobox.com>

;; This program is free software; you can redistribute it and/or    
;; modify it under the terms of the GNU General Public License as   
;; published by the Free Software Foundation; either version 2 of   
;; the License, or (at your option) any later version.              
;;                                                                  
;; This program is distributed in the hope that it will be useful,  
;; but WITHOUT ANY WARRANTY; without even the implied warranty of   
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    
;; GNU General Public License for more details.                     
;;                                                                  
;; You should have received a copy of the GNU General Public License
;; along with this program; if not, contact:
;;
;; Free Software Foundation           Voice:  +1-617-542-5942
;; 59 Temple Place - Suite 330        Fax:    +1-617-542-2652
;; Boston, MA  02111-1307,  USA       gnu@gnu.org


;;; Commentary:
;;
;; Utilities for the network clock simulator.
;;
;;; Code:


;; General utilities.

(define (iround x)
  (if (inexact? x)
      (inexact->exact (round x))
      x))

(define (filter proc l)
  (cond
   ((null? l) '())
   ((proc (car l)) (cons (car l) (filter proc (cdr l))))
   (else (filter proc (cdr l)))))

(define (sum l)
  (apply + l))

(define (avg . nums)
  (/ (sum nums) (length nums)))

(define (sq x)
  (* x x))

;; Linear least squares.
;;
;; See http://mathworld.wolfram.com/LeastSquaresFitting.html
;; returns (values slope intercept r-squared)

(define (least-squares x y)
  (let ((n (length x)))
    (let ((xbar (apply avg x))
          (ybar (apply avg y)))
      (let ((sxx (- (sum (map sq x)) (* n (sq xbar))))
            (syy (- (sum (map sq y)) (* n (sq ybar))))
            (sxy (- (sum (map * x y)) (* n xbar ybar))))
        (let ((slope (/ sxy sxx)))
          (values
           slope
           (- ybar (* slope xbar))
           (/ (sq sxy) (* sxx syy))))))))

;; Streams: lists with lazy cdrs.

(define-macro (stream-cons kar kdr)
  `(cons ,kar (delay ,kdr)))

(define (stream-cdr stream)
  (force (cdr stream)))

(define (stream-car stream)
  (car stream))

(define (stream-null? stream)
  (null? stream))

(define (stream-ref stream n)
  (if (zero? n)
      (stream-car stream)
      (stream-ref (stream-cdr stream) (1- n))))

(define (stream->list stream n)
  (let lp ((in stream) (out '()) (n n))
    (if (zero? n)
        (reverse! out)
        (lp (stream-cdr in) (cons (stream-car in) out) (1- n)))))

(define (stream-skip stream n)
  (if (zero? n)
      stream
      (stream-skip (stream-cdr stream) (1- n))))

(define (stream-sample stream n)
  (stream-cons (stream-car stream)
               (stream-sample (stream-skip stream n) n)))

(define (stream-map proc . streams)
  (stream-cons (apply proc (map stream-car streams))
               (apply stream-map proc (map stream-cdr streams))))

(define (arithmetic-series start step)
  (stream-cons start (arithmetic-series (+ start step) step)))

(define (scale-stream stream factor)
  (stream-map (lambda (t) (* t factor)) *absolute-time*))

(define (stream-while pred proc . streams)
  (if (apply pred (map stream-car streams))
      (begin
        (apply proc (map stream-car streams))
        (apply stream-while pred proc (map stream-cdr streams)))))

(define (stream-of val)
  (stream-cons val (stream-of val)))

(define (periodic-stream val period)
  (let ((period (iround (max 1 (* *sample-frequency* period)))))
    (let lp ((n 0))
      (if (zero? n)
          (stream-cons val (lp period))
          (stream-cons #f (lp (1- n)))))))


;; Queues with a maximum length.

(define *q-length* 32)

(define (make-q l)
  (cons l (last-pair l)))

(define (q-head q)
  (car q))

(define (q-tail q)
  (car q))

(define (q-push q val)
  (let ((tail (cons val '())))
    (if (null? (q-tail q))
        (make-q tail)
        (let ((l (append! (q-head q) tail)))
          (if (> (length (q-head q)) *q-length*)
              (make-q (cdr (q-head q)))
              q)))))
