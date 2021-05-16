set(srcs "main.c"
        "board.c"
        "ble_mesh/callbacks.c"
        "ble_mesh/bt.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS  ".")
