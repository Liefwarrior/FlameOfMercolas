# Link options that make a mingw-w64 executable self-contained and reproducible.
#
# Apply this to EVERY executable, not just the game. A binary built without it
# links against libstdc++-6.dll, libgcc_s_seh-1.dll and libwinpthread-1.dll,
# which do not exist on a stock Windows box — it dies at startup with
# 0xC0000139 STATUS_ENTRYPOINT_NOT_FOUND before main() ever runs. That is
# exactly how granadad-tests.exe first shipped broken: the game had these flags
# and the test binary did not, so the game ran and the tests did not.

function(granadad_static_runtime target)
    if(NOT MINGW)
        return()
    endif()

    target_link_options(${target} PRIVATE
        # Fold the GCC runtimes into the .exe so it needs no DLLs beside it.
        -static-libgcc
        -static-libstdc++
        # winpthread has to come in whole — the posix threading model's
        # std::thread support lives here, and partial linking leaves dangling
        # __imp_ symbols.
        -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive -Wl,-Bdynamic
        # PE headers carry a link timestamp by default, which alone is enough to
        # make two identical builds hash differently. Reproducibility needs it
        # gone.
        -Wl,--no-insert-timestamp
    )
endfunction()
