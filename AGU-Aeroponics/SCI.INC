
; MUSEL = 0		; use Asynchronous
; U2X = 0		; Asynchronous normal mode
; UBRR = 		; Baudrate = Fosc / [16(UBRR + 1)]      UBRR=[Fosc/16.BAUD]-1
; Fosc=1MHz [1E+6/(16.19200)] - 1 = 5 for speed of 19200 bps (-7%)
;

;.equ	UBRR_CNT = 6	; Fosc=1MHz  U2X=1 --> BAUD=19200	(-7%)
;.equ	UBRR_CNT = 2	; Fosc=1MHz  U2X=1 --> BAUD=38400	(8.5%)

;.equ	UBRR_CNT = 11	; Fosc=1.8432MHz  U2X=1 --> BAUD=19200	(0%)
;.equ	UBRR_CNT = 5	; Fosc=1.8432MHz  U2X=1 --> BAUD=38400	(0%)

;.equ	UBRR_CNT = 12	; Fosc=2MHz  U2X=1 --> BAUD=19200	(0.2%)
;.equ	UBRR_CNT = 6	; Fosc=2MHz  U2X=1 --> BAUD=38400	(-7%)

;.equ	UBRR_CNT = 23	; Fosc=3.6864MHz  U2X=1 --> BAUD=19200	(0.2%)
;.equ	UBRR_CNT = 11	; Fosc=3.6864MHz  U2X=1 --> BAUD=38400	(-7%)

;.equ	UBRR_CNT = 25	; Fosc=4MHz  U2X=1 --> BAUD=19200	(0.2%)
;.equ	UBRR_CNT = 12	; Fosc=4MHz  U2X=1 --> BAUD=38400	(0.2%)

;.equ	UBRR_CNT = 47	; Fosc=7.3728MHz U2X=1  --> BAUD=19200	(0.2%)
;.equ	UBRR_CNT = 23	; Fosc=7.3728MHz U2X=1  --> BAUD=38400	(0.2%)

.equ	UBRR_CNT_9600 = 103	; baud 9600
;.equ	UBRR_CNT = 8332	; 120
;.equ	UBRR_CNT = 207	; 4800
;.equ	UBRR_CNT = 103	; 9600
;.equ	UBRR_CNT = 51	; Fosc=8MHz U2X=1  --> BAUD=19200	(0.2%)
.equ	UBRR_CNT = 25	; Fosc=8MHz U2X=1  --> BAUD=38400	(0.2%)
;.equ	UBRR_CNT = 16	; Fosc=8MHz U2X=1  --> BAUD=57600	(3.6%)
;.equ	UBRR_CNT = 12	; Fosc=8MHz U2X=1  --> BAUD=76800	(0.16%)
;.equ	UBRR_CNT = 8	; Fosc=8MHz U2X=1  --> BAUD=115200	(3.6%)

;.equ	UBRR_CNT = 71	; Fosc=11.0592MHz U2X=1  --> BAUD=19200	(0.2%)
;.equ	UBRR_CNT = 35	; Fosc=11.0592MHz U2X=1  --> BAUD=38400	(0.2%)


Proc SCIInit
	push	ax
	clr	al
	mov	ax,UBRR_CNT
	mov	UBRRH,ah
	mov	UBRRL,al	;low(UBRR_CNT)		; set baudrate
	setb	U2X			; set U2X=1, use double speed
	setb	RXCIE|RXEN|TXEN		; enable receive interrupt //|(1<<TXCIE) |(1<<UDRIE)
	setb	URSEL|USBS|UCSZ1|UCSZ0	; asynch / no parity / 2 stop / 8 bit data
	pop	ax
	ret
endp ----------------------------

Proc SCIInit_9600
	push	al
	clr	al
	mov	UBRRH,al
	mov	UBRRL,UBRR_CNT_9600	; set baudrate 9600
	setb	U2X			; set U2X=1, use double speed
	setb	RXCIE|RXEN|TXEN		; enable receive interrupt //|(1<<TXCIE) |(1<<UDRIE)
	setb	URSEL|USBS|UCSZ1|UCSZ0	; asynch / no parity / 2 stop / 8 bit data
	pop	al
	ret
endp ----------------------------


Proc WrSCI_	; al:value
&&w1:
	sbis	UCSRA,UDRE	; Wait for empty transmit buffer
	rjmp	&&w1
	out	UDR,al		; Put data (r16) into buffer, sends the data
	ret
endp ----------------------------

Proc	SCISendBuf
	setb	UDRIE		; kich hoat ngat truyen SCI
	
;	call	BlinkTXLED
	
	ret
endp ----------------------------

;--------------------------------
; For RS-485 haftduplex
;
Proc SetBusToSend
;	setb	CCtrl		; active rs-485 bus, client send to master
	ret
endp ----------------------------

Proc SetBusToFree
;	clr	CCtrl		; free bus, client receive from bus
	ret
endp ----------------------------
	
Proc StartFrame
;	mov	al,0xff
;	call	PutByte
;	mov	al,0x5a
;	call	PutByte
	ret
endp ----------------------------

Proc EndFrame
;	mov	al,0xff
;	call	PutByte
	ret
endp ----------------------------
	
Proc SendFrame
	call	SCISendBuf
	ret
endp ----------------------------
	

Proc PutACK
	push	al
	mov	al,0x5A
	call	PutByte
	pop	al
	ret
endp ----------------------------

Proc PutNACK
	clr	al
	call	PutByte
	ret
endp ----------------------------
	
;--------------------------------
; SCI receive interrupt
;
; Read from UDR will reset RXC flag
;
Proc USART_RXC_Int		; SCI rec interrupt when RXC flag (receive complete) is set
;USART_TXC_Int:			; Tx Rx dung chung trinh xu ly ngat
	pushf			; push a / mov	a,SREG / push a
	push	a
	push	ax
	push	di
	
	in	ah,UDR		; get byte to ah
	
; check RF init mode then jump to simple process
	mov	a,RFSetup
	test	a
	jz	&&nor
	jmp	&&SimpleMode
&&nor:
	
	lds	a,CodeL		; code length
	test	a		; CodeL is zero, da nhan het chuoi lenh?
	brne	&&n1		; jnz no, nhan tiep
	lds	a,BufCode	; yes, CodeL=0, bat dau chuoi lenh moi
	test	a		; lenh xu ly xong chua?
	brne	&&q		; jnz, lenh chua xu ly, quit
	sts	CodeL,ah	; byte dau tien la chieu dai chuoi lenh
	ldi	al,20
	sts	TCOut,al	; 20ms/command, set timeout cho mỗi lenh toi da 20ms
	rjmp	&&q
&&n1:
	dec	a		; moi lan nhan 1 byte thi dec CodeL
	sts	CodeL,a		; 
	lds	al,BufCode	; length byte string[0]
	inc	al	
	sts	BufCode,al	; chieu dai chuoi lenh
	lea	di,BufCode
	add	dil,al
	brcc	&&n2
	inc	dih
&&n2:
	st	di,ah		; store value
&&q:
;
; for testing...
;	mov	al,SCI_RX_LED_BIT
;	call	BlinkLed
;       cpl     Pump
	
	pop	di
	pop	ax
	pop	a
	popf			; pop	a / mov	SREG,a  / pop a
	reti
;-------
&&SimpleMode:
;	mov	al,ah
;	call	PutRXByte	; put to rx buffer
	jmp	&&q
endp ---------------------------

Proc USART_UDRE_Int		; transmit interrupt, call while UDR empty
	pushf
	push	ax
	
	call	GetByte		; --> al:byte
	jc	&&q1		; buffer empty
	out	UDR,al
&&q:
;
; for testing
;	mov	al,SCI_TX_LED_BIT
;	call	BlinkLed

	pop	ax
	popf
	reti
&&q1:
	clr	UDRIE		; disable UDRIE
	call	SetBusToFree
	jmp	&&q
endp ----------------------------
