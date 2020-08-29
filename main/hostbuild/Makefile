OBJS = main.o pdp11_ch.o pdp11_cis.o pdp11_cpu.o pdp11_cpumod.o pdp11_cr.o pdp11_daz.o pdp11_dc.o 
OBJS += pdp11_dl.o pdp11_dmc.o pdp11_dup.o pdp11_dz.o pdp11_fp.o pdp11_hk.o pdp11_io.o pdp11_io_lib.o 
OBJS += pdp11_ke.o pdp11_kg.o pdp11_kmc.o pdp11_lp.o pdp11_pclk.o pdp11_pt.o pdp11_rc.o pdp11_rf.o 
OBJS += pdp11_rh.o pdp11_rk.o pdp11_rl.o pdp11_rom.o pdp11_rp.o pdp11_rq.o pdp11_rs.o pdp11_rx.o 
OBJS += pdp11_ry.o pdp11_stddev.o pdp11_sys.o pdp11_ta.o pdp11_tc.o pdp11_td.o pdp11_tm.o pdp11_tq.o
OBJS += pdp11_ts.o pdp11_tu.o pdp11_vh.o pdp11_vt.o pdp11_xq.o pdp11_xu.o scp.o sim_card.o sim_console.o 
OBJS += sim_disk.o sim_ether.o sim_fio.o sim_imd.o sim_serial.o sim_sock.o sim_tape.o sim_timer.o sim_tmxr.o sim_video.o
CFLAGS = -Wall -ggdb -I.. -DVM_PDP11=1
TARGET = pdp11
LDFLAGS = -lm

%.o: ../%.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: clean

