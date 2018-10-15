;	Header to compile Altirra MathPack as standalone binary
;

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Default mathpack equates
fr0     = $00d4    ;FP: Accumulator 0
_fr3    = $00da    ;FP: Accumulator 3 (officially FRE)
fr1     = $00e0    ;FP: Accumulator 1
fr2     = $00e6    ;FP: Accumulator 2
_fpcocnt= $00ec    ;FP: temporary storage - polynomial coefficient counter
_fptemp0= $00ed    ;FP: temporary storage - transcendental temporary (officially EEXP)
_fptemp1= $00ee    ;FP: temporary storage - transcendental temporary (officially NSIGN)

cix     = $00f2    ;FP: Character index
inbuff  = $00f3    ;FP: ASCII conversion buffer
        ; $00f4
        ; $00f5    ;FP: temporary storage -- also temporarily used by BASIC power routine
        ; $00f6    ;FP: temporary storage
ztemp4  = $00f7    ;FP: temporary storage -- also temporarily used by BASIC power routine
        ; $00f8    ;FP: temporary storage
        ; $00f9    ;FP: temporary storage
        ; $00fa    ;FP: temporary storage

flptr   = $00fc    ;FP: pointer for floating-point loads and stores
fptr2   = $00fe    ;FP: pointer for polynomial evaluation

lbuff   = $0580    ; $60 bytes
plyarg  = $05e0    ;FP: Polynomial evaluation temp register
fpscr   = $05e6    ;FP: Temp evaluation register (used by LOG/LOG10)

; Compile as a single header-less binary
        opt     f+h-
; Include mathpack sources
        icl     'mathpack.s'

