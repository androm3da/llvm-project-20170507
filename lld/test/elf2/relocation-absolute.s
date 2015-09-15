// RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %S/Inputs/abs.s -o %tabs
// RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t
// RUN: lld -flavor gnu2 %tabs %t -o %tout
// RUN: llvm-objdump -s %tout
// REQUIRES: x86

.global _start
_start:
  movl $abs, %edx

#CHECK:      Contents of section .text:
#CHECK-NEXT:  {{[0-1a-f]+}} ba420000 00
