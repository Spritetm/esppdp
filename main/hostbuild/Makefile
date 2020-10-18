OBJS = sim_evtq.o pdp11_cis.o pdp11_cpu.o pdp11_cpumod.o pdp11_fp.o pdp11_io.o pdp11_io_lib.o 
OBJS += pdp11_pt.o pdp11_rh.o pdp11_rl.o pdp11_rom.o pdp11_rp.o pdp11_rq.o 
OBJS += pdp11_rx.o pdp11_stddev.o pdp11_sys.o pdp11_xq.o scp.o
OBJS += sim_card.o sim_disk.o sim_ether.o sim_fio.o sim_imd.o sim_serial.o sim_sock.o 
OBJS += sim_timer.o sim_term.o hexdump.o wifi_if_tap.o
CFLAGS = -Wall -Wno-address -ggdb -I.. -DVM_PDP11=1 -Werror=implicit-function-declaration
TARGET = pdp11
LDFLAGS = -lm

%.o: ../%.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: clean

