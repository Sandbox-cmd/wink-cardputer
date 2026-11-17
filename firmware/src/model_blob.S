/* Wink's mind, placed in the chip's storage where it can be read like memory. */
    .section .rodata.winkmind,"a"
    .balign 4
    .global wink_model_start
wink_model_start:
    .incbin "model/wink.bin"
    .global wink_model_end
wink_model_end:
    .byte 0

/* The word pieces it reads and writes. */
    .section .rodata.winkpieces,"a"
    .balign 4
    .global wink_pieces_start
wink_pieces_start:
    .incbin "model/pieces.bin"
    .global wink_pieces_end
wink_pieces_end:
    .byte 0
