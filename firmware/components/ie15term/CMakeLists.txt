
idf_component_register(SRCS "ie15lcd.c"
                    REQUIRES driver
                    INCLUDE_DIRS "."  "../boards" $ENV{IDF_PATH}/components/nvs_flash/include $ENV{IDF_PATH}/components/esp_ringbuf/include $ENV{IDF_PATH}/components/driver/include $ENV{IDF_PATH}/components/vfs/include
                    EMBED_FILES chargen.bin)

