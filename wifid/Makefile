
PREFIX=pdp11-aout-
CC=$(PREFIX)gcc
CFLAGS=-static -msplit -DPDP
LDLAGS=-Wl,-T${PWD}/link.ld" -static -msplit -lc -Wl,--allow-multiple-definition
TARGET=wifid
OBJS=wifid.o

default: run

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS)

run: $(TARGET)
	curl -T $(TARGET) ftp://192.168.99.2 --user pdp11:esppdp11

real: $(TARGET)
	curl -T $(TARGET) ftp://192.168.5.104 --user pdp11:esppdp11

#echo "Press enter to get corefile"
#read
#rm -rf helloworld.core
#curl ftp://192.168.99.2/helloworld.core --output helloworld.core --user jeroen:jd1.com
