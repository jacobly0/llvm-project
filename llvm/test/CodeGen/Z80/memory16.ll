; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc -mtriple=ez80-code16 < %s | FileCheck %s --check-prefixes=EZ80-CODE16
;; NOTE: These prefixes are unused and the list is autogenerated. Do not add tests below this line:
; EZ80-CODE16: {{.*}}
