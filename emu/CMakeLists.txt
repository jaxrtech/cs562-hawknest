
target_include_directories(hawknest PRIVATE ./include)
target_sources(hawknest PRIVATE
        main.c
        fileio.c
        ines.c
        membus.c
        memory.c
        rc.c
        reset_manager.c
        shell.c
        timekeeper.c

        mos6502/mos6502.c
        mos6502/mos6502-common.c
        mos6502/vmcall.c

        nes/io_reg.c
        nes/mmc1.c
        nes/nrom.c
        nes/ppu.c
        nes/sxrom.c
        )