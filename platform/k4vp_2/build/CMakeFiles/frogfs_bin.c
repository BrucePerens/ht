asm (
    ".section .rodata\n"
    ".balign 4\n"
    ".global frogfs_bin_len\n"
    "frogfs_bin_len:\n"
    ".int 268\n"
    ".global frogfs_bin\n"
    "frogfs_bin:\n"
    ".incbin \"/home/bruce/ht/platform/k4vp_2/build/CMakeFiles/frogfs.bin\"\n"
    ".balign 4\n"
    ".section .text\n"
);
