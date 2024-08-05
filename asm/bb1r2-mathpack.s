	OPT	F+H-

;	SPACE	4,10
;***	Copyright 1984 ATARI.  Unauthorized reproduction, adaptation,
;*	distribution, performance or display of this computer program
;*	or the associated audiovisual work is strictly prohibited.

;	SPACE	4,10
;***	OS - Operating System
;*
;*		Revision A (400/800)
;*		D. Crane/A. Miller/L. Kaplan/R. Whitehead
;*

;	SPACE	4,10
;**	Floating Point Package Miscellaneous Equates

FPREC	EQU	6	;precision

FMPREC	EQU	FPREC-1	;length of mantissa

;	SPACE	4,10
;**	Floating Point Package Page Zero Address Equates


FR0	EQU	$00D4	;6-byte register 0
FR0M	EQU	$00D5	;5-byte register 0 mantissa
QTEMP	EQU	$00D9	;1-byte temporary

FRE	EQU	$00DA	;6-byte (internal) register E

FR1	EQU	$00E0	;6-byte register 1
FR1M	EQU	$00E1	;5-byte register 1 mantissa

FR2	EQU	$00E6	;6-byte (internal) register 2

FRX	EQU	$00EC	;1-byte temporary

EEXP	EQU	$00ED	;1-byte value of exponent

FRSIGN	EQU	$00EE	;1-byte floating point sign
NSIGN	EQU	$00EE	;1-byte sign of number

PLYCNT	EQU	$00EF	;1-byte polynomial degree
ESIGN	EQU	$00EF	;1-byte sign of exponent

SGNFLG	EQU	$00F0	;1-byte sign flag
FCHFLG	EQU	$00F0	;1-byte first character flag

XFMFLG	EQU	$00F1	;1-byte transform flag
DIGRT	EQU	$00F1	;1-byte number of digits after decimal point

CIX	EQU	$00F2	;1-byte current input index
INBUFF	EQU	$00F3	;2-byte line input buffer

ZTEMP1	EQU	$00F5	;2-byte temporary
ZTEMP4	EQU	$00F7	;2-byte temporary
ZTEMP3	EQU	$00F9	;2-byte temporary

FLPTR	EQU	$00FC	;2-byte floating point number pointer
FPTR2	EQU	$00FE	;2-byte floating point number pointer

;	SPACE	4,10
;**	Page Five Address Equates


;	Reserved for Application and Floating Point Package

;	EQU	$0500	;256 bytes reserved for application and FPP
;	SPACE	4,10
;**	Floating Point Package Address Equates


LBPR1	EQU	$057E	;1-byte LBUFF preamble
LBPR2	EQU	$057F	;1-byte LBUFF preamble
LBUFF	EQU	$0580	;128-byte line buffer

PLYARG	EQU	$05E0	;6-byte floating point polynomial argument
FPSCR	EQU	$05E6	;6-byte floating point temporary
FPSCR1	EQU	$05EC	;6-byte floating point temporary

;	SPACE	4,10
;**	Floating Point Package Address Equates

AFP	EQU	$D800	;convert ASCII to floating point
FASC	EQU	$D8E6	;convert floating point to ASCII
IFP	EQU	$D9AA	;convert integer to floating point
FPI	EQU	$D9D2	;convert floating point to integer
ZFR0	EQU	$DA44	;zero FR0
ZF1	EQU	$DA46	;zero floating point number
FSUB	EQU	$DA60	;subtract floating point numbers
FADD	EQU	$DA66	;add floating point numbers
FMUL	EQU	$DADB	;multiply floating point numbers
FDIV	EQU	$DB28	;divide floating point numbers
PLYEVL	EQU	$DD40	;evaluate floating point polynomial
FLD0R	EQU	$DD89	;load floating point number
FLD0P	EQU	$DD8D	;load floating point number
FLD1R	EQU	$DD98	;load floating point number
FLD1P	EQU	$DD9C	;load floating point number
FST0R	EQU	$DDA7	;store floating point number
FST0P	EQU	$DDAB	;store floating point number
FMOVE	EQU	$DDB6	;move floating point number
LOG	EQU	$DECD	;calculate floating point logarithm
LOG10	EQU	$DED1	;calculate floating point base 10 logarithm
EXP	EQU	$DDC0	;calculate floating point exponentiation
EXP10	EQU	$DDCC	;calculate floating point base 10 exponentiation

;	SUBTTL	'Macro Definitions'
;	SPACE	4,10
;**	FIX - Fix Address
;*
;*	FIX sets the origin counter to the value specified as an
;*	argument.  If the current origin counter is less than the
;*	argument, FIX fills the intervening bytes with zero and
;*	issues a message to document the location and number of
;*	bytes that are zero filled.
;*
;*	ENTRY	FIX	address
;*
;*
;*	EXIT
;*		Origin counter set to specified address.
;*		Message issued if zero fill required.
;*
;*	CHANGES
;*		-none-
;*
;*	CALLS
;*		-none-
;*
;*	NOTES
;*		If the current origin counter value is beyond the
;*		argument, FIX generates an error.
;*
;*	MODS
;*		R. K. Nordin	11/01/83


.macro	FIX	address
	.if	* > :address
	.echo	:address, " precedes current origin counter of ", *
	.error	"address precedes current origin counter"
	.elseif * < :address
	.echo	(:address - *), " free bytes from ", *, " to ", (:address - 1)
	:(:address-*)	.BYTE	0
	.endif
.endm

;	SUBTTL	'Floating Point Package'
;	SPACE	4,10
;***	(C) Copyright 1978 Shepardson Microsystems, Inc.
;	SPACE	4,10
	ORG	$D800
;	SPACE	4,10
;***	FPP - Floating Point Package
;*
;*	FPP is a collection of routines for floating point
;*	computations.  A floating point number is represented
;*	in 6 bytes:
;*
;*	Byte 0
;*		Bit 7		Sign of mantissa
;*		Bits 0 - 6	BCD exponent, biased by $40
;*
;*	Bytes 1 - 5		BCD mantissa
;*
;*	MODS
;*		Shepardson Microsystems
;*
;*		Produce 2K version.
;*		M. Lorenzen	09/06/81
;	SPACE	4,10
	FIX	AFP
;	SPACE	4,10
;**	AFP - Convert ASCII to Floating Point
;*
;*	ENTRY	JSR	AFP
;*		INBUFF = line buffer pointer
;*		CIX = offset to first byte of number
;*
;*	EXIT
;*		C clear, if valid number
;*		C set, if invalid number
;*
;*	NOTES
;*		Problem: bytes wasted by check for "-", near AFP7.
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;AFP	=	*		;entry

;	Initialize.

	JSR	SLB	;skip leading blanks

;	Check for number.

	JSR	TVN	;test for valid number character
	BCS	AFP5	;if not number character

;	Set initial values.

	LDX	#EEXP	;exponent
	LDY	#4	;indicate 4 bytes to clear
	JSR	ZXLY
	LDX	#$FF
	STX	DIGRT	;number of digits after decimal point
	JSR	ZFR0	;zero FR0
	BEQ	AFP2	;get first character

;	Indicate not first character.

AFP1	LDA	#$FF	;indicate not first character
	STA	FCHFLG	;first character flag

;	Get next character.

AFP2	JSR	GNC	;get next character
	BCS	AFP6	;if character not numeric

;	Process numeric character.

	PHA			;save digit
	LDX	FR0M		;first byte
	BNE	AFP3		;if not zero

	JSR	S0L		;shift FR0 left 1 digit
	PLA			;saved digit
	ORA	FR0M+FMPREC-1	;insert into last byte
	STA	FR0M+FMPREC-1	;update last byte

;	Check for decimal point.

	LDX	DIGRT	;number of digits after decimal point
	BMI	AFP1	;if no decimal point, process next character

;	Increment number of digits after decimal point.

	INX		;increment number of digits
	STX	DIGRT	;number of digits after decimal point
	BNE	AFP1	;process next character

;	Increment exponent, if necessary.

AFP3	PLA		;clean stack
	LDX	DIGRT	;number of digits after decimal point
	BPL	AFP4	;if already have decimal point

	INC	EEXP	;increment number of digits more than 9

;	Process next character.

AFP4	JMP	AFP1	;process next character

;	Exit.

AFP5	RTS		;return

;	Process non-numeric character.

AFP6	CMP	#'.'
	BEQ	AFP8	;if ".", process decimal point

	CMP	#'E'
	BEQ	AFP9	;if "E", process exponent

	LDX	FCHFLG	;first character flag
	BNE	AFP16	;if not first character, process end of input

	CMP	#'+'
	BEQ	AFP1	;if "+", process next character

	CMP	#'-'
	BEQ	AFP7	;if "-", process negative sign

;	Process negative sign.

AFP7	STA	NSIGN	;sign of number
	BEQ	AFP1	;process next character

;	Process decimal point.

AFP8	LDX	DIGRT	;number of digits after decimal point
	BPL	AFP16	;if already have decimal point

	INX		;zero
	STX	DIGRT	;number of digits after decimal point
	BEQ	AFP1	;process next character

;	Process exponent.

AFP9	LDA	CIX	;offset to character
	STA	FRX	;save offset to character
	JSR	GNC	;get next character
	BCS	AFP13	;if not numeric

;	Process numeric character in exponent.

AFP10	TAX		;first character of exponent
	LDA	EEXP	;number of digits more than 9
	PHA		;save number of digits more than 9
	STX	EEXP	;first character of exponent

;	Process second character of exponent.

	JSR	GNC	;get next character
	BCS	AFP11	;if not numeric, no second digit

	PHA		;save second digit
	LDA	EEXP	;first digit
	ASL	 	;2 times first digit
	STA	EEXP	;2 times first digit
	ASL	 	;4 times first digit
	ASL	 	;8 times first digit
	ADC	EEXP	;add 2 times first digit
	STA	EEXP	;save 10 times first digit
	PLA		;saved second digit
	CLC
	ADC	EEXP	;insert in exponent
	STA	EEXP	;update exponent

;	Process third character of exponent???

	LDY	CIX	;offset to third character
	JSR	ICX	;increment offset

AFP11	LDA	ESIGN	;sign of exponent
	BEQ	AFP12	;if no sign on exponent

;	Process negative exponent.

	LDA	EEXP	;exponent
	EOR	#$FF	;complement exponent
	CLC
	ADC	#1	;add 1 for 2's complement
	STA	EEXP	;update exponent

;	Add in number of digits more than 9.

AFP12	PLA		;saved number of digits more than 9
	CLC
	ADC	EEXP	;add exponent
	STA	EEXP	;update exponent
	BNE	AFP16	;process end of input

;	Process non-numeric in exponent.

AFP13	CMP	#'+'
	BEQ	AFP14	;if "+", process next character

	CMP	#'-'
	BNE	AFP15	;if not "-", ???

	STA	ESIGN	;save sign of exponent

;	Process next character.

AFP14	JSR	GNC	;get next character
	BCC	AFP10	;if numeric, process numeric character

;	Process other non-numeric in exponent.

AFP15	LDA	FRX	;saved offset
	STA	CIX	;restore offset

;	Process end of input.

AFP16	DEC	CIX	;decrement offset
	LDA	EEXP	;exponent
	LDX	DIGRT	;number of digits after decimal point
	BMI	AFP17	;if no decimal point

	BEQ	AFP17	;if no digits after decimal point

	SEC
	SBC	DIGRT	;subtract number of digits after decimal point

AFP17	PHA		;save adjusted exponent
	ROL		;set C with sign of exponent
	PLA		;saved adjusted exponent
	ROR		;shift right
	STA	EEXP	;save power of 100
	BCC	AFP18	;if no carry, process even number

	JSR	S0L	;shift FR0 left 1 digit

AFP18	LDA	EEXP	;exponent
	CLC
	ADC	#$40+4	;add bias plus 4 for normalization
	STA	FR0	;save exponent

	JSR	NORM	;normalize number
	BCS	AFP20	;if error

;	Check sign of number.

	LDX	NSIGN	;sign of number
	BEQ	AFP19	;if sign of number not negative

;	Process negative number.

	LDA	FR0	;first byte of mantissa
	ORA	#$80	;indicate negative
	STA	FR0	;update first byte of mantissa

;	Exit.

AFP19	CLC		;indicate valid number

AFP20	RTS		;return
;	SPACE	4,10
	FIX	FASC
;	SPACE	4,10
;**	FASC - Convert Floating Point Number to ASCII
;*
;*	ENTRY	JSR	FASC
;*		FR0 - FR0+5 = number to convert
;*
;*	EXIT
;*		INBUFF = pointer to start of number
;*		High order bit of last charecter set
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;FASC	=	*	;entry

;	Initialize.

	JSR	ILP	;initialize line buffer pointer
	LDA	#'0'
	STA	LBPR2	;put "0" in front of line buffer

;	Check for E format required.

	LDA	FR0	;exponent
	BEQ	FASC2	;if exponent zero, number zero

	AND	#$7F	;clear sign
	CMP	#$40-1	;bias-1
	BCC	FASC3	;if exponent < bias-1, E format required

	CMP	#$40+5	;bias+5
	BCS	FASC3	;if >= bias+5, E format required

;	Process E format not required.

	SEC
	SBC	#$40-1	;subtract bias-1, yielding decimal position
	JSR	C0A	;convert FR0 to ASCII
	JSR	FNZ	;find last non-zero character
	ORA	#$80	;set high order bit
	STA	LBUFF,X	;update last character
	LDA	LBUFF	;first character
	CMP	#'.'
	BEQ	FASC1	;if decimal point

	JMP	FASC10

FASC1	JSR	DLP	;decrement line buffer pointer
	JMP	FASC11	;perform final adjustment

;	Process zero.

FASC2	LDA	#$80+'0'	;"0" with high order bit set
	STA	LBUFF		;put zero character in line buffer
	RTS			;return

;	Process E format required.

FASC3	LDA	#1	;GET DECIMAL POSITION???
	JSR	C0A	;convert FR0 to ASCII
	JSR	FNZ	;find last non-zero character
	INX		;increment offset to last character
	STX	CIX	;save offset to last character

;	Adjust exponent.

	LDA	FR0	;exponent
	ASL		;double exponent
	SEC
	SBC	#$40*2	;subtract 2 times bias

;	Check first character for "0".

	LDX	LBUFF	;first character
	CPX	#'0'
	BEQ	FASC5	;if "0"

;	Put decimal after first character.

	LDX	LBUFF+1	;second character
	LDY	LBUFF+2	;decimal point
	STX	LBUFF+2	;decimal point
	STY	LBUFF+1	;third character
	LDX	CIX	;offset
	CPX	#2	;former offset to decimal point
	BNE	FASC4	;if offset pointed to second character

	INC	CIX	;increment offset

FASC4	CLC
	ADC	#1	;adjust exponent for movement of decimal point

;	Convert exponent to ASCII.

FASC5	STA	EEXP	;exponent
	LDA	#'E'
	LDY	CIX	;offset
	JSR	SAL	;store ASCII character in line buffer
	STY	CIX	;save offset
	LDA	EEXP	;exponent
	BPL	FASC6	;if exponent positive

	LDA	#0
	SEC
	SBC	EEXP	;complement exponent
	STA	EEXP	;update exponent
	LDA	#'-'
	BNE	FASC7	;store "-"

FASC6	LDA	#'+'

FASC7	JSR	SAL	;store ASCII character in line buffer
	LDX	#0	;initial number of 10's
	LDA	EEXP	;exponent

FASC8	SEC
	SBC	#10	;subtract 10
	BCC	FASC9	;if < 0, done

	INX		;increment number of 10's
	BNE	FASC8	;continue

FASC9	CLC
	ADC	#10	;add back 10
	PHA		;save remainder
	TXA		;number of 10's
	JSR	SNL	;store number in line buffer
	PLA		;saved remainder
	ORA	#$80	;set high order bit
	JSR	SNL	;store number in line buffer

;	Perform final adjustment.

FASC10	LDA	LBUFF	;first character
	CMP	#'0'
	BNE	FASC11	;if not "0", ???

;	Increment pointer to point to non-zero character.

	CLC
	LDA	INBUFF		;line buffer pointer
	ADC	#1		;add 1
	STA	INBUFF		;update line buffer pointer
	LDA	INBUFF+1
	ADC	#0
	STA	INBUFF+1

;	Check for positive exponent.

FASC11	LDA	FR0		;exponent
	BPL	FASC12		;if exponent positive, exit

;	Process negative exponent.

	JSR	DLP		;decrement line buffer pointer
	LDY	#0		;offset to first character
	LDA	#'-'
	STA	(INBUFF),Y	;put "-" in line buffer

;	Exit.

FASC12	RTS			;return
;	SPACE	4,10
	FIX	IFP
;	SPACE	4,10
;**	IFP - Convert Integer to Floating Point Number
;*
;*	ENTRY	JSR	IFP
;*		FR0 - FR0+1 = integer to convert
;*
;*	EXIT
;*		FR0 - FR0+5 = floating point number
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;IFP	=	*	;entry

;	Initialize.

	LDA	FR0		;low integer
	STA	ZTEMP4+1	;save low integer
	LDA	FR0+1		;high integer
	STA	ZTEMP4		;save high integer
	JSR	ZFR0		;zero FR0

;	Convert to floating point.

	SED
	LDY	#16		;number of bits in integer

IFP1	ASL	ZTEMP4+1	;shift integer
	ROL	ZTEMP4		;shift integer, setting C if bit present

	LDX	#3		;offset to last possible byte of number

IFP2	LDA	FR0,X		;byte of number
	ADC	FR0,X		;double byte, adding in carry
	STA	FR0,X		;update byte of number
	DEX
	BNE	IFP2		;if not done

	DEY			;decrement count of integer bits
	BNE	IFP1		;if not done

	CLD

;	Set exponent.

	LDA	#$40+2		;indicate decimal after last digit
	STA	FR0		;exponent

;	Exit.

	JMP	NORM		;normalize, return
;	SPACE	4,10
	FIX	FPI
;	SPACE	4,10
;**	FPI - Convert Floating Point Number to Integer
;*
;*	ENTRY	JSR	FPI
;*		FR0 - FR0+5 = floating point number
;*
;*	EXIT
;*		C set, if error
;*		C clear, if no error
;*		FR0 - FR0+1 = integer
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;FPI	=	*		;entry

;	Initialize.

	LDA	#0
	STA	ZTEMP4		;zero integer
	STA	ZTEMP4+1

;	Check exponent.

	LDA	FR0		;exponent
	BMI	FPI4		;if sign of exponent is negative, error

	CMP	#$40+3		;bias+3
	BCS	FPI4		;if number too big, error

	SEC
	SBC	#$40		;subtract bias
	BCC	FPI2		;if number less than 1, test for round

;	Compute number of digits to convert.

	ADC	#0		;add carry
	ASL			;2 times exponent-$40+1
	STA	ZTEMP1		;number of digits to convert

;	Convert.

FPI1	JSR	SIL		;shift integer left
	BCS	FPI4		;if number too big, error

	LDA	ZTEMP4		;2 times integer
	STA	ZTEMP3		;save 2 times integer
	LDA	ZTEMP4+1
	STA	ZTEMP3+1
	JSR	SIL		;shift integer left
	BCS	FPI4		;if number too big, error

	JSR	SIL		;shift integer left
	BCS	FPI4		;if number too big, error

	CLC
	LDA	ZTEMP4+1	;8 times integer
	ADC	ZTEMP3+1	;add 2 times integer
	STA	ZTEMP4+1	;10 times integer
	LDA	ZTEMP4
	ADC	ZTEMP3
	STA	ZTEMP4
	BCS	FPI4		;if overflow???, error

	JSR	GND		;get next digit
	CLC
	ADC	ZTEMP4+1	;insert digit in ???
	STA	ZTEMP4+1	;update ???
	LDA	ZTEMP4		;???
	ADC	#0		;add carry
	BCS	FPI4		;if overflow, error

	STA	ZTEMP4		;update ???
	DEC	ZTEMP1		;decrement count of digits to convert
	BNE	FPI1		;if not done

;	Check for round required.

FPI2	JSR	GND		;get next digit
	CMP	#5
	BCC	FPI3		;if digit less than 5, do not round

;	Round.

	CLC
	LDA	ZTEMP4+1
	ADC	#1		;add 1 to round
	STA	ZTEMP4+1
	LDA	ZTEMP4
	ADC	#0
	STA	ZTEMP4

;	Return integer.

FPI3	LDA	ZTEMP4+1	;low integer
	STA	FR0		;low integer result
	LDA	ZTEMP4		;high integer
	STA	FR0+1		;high integer result
	CLC			;indicate success
	RTS			;return

;	Return error.

FPI4	SEC			;indicate error
	RTS			;return
;	SPACE	4,10
	FIX	ZFR0
;	SPACE	4,10
;**	ZFR0 - Zero FR0
;*
;*	ENTRY	JSR	ZFR0
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;ZFR0	=	*	;entry

	LDX	#FR0	;indicate zero FR0
;	JMP	ZF1	;zero floating point number, return
;	SPACE	4,10
	FIX	ZF1
;	SPACE	4,10
;**	ZF1 - Zero Floating Point Number
;*
;*	ENTRY	JSR	ZF1
;*		X = offset to register
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;ZF1	=	*	;entry

	LDY	#6	;number of bytes to zero
;	JMP	ZXLY	;zero bytes, return
;	SPACE	4,10
;**	ZXLY - Zero Page Zero Location X for Length Y
;*
;*	ENTRY	JSR	ZXLY
;*		X = offset
;*		Y = length
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


ZXLY	=	*	;entry

	LDA	#0

ZXLY1	STA	$0000,X	;zero byte
	INX
	DEY
	BNE	ZXLY1	;if not done

	RTS		;return
;	SPACE	4,10
;**	ILP - Initialize Line Buffer Pointer
;*
;*	ENTRY	JSR	ILP
;*
;*	EXIT
;*		INBUFF - INBUFF+1 = line buffer address
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


ILP	=	*		;entry
	LDA	#>LBUFF		;high buffer address
	STA	INBUFF+1	;high line buffer pointer
	LDA	#<LBUFF		;low buffer address
	STA	INBUFF		;low line buffer pointer
	RTS			;return
;	SPACE	4,10
;**	SIL - Shift Integer Left
;*
;*	ENTRY	JSR	SIL
;*		ZTEMP4 - ZTEMP4+1 = number (high, low) to shift
;*
;*	EXIT
;*		ZTEMP4 - ZTEMP4+1 shifted left 1
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


SIL	=	*		;entry
	CLC
	ROL	ZTEMP4+1	;shift low
	ROL	ZTEMP4		;shift high
	RTS			;return
;	SPACE	4,10
	FIX	FSUB
;	SPACE	4,10
;**	FSUB - Perform Floating Point Subtract
;*
;*	FSUB subtracts FR1 from FR0.
;*
;*	ENTRY	JSR	FSUB
;*		FR0 - FR0+5 = minuend
;*		FR1 - FR1+5 = subtrahend
;*
;*	EXIT
;*		C set, if error
;*		C clear, if no error
;*		FR0 - FR0+5 = difference
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;FSUB	=	*	;entry

;	Complement sign of subtrahend and add.

	LDA	FR1	;subtrahend exponent
	EOR	#$80	;complement sign of subtrahend
	STA	FR1	;update subtrahend exponent
;	JMP	FADD	;perform add, return
;	SPACE	4,10
	FIX	FADD
;	SPACE	4,10
;**	FADD - Perform Floating Point Add
;*
;*	ENTRY	JSR	FADD
;*		FR0 - FR0+5 = augend
;*		FR1 - FR1+5 = addend
;*
;*	EXIT
;*		C set, if error
;*		C clear, if no error
;*		FR0 - FR0+5 = sum
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;FADD	=	*	;entry

;	Initialize.

FADD1	LDA	FR1	;exponent of addend
	AND	#$7F	;clear sign of addend mantissa
	STA	ZTEMP4	;save addend exponent
	LDA	FR0	;exponent of augend
	AND	#$7F	;clear sign of augend mantissa
	SEC
	SBC	ZTEMP4	;subtract addend exponent
	BPL	FADD3	;if augend exponent >= addend exponent

;	Swap augend and addend.

	LDX	#FPREC-1	;offset to last byte

FADD2	LDA	FR0,X		;byte of augend
	LDY	FR1,X		;byte of addend
	STA	FR1,X		;move byte of augend to addend
	TYA
	STA	FR0,X		;move byte of addend to augend
	DEX
	BPL	FADD2		;if not done

	BMI	FADD1		;re-initialize

;	Check alignment.

FADD3	BEQ	FADD4	;if exponent difference zero, already aligned

	CMP	#FMPREC	;mantissa precision
	BCS	FADD6	;if exponent difference < mantissa precision

;	Align.

	JSR	S1R	;shift FR1 right

;	Check for like signs of mantissas.

FADD4	SED
	LDA	FR0	;augend exponent
	EOR	FR1	;EOR with addend exponent
	BMI	FADD8	;if signs differ, subtract

;	Add.

	LDX	#FMPREC-1	;offset to last byte of mantissa
	CLC

FADD5	LDA	FR0M,X		;byte of augend mantissa
	ADC	FR1M,X		;add byte of addend mantissa
	STA	FR0M,X		;update byte of result mantissa
	DEX
	BPL	FADD5		;if not done

	CLD
	BCS	FADD7		;if carry, process carry

;	Exit.

FADD6	JMP	NORM		;normalize, return

;	Process carry.

FADD7	LDA	#1		;indicate shift 1
	JSR	S0R		;shift FR0 right
	LDA	#1		;carry
	STA	FR0M		;set carry in result

;	Exit.

	JMP	NORM		;normalize, return

;	Subtract.

FADD8	LDX	#FMPREC-1	;offset to last byte of mantissa
	SEC

FADD9	LDA	FR0M,X		;byte of augend mantissa
	SBC	FR1M,X		;subtract byte of addend mantissa
	STA	FR0M,X		;update byte of result mantissa
	DEX
	BPL	FADD9		;if not done

	BCC	FADD10		;if borrow, process borrow

;	Exit.

	CLD
	JMP	NORM		;normalize ???, return

;	Process borrow.

FADD10	LDA	FR0		;result exponent
	EOR	#$80		;complement sign of result
	STA	FR0		;update result exponent

	SEC
	LDX	#FMPREC-1	;offset to last byte of mantissa

FADD11	LDA	#0
	SBC	FR0M,X		;complement byte of result mantissa
	STA	FR0M,X		;update byte of result mantissa
	DEX
	BPL	FADD11		;if not done

;	Exit.

	CLD
	JMP	NORM		;normalize ???, return
;	SPACE	4,10
	FIX	FMUL
;	SPACE	4,10
;**	FMUL - Perform Floating Point Multiply
;*
;*	ENTRY	JSR	FMUL
;*		FR0 - FR0+5 = multiplicand
;*		FR1 - FR1+5 = multiplier
;*
;*	EXIT
;*		C set, if error
;*		C clear, if no error
;*		FR0 - FR0+5 = product
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;FMUL	=	*	;entry

;	Check for zero multiplicand.

	LDA	FR0	;multiplicand exponent
	BEQ	FMUL8	;if multiplicand exponent zero, result is zero

;	Check for zero multiplier.

	LDA	FR1	;multiplier exponent
	BEQ	FMUL7	;if multiplier exponent zero, result is zero

	JSR	SUE	;set up exponent
	SEC
	SBC	#$40	;subtract bias
	SEC		;add 1
	ADC	FR1	;add multiplier exponent
	BMI	FMUL9	;if overflow, error

;	Set up.

	JSR	SUP	;set up

;	Compute number of times to add multiplicand.

FMUL1	LDA	FRE+FPREC-1	;last byte of FRE
	AND	#$0F		;extract low order digit
	STA	ZTEMP1+1

;	Check for completion.

FMUL2	DEC	ZTEMP1+1	;decrement counter
	BMI	FMUL3		;if done

	JSR	FRA10		;add FR1 to FR0
	JMP	FMUL2		;continue

;	Compute number of times to add 10 times multiplicand.

FMUL3	LDA	FRE+FPREC-1	;last byte of FRE
	LSR
	LSR
	LSR
	LSR			;high order digit
	STA	ZTEMP1+1

;	Check for completion.

FMUL4	DEC	ZTEMP1+1	;decrement counter
	BMI	FMUL5		;if done

	JSR	FRA20		;add FR2 to FR0
	JMP	FMUL4		;continue

;	Set up for next set of adds.

FMUL5	JSR	S0ER		;shift FR0/FRE right

;	Decrement counter and test for completion.

	DEC	ZTEMP1		;decrement
	BNE	FMUL1		;if not done

;	Set exponent.

FMUL6	LDA	EEXP		;exponent
	STA	FR0		;result exponent
	JMP	N0E		;normalize, return

;	Return zero result.

FMUL7	JSR	ZFR0		;zero FR0

;	Return no error.

FMUL8	CLC			;indicate no error
	RTS			;return

;	Return error.

FMUL9	SEC			;indicate error
	RTS			;return
;	SPACE	4,10
	FIX	FDIV
;	SPACE	4,10
;**	FDIV - Perform Floating Point Divide
;*
;*	ENTRY	JSR	FDIV
;*		FR0 - FR0+5 = dividend
;*		FR1 - FR1+5 = divisor
;*
;*	EXIT
;*		C clear, if no error
;*		C set, if error
;*		FR0 - FR0+5 = quotient
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;FDIV	=	*	;entry

;	Check for zero divisor.

	LDA	FR1	;divisor exponent
	BEQ	FMUL9	;if divisor exponent zero, error

;	Check for zero dividend.

	LDA	FR0	;dividend exponent
	BEQ	FMUL8	;if dividend exponent zero, result is zero

	JSR	SUE	;set up exponent
	SEC
	SBC	FR1	;subtract divisor exponent
	CLC
	ADC	#$40	;add bias
	BMI	FMUL9	;if overflow, error

	JSR	SUP	;set up
	INC	ZTEMP1	;divide requires extra pass
	JMP	FDIV3	;skip shift

;	Shift FR0/FRE left one byte.

FDIV1	LDX	#0		;offset to first byte to shift

FDIV2	LDA	FR0+1,X		;byte to shift
	STA	FR0,X		;byte of destination
	INX
	CPX	#FMPREC*2+2	;number of bytes to shift
	BNE	FDIV2		;if not done

;	Subtract 2 times divisor from dividend.

FDIV3	LDY	#FPREC-1	;offset to last byte
	SEC
	SED

FDIV4	LDA	FRE,Y		;byte of dividend
	SBC	FR2,Y		;subtract byte of 2*divisor
	STA	FRE,Y		;update byte of dividend
	DEY
	BPL	FDIV4		;if not done

	CLD
	BCC	FDIV5		;if difference < 0

	INC	QTEMP		;increment
	BNE	FDIV3		;continue

;	Adjust.

FDIV5	JSR	FRA2E	;add FR2 to FR0

;	Shift last byte of quotient left one digit.

	ASL	QTEMP
	ASL	QTEMP
	ASL	QTEMP
	ASL	QTEMP

;	Subtract divisor from dividend.

FDIV6	LDY	#FPREC-1	;offset to last byte
	SEC
	SED

FDIV7	LDA	FRE,Y		;byte of dividend
	SBC	FR1,Y		;subtract byte of divisor
	STA	FRE,Y		;update byte of dividend
	DEY
	BPL	FDIV7		;if not done

	CLD
	BCC	FDIV8		;if difference < 0

	INC	QTEMP		;increment
	BNE	FDIV6		;continue

;	Adjust.

FDIV8	JSR	FRA1E	;add FR1 to FR0
	DEC	ZTEMP1	;decrement
	BNE	FDIV1	;if not done

;	Clear exponent.

	JSR	S0ER	;shift  FR0/FRE right

;	Exit.

	JMP	FMUL6
;	SPACE	4,10
;**	GNC - Get Next Character
;*
;*	ENTRY	JSR	GNC
;*		INBUFF - INBUFF+1 = line buffer pointer
;*		CIX = offset to character
;*
;*	EXIT
;*		C set, if character not numeric
;*		A = non-numeric character
;*		C clear, if character numeric
;*		CIX = offset to next character
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


GNC	=	*		;entry
	JSR	TNC		;test for numeric character
	LDY	CIX		;offset
	BCC	ICX		;if numeric, increment offset, return

	LDA	(INBUFF),Y	;character
;	JMP	ICX		;increment offset, return
;	SPACE	4,10
;**	ICX - Increment Character Offset
;*
;*	ENTRY	JSR	ICX
;*		Y = offset
;*
;*	EXIT
;*		CIX = offset to next character
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


ICX	=	*	;entry
	INY		;increment offset
	STY	CIX	;offset
	RTS		;return
;	SPACE	4,10
;**	SLB - Skip Leading Blanks
;*
;*	ENTRY	JSR	SLB
;*		INBUFF - INBUFF+1 = line buffer pointer
;*		CIX = offset
;*
;*	EXIT
;*		CIX = offset to first non-blank character
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


SLB	=	*		;entry

;	Initialize.

	LDY	CIX		;offset to character
	LDA	#' '

;	Search for first non-blank character.

SLB1	CMP	(INBUFF),Y	;character
	BNE	SLB2		;if non-blank character

	INY
	BNE	SLB1		;if not done

;	Exit.

SLB2	STY	CIX		;offset to first non-blank character
	RTS			;return
;	SPACE	4,10
;**	TNC - Test for Numeric Character
;*
;*	ENTRY	JSR	TNC
;*		INBUFF - INBUFF+1 = line buffer pointer
;*		CIX = offset
;*
;*	EXIT
;*		C set, if numeric
;*		C clear if non-numeric
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


TNC	=	*		;entry
	LDY	CIX		;offset
	LDA	(INBUFF),Y	;character
	SEC
	SBC	#'0'
	BCC	TVN2		;if < "0", return failure

	CMP	#'9'-'0'+1	;return success or failure
	RTS			;return
;	SPACE	4,10
;**	TVN - Test for Valid Number Character
;*
;*	ENTRY	JSR	TVN
;*
;*	EXIT
;*		C set, if not number
;*		C clear, if number
;*
;*	NOTES
;*		Problem: bytes wasted by BCC TVN5.
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


TVN	=	*	;entry

;	Initialize.

	LDA	CIX	;offset
	PHA		;save offset

;	Check next character.

	JSR	GNC	;get next character
	BCC	TVN5	;if numeric, return success

	CMP	#'.'
	BEQ	TVN4	;if ".", check next character

	CMP	#'+'
	BEQ	TVN3	;if "+", check next character

	CMP	#'-'
	BEQ	TVN3	;if "-", check next character

;	Clean stack.

TVN1	PLA		;clean stack

;	Return failure.

TVN2	SEC		;indicate failure
	RTS		;return

;	Check character after "+" or "-".

TVN3	JSR	GNC	;get next character
	BCC	TVN5	;if numeric, return success

	CMP	#'.'
	BNE	TVN1	;if not ".", return failure

;	Check character after ".".

TVN4	JSR	GNC	;get next character
	BCC	TVN5	;if numeric, return success

	BCS	TVN1	;return failure

;	Return success.

TVN5	PLA		;saved offset
	STA	CIX	;restore offset
	CLC		;indicate success
	RTS		;return
;	SPACE	4,10
;**	S2L - Shift FR2 Left One Digit
;*
;*	ENTRY	JSR	S2L
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


S2L	=	*	;entry
	LDX	#FR2+1	;indicate shift of FR2 mantissa
	BNE	SML	;shift mantissa left 1 digit, return
;	SPACE	4,10
;**	S0L - Shift FR0 Left One Digit
;*
;*	ENTRY	JSR	S0L
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


S0L	=	*	;entry
	LDX	#FR0M	;indicate shift of FR0 mantissa
;	JMP	SML	;shift mantissa left 1 digit, return
;	SPACE	4,10
;**	SML - Shift Mantissa Left One Digit
;*
;*	ENTRY	JSR	SML
;*
;*	EXIT
;*		FRX = excess digit
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


SML	=	*	;entry
	LDY	#4	;number of bits to shift

SML2	CLC
	ROL	$0004,X	;shift 5th byte left 1 bit
	ROL	$0003,X	;shift 4th byte left 1 bit
	ROL	$0002,X	;shift 3rd byte left 1 bit
	ROL	$0001,X	;shift 2nd byte left 1 bit
	ROL	$0000,X	;shift 1st byte left 1 bit
	ROL	FRX	;shift excess digit left 1 bit
	DEY
	BNE	SML2	;if not done

	RTS		;return
;	SPACE	4,10
;**	NORM - Normalize FR0
;*
;*	ENTRY	JSR	NORM
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


NORM	=	*		;entry
	LDX	#0
	STX	FRE		;byte to shift in
;	JMP	N0E		;normalize FR0/FRE, return
;	SPACE	4,10
;**	N0E - Normalize FR0/FRE
;*
;*	ENTRY	JSR	N0E
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


N0E	=	*		;entry
	LDX	#FMPREC-1	;mantissa size
	LDA	FR0		;exponent
	BEQ	N0E5		;if exponent zero, number is zero

N0E1	LDA	FR0M		;first byte of mantissa
	BNE	N0E3		;if not zero, no shift

;	Shift mantissa left 1 byte.

	LDY	#0		;offset to first byte of mantissa

N0E2	LDA	FR0M+1,Y	;byte to shift
	STA	FR0M,Y		;byte of destination
	INY
	CPY	#FMPREC		;size of mantissa
	BCC	N0E2		;if not done

;	Decrement exponent and check for completion.

	DEC	FR0		;decrement exponent
	DEX
	BNE	N0E1		;if not done

;	Check first byte of mantissa.

	LDA	FR0M	;first byte of mantissa
	BNE	N0E3	;if mantissa not zero

;	Zero exponent.

	STA	FR0	;zero exponent
	CLC
	RTS		;return

;	Check for overflow.

N0E3	LDA	FR0	;exponent
	AND	#$7F	;clear sign
	CMP	#$40+49	;bias+49
	BCC	N0E4	;if exponent < 49, no overflow

;	Return error.

;	SEC		;indicate error
	RTS		;return

;	Check for underflow.

N0E4	CMP	#$40-49
	BCS	N0E5	;if exponent >= -49, no underflow

;	Zero result.

	JSR	ZFR0	;zero FR0

;	Exit.

N0E5	CLC		;indicate no error
	RTS		;return
;	SPACE	4,10
;**	S0R - Shift FR0 Right
;*
;*	ENTRY	JSR	S0R
;*		A = shift count
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


S0R	=	*	;entry
	LDX	#FR0	;indicate shift of FR0
	BNE	SRR	;shift register right, return
;	SPACE	4,10
;**	S1R - Shift FR1 Right
;*
;*	ENTRY	JSR	S1R
;*		A = shift count
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


S1R	=	*	;entry
	LDX	#FR1	;indicate shift of FR1
;	JMP	SRR	;shift register right, return
;	SPACE	4,10
;**	SRR - Shift Register Right
;*
;*	ENTRY	JSR	SRR
;*		X = offset to register
;*		A = shift count
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


SRR	=	*		;entry
	STX	ZTEMP3		;register
	STA	ZTEMP4		;shift count
	STA	ZTEMP4+1	;save shift count

SRR1	LDY	#FMPREC-1	;mantissa size-1

SRR2	LDA	$0004,X		;byte to shift
	STA	$0005,X		;byte of destination
	DEX
	DEY
	BNE	SRR2		;if not done

	LDA	#0
	STA	$0005,X		;first byte of mantissa
	LDX	ZTEMP3		;register
	DEC	ZTEMP4		;decrement shift count
	BNE	SRR1		;if not done

;	Adjust exponent.

	LDA	$0000,X		;exponent
	CLC
	ADC	ZTEMP4+1	;subtract shift count
	STA	$0000,X		;update exponent
	RTS			;return
;	SPACE	4,10
;**	S0ER - Shift FR0/FRE Right
;*
;*	ENTRY	JSR	S0ER
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


S0ER	=	*		;entry
	LDX	#FMPREC*2	;number of bytes to shift

S0ER1	LDA	FR0,X		;byte to shift
	STA	FR0+1,X		;byte of destination
	DEX
	BPL	S0ER1		;if not done

	LDA	#0
	STA	FR0		;shift in 0
	RTS			;return
;	SPACE	4,10
;**	C0A - Convert FR0 to ASCII
;*
;*	ENTRY	JSR	C0A
;*		A = decimal point position
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


C0A	=	*	;entry

;	Initialize.

	STA	ZTEMP4	;decimal point position counter
	LDX	#0	;offset to first byte of FR0M
	LDY	#0	;offset to first byte of LBUF

;	Convert next byte.

C0A1	JSR	TDP	;test for decimal point
	SEC
	SBC	#1	;decrement deciaml point position
	STA	ZTEMP4	;update deciaml point position counter

;	Convert first digit of next byte.

	LDA	FR0M,X	;byte
	LSR
	LSR
	LSR
	LSR		;first digit
	JSR	SNL	;store number in line buffer

;	Convert second digit of next byte.

	LDA	FR0M,X	;byte
	AND	#$0F	;extract second digit
	JSR	SNL	;store number in line buffer
	INX
	CPX	#FMPREC	;nuber of bytes
	BCC	C0A1	;if not done

;	Exit.

;	JMP	TDP	;test for decimal point, return
;	SPACE	4,10
;**	TDP - Test for Decimal Point
;*
;*	ENTRY	JSR	TDP
;*		ZTEMP4 = decimal point position counter
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


TDP	=	*	;entry

;	Check decimal point position counter.

	LDA	ZTEMP4	;decimal point position counter
	BNE	TDP1	;if not decimal point position, exit

;	Insert decimal point.

	LDA	#'.'
	JSR	SAL	;store ASCII character in line buffer

;	Exit.

TDP1	RTS		;return
;	SPACE	4,10
;**	SNL - Store Number in Line Buffer
;*
;*	ENTRY	JSR	SNL
;*		A = digit to store
;*		Y = offset
;*
;*	EXIT
;*		ASCII digit placed in line buffer
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


SNL	=	*	;entry
	ORA	#$30	;convert digit to ASCII
;	JMP	SAL	;store ASCII character in line buffer, return
;	SPACE	4,10
;**	SAL - Store ASCII Character in Line Buffer
;*
;*	ENTRY	JSR	SAL
;*		Y = offset
;*		A = character
;*
;*	EXIT
;*		Character placed in line buffer
;*		Y = incremented offset
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


SAL	=	*	;entry
	STA	LBUFF,Y	;store character in line buffer
	INY		;increment offset
	RTS		;return
;	SPACE	4,10
;**	FNZ - Find Last Non-zero Character in Line Buffer
;*
;*	FNZ returns the last non-zero character.  If the last
;*	non-zero character is ".", FNZ returns the character
;*	preceding the ".".  If no other non-zero character is
;*	encountered, FNZ returns the first character.
;*
;*	ENTRY	JSR	FNZ
;*
;*	EXIT
;*		A = character
;*		X = offset to character
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


FNZ	=	*	;entry

;	Initialize.

	LDX	#10	;offset to last possible character

;	Check next character.

FNZ1	LDA	LBUFF,X	;character
	CMP	#'.'
	BEQ	FNZ2	;if ".", return preceding character

	CMP	#'0'
	BNE	FNZ3	;if not "0", exit

;	Decrement offset and check for completion.

	DEX
	BNE	FNZ1	;if not done

;	Return character preceding "." or first character.

FNZ2	DEX		;offset to character
	LDA	LBUFF,X	;character

;	Exit.

FNZ3	RTS		;return
;	SPACE	4,10
;**	GND - Get Next Digit
;*
;*	ENTRY	JSR	GND
;*		FR0 - FR0+5 = number
;*
;*	EXIT
;*		A = digit
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


GND	=	*	;entry
	JSR	S0L	;shift FR0 left 1 digit
	LDA	FRX	;excess digit
	AND	#$0F	;extract low order digit
	RTS		;return
;	SPACE	4,10
;**	DLP - Decrement Line Buffer Pointer
;*
;*	ENTRY	JSR	DLP
;*		INBUFF - INBUFF+1 = line buffer pointer
;*
;*	EXIT
;*		INBUFF - INBUFF+1 = incremented line buffer pointer
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


DLP	=	*		;entry
	SEC
	LDA	INBUFF		;line buffer pointer
	SBC	#1		;subtract 1
	STA	INBUFF		;update line buffer pointer
	LDA	INBUFF+1
	SBC	#0
	STA	INBUFF+1
	RTS			;return
;	SPACE	4,10
;**	SUE - Set Up Exponent for Multiply or Divide
;*
;*	ENTRY	JSR	SUE
;*
;*	EXIT
;*		A = FR0 exponent (without sign)
;*		FR1 = FR1 exponent (without sign)
;*		FRSIGN = sign of result
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


SUE	=	*	;entry
	LDA	FR0	;FR0 exponent
	EOR	FR1	;EOR with FR1 exponent
	AND	#$80	;extract sign
	STA	FRSIGN	;sign of result
	ASL	FR1	;shift out FR1 sign
	LSR	FR1	;FR1 exponent without sign
	LDA	FR0	;FR0 exponent
	AND	#$7F	;FR0 exponent without sign
	RTS		;return
;	SPACE	4,10
;**	SUP - Set Up for Multiply or Divide
;*
;*	ENTRY	JSR	SUP
;*		A = exponent
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


SUP	=	*	;entry
	ORA	FRSIGN	;place sign in exponent
	STA	EEXP	;exponent
	LDA	#0
	STA	FR0	;clear FR0 exponent
	STA	FR1	;clear FR0 exponent
	JSR	M12	;move FR1 to FR2
	JSR	S2L	;shift FR2 left 1 digit
	LDA	FRX	;excess digit
	AND	#$0F	;extract low order digit
	STA	FR2	;shift in low order digit
	LDA	#FMPREC	;mantissa size
	STA	ZTEMP1	;mantissa size
	JSR	M0E	;move FR0 to FRE
	JSR	ZFR0	;zero FR0
	RTS		;return
;	SPACE	4,10
;**	FRA10 - Add FR1 to FR0
;*
;*	ENTRY	JSR	FRA10
;*		FR0 - FR0+5 = augend
;*		FR1 - FR1+5 = addend
;*
;*	EXIT
;*		FR0 - FR0+5 = sum
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


FRA10	=	*		;entry
	LDX	#FR0+FPREC-1	;offset to last byte of FR0
	BNE	F1R
;	SPACE	4,10
;**	FRA20 - Add FR2 to FR0
;*
;*	ENTRY	JSR	FRA20
;*		FR0 - FR0+5 = augend
;*		FR2 - FR2+5 = addend
;*
;*	EXIT
;*		FR0 - FR0+5 = sum
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


FRA20	=	*		;entry
	LDX	#FR0+FPREC-1	;offset to last byte of FR0
	BNE	F2R
;	SPACE	4,10
;**	FRA1E - Add FR1 to FRE
;*
;*	ENTRY	JSR	FRA1E
;*		FRE - FRE+5 = augend
;*		FR1 - FR1+5 = addend
;*
;*	EXIT
;*		FRE - FRE+5 = sum
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


FRA1E	=	*		;entry
	LDX	#FRE+FPREC-1	;offset to last byte of FRE
;	JMP	F1R		;add FR1 to register, return
;	SPACE	4,10
;**	F1R - Add FR1 to Register
;*
;*	ENTRY	JSR	F1R
;*		X = offset to last byte of augend register
;*		FR1 - FR1+5 = addend
;*
;*	EXIT
;*		Sum in augend register
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


F1R	=	*		;entry
	LDY	#FR1+FPREC-1	;offset to last byte of FR1
	BNE	FARR
;	SPACE	4,10
;**	FRA2E - Add FR2 to FRE
;*
;*	ENTRY	JSR	FRA2E
;*		FRE - FRE+5 = augend
;*		FR2 - FR2+5 = addend
;*
;*	EXIT
;*		FRE - FRE+5 = sum
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


FRA2E	=	*		;entry
	LDX	#FRE+FPREC-1	;offset to last byte of FRE
;	JMP	F2R
;	SPACE	4,10
;**	F2R - Add FR2 to Register
;*
;*	ENTRY	JSR	F2R
;*		X = offset to last byte of augend register
;*		FR2 - FR2+5 = addend
;*
;*	EXIT
;*		Sum in augend register
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


F2R	=	*		;entry
	LDY	#FR2+FPREC-1	;offset to last byte of FR2
;	JMP	FARR
;	SPACE	4,10
;**	FARR - Add Register to Register
;*
;*	ENTRY	JSR	FARR
;*		X = offset to last byte of augend register
;*		Y = offset to last byte of addend register
;*
;*	EXIT
;*		Sum in augend register
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


FARR	=	*		;entry

;	Initialize.

	LDA	#FPREC-1	;floating point number size-1
	STA	ZTEMP4		;byte count
	CLC
	SED

;	Add.

FARR1	LDA	$0000,X		;byte of augend
	ADC	$0000,Y		;add byte of addend
	STA	$0000,X		;update byte of augend
	DEX
	DEY
	DEC	ZTEMP4		;decrement byte count
	BPL	FARR1		;if not done

;	Exit.

	CLD
	RTS			;return
;	SPACE	4,10
;**	M12 - Move FR1 to FR2
;*
;*	ENTRY	JSR	M12
;*		FR1 - FR1+5 = number to move
;*
;*	EXIT
;*		FR2 - FR2+5 = moved number
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


M12	=	*		;entry
	LDY	#FPREC-1	;offset to last byte

M121	LDA	FR1,Y		;byte of source
	STA	FR2,Y		;byte of destination
	DEY
	BPL	M121		;if not done

	RTS			;return
;	SPACE	4,10
;**	M0E - Move FR0 to FRE
;*
;*	ENTRY	JSR	M0E
;*		FR0 - FR0+5 = number to move
;*
;*	EXIT
;*		FRE - FRE+5 = moved number
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


M0E	=	*		;entry
	LDY	#FPREC-1	;offset to last byte

M0E1	LDA	FR0,Y		;byte of source
	STA	FRE,Y		;byte of destination
	DEY
	BPL	M0E1		;if not done

	RTS			;return
;	SPACE	4,10
	FIX	PLYEVL
;	SPACE	4,10
;**	PLYEVL - Evaluate Polynomial
;*
;*	Y = A(0)+A(1)*X+A(2)*X^2+...+A(N)*X^N
;*
;*	ENTRY	JSR	PLYEVL
;*		X = low address of coefficient table
;*		Y = high address of coefficient table
;*		FR0 - FR0+5 = X argument
;*		A = N+1
;*
;*	EXIT
;*		FR0 - FR0+5 = Y result
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;PLYEVL	=	*		;entry

	STX	FPTR2		;save pointer to coefficients
	STY	FPTR2+1
	STA	PLYCNT		;degree
	LDX	#<PLYARG
	LDY	#>PLYARG
	JSR	FST0R		;save argument
	JSR	FMOVE		;move argument to FR1
	LDX	FPTR2
	LDY	FPTR2+1
	JSR	FLD0R		;initialize sum in FR0
	DEC	PLYCNT		;decrement degree
	BEQ	PLY3		;if complete, exit

PLY1	JSR	FMUL		;argument times current sum
	BCS	PLY3		;if overflow

	CLC
	LDA	FPTR2		;current low coefficient address
	ADC	#FPREC		;add floating point number size
	STA	FPTR2		;update low coefficient address
	BCC	PLY2		;if no carry

	LDA	FPTR2+1		;current high coefficceint address
	ADC	#0		;adjust high coefficient address
	STA	FPTR2+1		;update high coefficient address

PLY2	LDX	FPTR2		;low coefficient address
	LDY	FPTR2+1		;high coefficient address
	JSR	FLD1R		;get next coefficient
	JSR	FADD		;add coefficient to argument times sum
	BCS	PLY3		;if overflow

	DEC	PLYCNT		;decrement degree
	BEQ	PLY3		;if complete, exit

	LDX	#<PLYARG	;low argument address
	LDY	#>PLYARG	;high argument address
	JSR	FLD1R		;get argument
	BMI	PLY1		;continue

PLY3	RTS			;return
;	SPACE	4,10
	FIX	FLD0R
;	SPACE	4,10
;**	FLD0R - ???
;*
;*	ENTRY	JSR	FLD0R
;*		X = low pointer
;*		Y = high pointer
;*
;*	EXIT
;*		FR0 loaded
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;FLD0R	=	*		;entry
	STX	FLPTR		;low pointer
	STY	FLPTR+1		;high pointer
;	JMP	FLD0P		;load FR0, return
;	SPACE	4,10
	FIX	FLD0P
;	SPACE	4,10
;**	FLD0P - Load FR0
;*
;*	ENTRY	JSR	FLD0P
;*		FLPTR - FLPTR+1 = pointer
;*
;*	EXIT
;*		FR0 loaded
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;FLD0P	=	*		;entry

	LDY	#FPREC-1	;offset to last byte

FLD01	LDA	(FLPTR),Y	;byte of source
	STA	FR0,Y		;byte of destination
	DEY
	BPL	FLD01		;if not done

	RTS			;return
;	SPACE	4,10
	FIX	FLD1R
;	SPACE	4,10
;**	FLD1R - Load FR1
;*
;*	ENTRY	JSR	FLD1R
;*		X = low pointer
;*		Y = high pointer
;*
;*	EXIT
;*		FR1 loaded
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;FLD1R	=	*		;entry
	STX	FLPTR		;low pointer
	STY	FLPTR+1		;high pointer
;	JMP	FLD1P		;load FR1, return
;	SPACE	4,10
	FIX	FLD1P
;	SPACE	4,10
;**	FLD1P - Load FR1
;*
;*	ENTRY	JSR	FLD1P
;*		FLPTR - FLPTR+1 = pointer
;*
;*	EXIT
;*		FR1 loaded
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;FLD1P	=	*		;entry

	LDY	#FPREC-1	;offset to last byte

FLD11	LDA	(FLPTR),Y	;byte of source
	STA	FR1,Y		;byte of destination
	DEY
	BPL	FLD11		;if not done

	RTS			;return
;	SPACE	4,10
	FIX	FST0R
;	SPACE	4,10
;**	FST0R - Store FR0
;*
;*	ENTRY	JSR	FST0R
;*		FR0 - FR0+5 = number
;*		X = low pointer
;*		Y = high pointer
;*
;*	EXIT
;*		FR0 stored
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;FST0R	=	*		;entry
	STX	FLPTR		;low pointer
	STY	FLPTR+1		;high pointer
;	JMP	FST0P		;???, return
;	SPACE	4,10
	FIX	FST0P
;	SPACE	4,10
;**	FST0P - Store FR0
;*
;*	ENTRY	JSR	FST0P
;*		FR0 - FR0+5 = number
;*		FLPTR - FLPTR+1 = pointer
;*
;*	EXIT
;*		FR0 stored
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;FST0P	=	*		;entry

	LDY	#FPREC-1	;offset to last byte

FST01	LDA	FR0,Y		;byte of source
	STA	(FLPTR),Y	;byte of destination
	DEY
	BPL	FST01		;if not done

	RTS			;return
;	SPACE	4,10
	FIX	FMOVE
;	SPACE	4,10
;**	FMOVE - Move FR0 to FR1
;*
;*	ENTRY	JSR	FMOVE
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;FMOVE	=	*		;entry

	LDX	#FPREC-1	;offset to last byte

FMO1	LDA	FR0,X		;byte of source
	STA	FR1,X		;byte of destination
	DEX
	BPL	FMO1		;if not done

	RTS			;return
;	SPACE	4,10
	FIX	EXP
;	SPACE	4,10
;**	EXP - Compute Power of e
;*
;*	ENTRY	JSR	EXP
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;EXP	=	*		;entry

;	Initialize.

	LDX	#<LOG10E	;base 10 logarithm of e
	LDY	#>LOG10E
	JSR	FLD1R		;load FR1

;	Compute X*LOG10(E).

	JSR	FMUL		;multiply
	BCS	EXP6		;if overflow, error

;	Compute result = 10^(X*LOG10(E)).

;	JMP	EXP10		;compute power of 10, return
;	SPACE	4,10
	FIX	EXP10
;	SPACE	4,10
;**	EXP10 - Compute Power of 10
;*
;*	ENTRY	JSR	EXP10
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;EXP10	=	*		;entry

;	Initialize.

	LDA	#0
	STA	XFMFLG		;zero integer part
	LDA	FR0
	STA	SGNFLG		;save argument sign
	AND	#$7F		;extract absolute value
	STA	FR0		;update argument

;	Check for argument less than 1.

	SEC
	SBC	#$40		;subtract bias
	BMI	EXP1		;if argument < 1

;	Extract integer and fractional parts of exponent.

	CMP	#FPREC-2
	BPL	EXP6		;if argument too big, error

	LDX	#<FPSCR
	LDY	#>FPSCR
	JSR	FST0R		;save argument
	JSR	FPI		;convert argument to integer
	LDA	FR0
	STA	XFMFLG		;save interger part
	LDA	FR0+1		;most significant byte of integer part
	BNE	EXP6		;if integer part too large, error

	JSR	IFP		;convert integer part to floating point
	JSR	FMOVE		;???
	LDX	#<FPSCR
	LDY	#>FPSCR
	JSR	FLD0R		;argument
	JSR	FSUB		;subtract to get fractional part

;	Compute 10 to fractional exponent.

EXP1	LDA	#NPCOEF
	LDX	#<P10COF
	LDY	#>P10COF
	JSR	PLYEVL		;P(X)
	JSR	FMOVE
	JSR	FMUL		;P(X)*P(X)

;	Check integer part.

	LDA	XFMFLG		;integer part
	BEQ	EXP4		;if integer part zero

;	Compute 10 to integer part.

	CLC
	ROR			;integer part divided by 2
	STA	FR1		;exponent
	LDA	#1		;assume mantissa 1
	BCC	EXP2		;if integer part even

	LDA	#$10		;substitute mantissa 10

EXP2	STA	FR1M		;mantissa
	LDX	#FMPREC-1	;offset to last byte of mantissa
	LDA	#0

EXP3	STA	FR1M+1,X	;zero byte of mantissa
	DEX
	BPL	EXP3		;if not done

	LDA	FR1		;exponent
	CLC
	ADC	#$40		;add bias
	BCS	EXP6		;if too big, error

	BMI	EXP6		;if underflow, error

	STA	FR1		;10 to integer part

;	Compute product of 10 to integer part and 10 to fractional part.

	JSR	FMUL		;multiply to get result

;	Invert result if argument < 0.

EXP4	LDA	SGNFLG		;argument sign
	BPL	EXP5		;if argument >= 0

	JSR	FMOVE
	LDX	#<FONE
	LDY	#>FONE
	JSR	FLD0R		;load FR0
	JSR	FDIV		;divide to get result

;	Exit.

EXP5	RTS			;return

;	Return error.

EXP6	SEC			;indicate error
	RTS			;return
;	SPACE	4,10
;**	P10COF - Power of 10 Coefficients


P10COF	.BYTE	$3D,$17,$94,$19,$00,$00	;0.0000179419
	.BYTE	$3D,$57,$33,$05,$00,$00	;0.0000573305
	.BYTE	$3E,$05,$54,$76,$62,$00	;0.0005547662
	.BYTE	$3E,$32,$19,$62,$27,$00	;0.0032176227
	.BYTE	$3F,$01,$68,$60,$30,$36	;0.0168603036
	.BYTE	$3F,$07,$32,$03,$27,$41	;0.0732032741
	.BYTE	$3F,$25,$43,$34,$56,$75	;0.2543345675
	.BYTE	$3F,$66,$27,$37,$30,$50	;0.6627373050
	.BYTE	$40,$01,$15,$12,$92,$55	;1.15129255
	.BYTE	$3F,$99,$99,$99,$99,$99	;0.9999999999

NPCOEF	=	(*-P10COF)/FPREC
;	SPACE	4,10
;**	LOG10E - Base 10 Logarithm of e


LOG10E	.BYTE	$3F,$43,$42,$94,$48,$19	;base 10 logarithm of e
;	SPACE	4,10
;**	FONE - 1.0


FONE	.BYTE	$40,$01,$00,$00,$00,$00	;1.0
;	SPACE	4,10
;**	XFORM - Transform
;*
;*	Z = (X-C)/(X+C)
;*
;*	ENTRY	JSR	XFORM
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


XFORM	=	*		;entry
	STX	FPTR2
	STY	FPTR2+1
	LDX	#<PLYARG
	LDY	#>PLYARG
	JSR	FST0R		;save argument
	LDX	FPTR2
	LDY	FPTR2+1
	JSR	FLD1R		;load FR1
	JSR	FADD		;X+C
	LDX	#<FPSCR
	LDY	#>FPSCR
	JSR	FST0R		;store FR0
	LDX	#<PLYARG
	LDY	#>PLYARG
	JSR	FLD0R		;load FR0
	LDX	FPTR2
	LDY	FPTR2+1
	JSR	FLD1R		;load FR1
	JSR	FSUB		;X-C
	LDX	#<FPSCR
	LDY	#>FPSCR
	JSR	FLD1R		;load FR1
	JSR	FDIV		;divide to get result
	RTS			;return
;	SPACE	4,10
	FIX	LOG
;	SPACE	4,10
;**	LOG - Compute Base e Logarithm
;*
;*	ENTRY	JSR	LOG
;*		FR0 - FR0+5 = argument
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;LOG	=	*	;entry

	LDA	#1	;indicate base e logarithm
	BNE	LOGS	;compute logartihm, return
;	SPACE	4,10
	FIX	LOG10
;	SPACE	4,10
;**	LOG10 - Compute Base 10 Logarithm
;*
;*	ENTRY	JSR	LOG10
;*		FR0 - FR0+5 = argument
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


;LOG10	=	*	;entry

	LDA	#0	;indicate base 10 logartihm
;	JMP	LOGS	;compute logarithm, return
;	SPACE	4,10
;**	LOGS - Compute Logarithm
;*
;*	ENTRY	JSR	LOGS
;*		A = 0, if base 10 logarithm
;*		  = 1, if base e logartihm
;*		FR0 - FR0+5 = argument
;*
;*	EXIT
;*		C set, if error
;*		C clear, if no error
;*		FR0 - FR0+5 = result
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


LOGS	=	*	;entry

;	Initialize.

	STA	SGNFLG	;save logarithm base indicator

;	Check argument.

	LDA	FR0	;argument exponent
	BEQ	LOGS1	;if argument zero, error

	BMI	LOGS1	;if argument negative, error

;	X = F*(10^Y), 1<F<10
;	10^Y HAS SAME EXP BYTE AS X
;	& MANTISSA BYTE = 1 OR 10

	JMP	LOGQ

;	Return error.

LOGS1	SEC		;indicate error
	RTS		;return
;	SPACE	4,10
;**	LOGC - Complete Computation of Logarithm
;*
;*	ENTRY	JSR	LOGC
;*		SGNFLG = 0, if base 10 logarithmr
;*		       = 1, if base e logarithm
;*
;*	NOTES
;*		Problem: logic is convoluted because LOGQ code
;*		was moved.
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


LOGC	=	*		;entry

;	Initialize.

	SBC	#$40
	ASL
	STA	XFMFLG		;save Y
	LDA	FR0+1
	AND	#$F0
	BNE	LOGC2

	LDA	#1		;mantissa is 1
	BNE	LOGC3		;set mantissa

LOGC2	INC	XFMFLG		;increment Y
	LDA	#$10		;mantissa is 10

LOGC3	STA	FR1M		;mantissa
	LDX	#FMPREC-1	;offset to last byte of mantissa
	LDA	#0

LOGC4	STA	FR1M+1,X	;zero byte of mantissa
	DEX
	BPL	LOGC4		;if not done

	JSR	FDIV		;X = X/(10^Y), S.B. IN (1,10)

;	Compute LOG10(X), 1 <= X <= 10.

	LDX	#<SQR10
	LDY	#>SQR10
	JSR	XFORM		;Z = (X-C)/(X+C); C*C = 10
	LDX	#<FPSCR
	LDY	#>FPSCR
	JSR	FST0R		;SAVE Z
	JSR	FMOVE
	JSR	FMUL		;Z*Z
	LDA	#NLCOEF
	LDX	#<LGCOEF
	LDY	#>LGCOEF
	JSR	PLYEVL		;P(Z*Z)
	LDX	#<FPSCR
	LDY	#>FPSCR
	JSR	FLD1R		;load FR1
	JSR	FMUL		;Z*P(Z*Z)
	LDX	#<FHALF
	LDY	#>FHALF
	JSR	FLD1R
	JSR	FADD		;0.5 + Z*P(Z*Z)
	JSR	FMOVE
	LDA	#0
	STA	FR0+1
	LDA	XFMFLG
	STA	FR0
	BPL	LOGC5

	EOR	#-$01		;complement sign
	CLC
	ADC	#1
	STA	FR0

LOGC5	JSR	IFP		;convert integer to floating point
	BIT	XFMFLG
	BPL	LOGC6

	LDA	#$80
	ORA	FR0
	STA	FR0		;update exponent

LOGC6	JSR	FADD		;LOG(X) = LOG(X)+Y

;	Check base of logarithm.

	LDA	SGNFLG		;logarithm base indicator
	BEQ	LOGC7		;if LOG10 (not LOG)

;	Compute base e logarithm.

	LDX	#<LOG10E	;base 10 logarithm of e
	LDY	#>LOG10E
	JSR	FLD1R		;load FR1
	JSR	FDIV		;result is LOG(X) divided by LOG10(e)

;	Exit.

LOGC7	CLC			;indicate success
	RTS			;return
;	SPACE	4,10
;**	SQR10 - Square Root of 10


SQR10	.BYTE	$40,$03,$16,$22,$77,$66	;square root of 10
;	SPACE	4,10
;**	FHALF - 0.5


FHALF	.BYTE	$3F,$50,$00,$00,$00,$00	;0.5
;	SPACE	4,10
;**	LGCOEF - Logartihm Coefficients


LGCOEF	.BYTE	$3F,$49,$15,$57,$11,$08	;0.4915571108
	.BYTE	$BF,$51,$70,$49,$47,$08	;-0.5170494708
	.BYTE	$3F,$39,$20,$57,$61,$95	;0.3920576195
	.BYTE	$BF,$04,$39,$63,$03,$55	;-0.0439630355
	.BYTE	$3F,$10,$09,$30,$12,$64	;0.1009301264
	.BYTE	$3F,$09,$39,$08,$04,$60	;0.0939080460
	.BYTE	$3F,$12,$42,$58,$47,$42	;0.1242584742
	.BYTE	$3F,$17,$37,$12,$06,$08	;0.1737120608
	.BYTE	$3F,$28,$95,$29,$71,$17	;0.2895297117
	.BYTE	$3F,$86,$85,$88,$96,$44	;0.8685889644

NLCOEF	=	(*-LGCOEF)/FPREC
;	SPACE	4,10
;**	ATCOEF - Arctangent Coefficients
;*
;*	NOTES
;*		Problem: not used.


	.BYTE	$3E,$16,$05,$44,$49,$00	;0.001605444900
	.BYTE	$BE,$95,$68,$38,$45,$00	;-0.009568384500
	.BYTE	$3F,$02,$68,$79,$94,$16	;0.0268799416
	.BYTE	$BF,$04,$92,$78,$90,$80	;-0.0492789080
	.BYTE	$3F,$07,$03,$15,$20,$00	;0.0703152000
	.BYTE	$BF,$08,$92,$29,$12,$44	;-0.0892291244
	.BYTE	$3F,$11,$08,$40,$09,$11	;0.1108400911
	.BYTE	$BF,$14,$28,$31,$56,$04	;-0.1428315604
	.BYTE	$3F,$19,$99,$98,$77,$44	;0.1999987744
	.BYTE	$BF,$33,$33,$33,$31,$13	;-0.3333333113
	.BYTE	$3F,$99,$99,$99,$99,$99	;0.9999999999

	.BYTE	$3F,$78,$53,$98,$16,$34	;pi/4 = arctan 1
;	SPACE	4,10
;**	LOGQ - Continue Computation of Loagarithm
;*
;*	ENTRY	JSR	LOGQ
;*
;*	NOTES
;*		Problem: logic is convoluted because this code was
;*		moved.
;*		Problem: for readability, this might be relocated
;*		before tables.
;*
;*	MODS
;*		Original Author Unknown
;*		1. Bring closer to Coding Standard (object unchanged).
;*		   R. K. Nordin	11/01/83


LOGQ	=	*	;entry
	LDA	FR0
	STA	FR1
	SEC
	JMP	LOGC	;complete computation of logarithm, return

        FIX     $E000
