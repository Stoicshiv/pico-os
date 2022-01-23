commit d8f3eec5574a14a4e382612e71a5c959f89ff800

For some reason, both the '.boot_2_flash_second_stage' and '.boot_0_openocd_entry' are placed in the same segment of the executable.
This makes no sense, because they (should) have different load addresses.

### Notes

-   Now everything appears to be loaded correctly, however, we still don't appear to execute correctly.
    If we do the following:

    ```none
    break boot_0_openocd_entry
    monitor reset
    load
    continue
    ```

    We do hit the breakpoint and correctly continue executing somewhere in RAM.
    However, we do appear to crash in a random location.

    Notice that this is very fragile, because this is a software breakpoint.
    We can not place a hardware breakpoint in RAM.

-   It appears that there is no helper function in rom that can trigger a reset.

### Ideas

-   We should load the vector table, this is what the chip does otherwise?

-   I could write this in Assembler by hand, but that would be ugly.

-   The simple solution would be to write my own `memcpy` implementation quickly.

### Theories

-   The C++ compiler translates our precisely crafted code into an unaligned load instruction which obviously fails.
    Maybe they compiler needs some different flags to avoid this issue?

### Conclusions

-   It appears that the linker will do weird things unless we always specify both the memory area we want to run at, and the memory
    area we want to load at.
    In other words, we needed to explicitly say `> RAM AT > RAM` instead of just `> RAM`.