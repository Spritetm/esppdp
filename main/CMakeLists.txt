idf_component_register(SRCS "main.c"
					"ng.c" "pdp11_ch.c" "pdp11_cis.c" "pdp11_cpu.c" "pdp11_cpumod.c" "pdp11_cr.c" "pdp11_daz.c" "pdp11_dc.c" 
					"pdp11_dl.c" "pdp11_dmc.c" "pdp11_dup.c" "pdp11_dz.c" "pdp11_fp.c" "pdp11_hk.c" "pdp11_io.c" "pdp11_io_lib.c" "pdp11_ke.c" 
					"pdp11_kg.c" "pdp11_kmc.c" "pdp11_lp.c" "pdp11_ng.c" "pdp11_pclk.c" "pdp11_pt.c" "pdp11_rc.c" "pdp11_rf.c" "pdp11_rh.c" 
					"pdp11_rk.c" "pdp11_rl.c" "pdp11_rom.c" "pdp11_rp.c" "pdp11_rq.c" "pdp11_rs.c" "pdp11_rx.c" "pdp11_ry.c" "pdp11_stddev.c" 
					"pdp11_sys.c" "pdp11_ta.c" "pdp11_tc.c" "pdp11_td.c" "pdp11_tm.c" "pdp11_tq.c" "pdp11_ts.c" "pdp11_tu.c" "pdp11_vh.c" 
					"pdp11_vt.c" "pdp11_xq.c" "pdp11_xu.c" "scp.c" "sim_card.c" "sim_console.c" "sim_disk.c" "sim_ether.c" "sim_fio.c" 
					"sim_imd.c" "sim_serial.c" "sim_sock.c" "sim_tape.c" "sim_timer.c" "sim_tmxr.c" "sim_video.c" "vt11.c" 
                    INCLUDE_DIRS ".")

target_compile_options(${COMPONENT_TARGET} PRIVATE
        "-DVM_PDP11=1" -Wno-missing-field-initializers -Wno-address
    )
