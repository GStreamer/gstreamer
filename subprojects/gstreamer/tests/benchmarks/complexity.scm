#!/bin/bash
# -*- scheme -*-
exec guile -s $0 "$@"
!#

;; 	Copyright (C) 2005 Andy Wingo
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
;; Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

;; Quick hack to make some data files that gnuplot can read from
;; complexity. Guile 1.8.

(use-modules (srfi srfi-13)
             (srfi srfi-1)
             (ice-9 optargs)
             (ice-9 popen)
             (ice-9 rdelim))

(define *phases* '(create set run destroy))

(define (read-lines port)
  (let lp ((lines '()))
    (let ((x (read-line port)))
      (if (eof-object? x)
          (begin
            (close-port port)
            (reverse! lines))
          (lp (cons x lines))))))

(define (parse-time str)
  (and (char-numeric? (string-ref str 0))
       (fold (lambda (x ret) (+ (* ret 60) x)) 0
             (map (lambda (x) (with-input-from-string x read))
                  (string-split str #\:)))))

(define (run-test program . args)
  (format #t "; running test: ~a\n" (cons program args))
  (map
   cons
   *phases*
   (filter-map
    parse-time
    (read-lines
     (open-input-pipe
      (string-join (map object->string (cons program args)) " "))))))

(define (seq start stop step)
  (let lp ((n start) (out '()))
    (if (> n stop)
        (reverse! out)
        (lp (+ n step) (cons n out)))))

(define (run-tests n-elements)
  (let lp ((x 1) (out '()))
    (if (> x n-elements)
        (reverse! out)
        (lp (* x 2)
            (acons x (run-test "./complexity" x n-elements) out)))))

(define (output-results results)
  (let ((p (open-output-file "complexity.data")))
    (display "#complexity creation state-change run destroy\n" p)
    (for-each
     (lambda (line)
       (display (car line) p)
       (for-each
        (lambda (t) (format p " ~a" t))
        (map cdr (cdr line)))
       (newline p))
     results)
    (close-port p)))

(output-results
 (apply run-tests (map string->number (cdr (program-arguments)))))
