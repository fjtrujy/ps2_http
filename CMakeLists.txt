cmake_minimum_required(VERSION 3.10)

set(CMAKE_TOOLCHAIN_FILE $ENV{PS2SDK}/ps2dev.cmake)

project(ps2-http C)

# Three variants share the same sources and link libs and differ only in the
# PS2HTTP_VARIANT_* compile definition that picks which lwIP path is wired up
# in main.c:
#   ps2-http-ee.elf   — only the EE-side lwIP backend is linked.
#   ps2-http-iop.elf  — only the IOP-side ps2ip-nm/ps2ips backend is linked.
#   ps2-http.elf      — both backends linked, picked at runtime via ps2_http.cfg.
set(PS2HTTP_SOURCES main.c mongoose.c config.c mongoose.h config.h)
set(PS2HTTP_LIBS    kernel patches debug.so ps2_drivers)

function(ps2http_add_variant target_name variant_def)
    add_executable(${target_name} ${PS2HTTP_SOURCES})
    target_compile_definitions(${target_name} PRIVATE ${variant_def})
    target_link_libraries(${target_name} PRIVATE ${PS2HTTP_LIBS})
endfunction()

ps2http_add_variant(ps2-http-ee.elf  PS2HTTP_VARIANT_EE)
ps2http_add_variant(ps2-http-iop.elf PS2HTTP_VARIANT_IOP)
ps2http_add_variant(ps2-http.elf     PS2HTTP_VARIANT_BOTH)

# The combined ELF links both libps2ip.a (EE-lwIP) and libps2ips.a (IOP RPC
# client). Three globals collide:
#   - ip_addr_any  — same value (0.0.0.0 const) in both libs.
#   - erl_id, erl_dependancies — ERL loader metadata, only used when the
#     library is loaded as an .erl module; harmless when statically linked.
# Tell the linker that first wins; values are functionally identical.
target_link_options(ps2-http.elf PRIVATE -Wl,--allow-multiple-definition)
