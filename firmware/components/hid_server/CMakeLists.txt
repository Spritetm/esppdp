
idf_component_register(SRCS "hci_server.cpp" "hci_transport_esp32.cpp" "hid_server.cpp"
                    REQUIRES bt
                    INCLUDE_DIRS "." $ENV{IDF_PATH}/components/nvs_flash/include )

