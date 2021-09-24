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
;; mass-elements. Guile 1.6.

(use-modules (srfi srfi-13)
             (srfi srfi-1)
             (ice-9 optargs)
             (ice-9 popen))

(define *phases* '(create set run destroy))

(define (read-lines port)
  (let lp ((lines '()))
    (let ((x (read-line port)))
      (if (eof-object? x)
          (begin
            (close-port port)
            (reverse! lines))
          (lp (cons x lines))))))

(define (run-test n-identities)
  (format #t "; running test: ~a\n" n-identities)
  (let lp ((in (read-lines
                (open-input-pipe
                 (format #f "./mass-elements ~A"
                         (number->string n-identities)))))
           (out '()))
    (if (null? in)
        (begin
          (or (eq? (length out) 4) (error "Invalid mass_elements output"))
          (map cons *phases* (reverse! out)))
        (let ((line (car in)))
          (if (eqv? (string-ref line 0) #\*)
              (lp (cdr in) out)
              (lp (cdr in)
                  (cons (fold (lambda (x ret) (+ (* ret 60) x)) 0
                              (map (lambda (x) (with-input-from-string x read))
                                   (string-split line #\:)))
                        out)))))))

(define (run-tests start stop step)
  (let lp ((n start) (out '()))
    (if (> n stop)
        (reverse! out)
        (lp (+ n step)
            (acons n (run-test n) out)))))

(define (output-results results)
  (let ((p (open-output-file "mass_elements.data")))
    (display "#num_identities creation state-change run destroy\n" p)
    (let lp ((in results))
      (if (not (null? in))
          (let* ((line (car in))
                 (n (car line)))
            (display n p)
            (for-each
             (lambda (t) (format p " ~a" t))
             (map cdr (cdr line)))
            (newline p)
            (lp (cdr in)))))
    (close-port p)))

(output-results
 (apply run-tests (map string->number (cdr (program-arguments)))))
