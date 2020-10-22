idf_component_register(SRCS "main.c" "pdp11_cis.c" "pdp11_cpu.c" "pdp11_cpumod.c" "pdp11_fp.c" "pdp11_io.c" "pdp11_io_lib.c" "pdp11_pt.c" 
					"pdp11_rh.c" "pdp11_rl.c" "pdp11_rom.c" "pdp11_rp.c" "pdp11_rq.c" "pdp11_rx.c" "pdp11_stddev.c" "pdp11_sys.c" 
					"pdp11_xq.c" "scp.c" "sim_card.c" "sim_disk.c" "sim_ether.c" "sim_evtq.c" "sim_fio.c" "sim_imd.c" 
					"sim_serial.c" "sim_sock.c" "sim_term.c" "sim_timer.c" "bthid.c" "hexdump.c" "wifi_if_esp32.c" 
					"wifi_if_esp32_packet_filter.c"
                    INCLUDE_DIRS ".")

target_compile_options(${COMPONENT_TARGET} PRIVATE
        "-DVM_PDP11=1" -Wno-missing-field-initializers -Wno-address
    )
