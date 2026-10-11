    .section .rodata
    .balign 4
    .global boot_sound_mp3_data
boot_sound_mp3_data:
    .incbin "src/system/assets/boot_sound.mp3"
    .balign 4
    .global button_sound_mp3_data
button_sound_mp3_data:
    .incbin "src/system/assets/button_sound.mp3"
    .balign 4
