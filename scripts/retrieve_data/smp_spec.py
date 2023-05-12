MGMT_OP_READ = 0
MGMT_OP_READ_RSP = 1
MGMT_OP_WRITE = 2
MGMT_OP_WRITE_RSP = 3

SMP_DICT = {
    "default": {
        "group_id": 0,
        "echo": {
            "cmd_id": 0,
            "request": {"op_code": MGMT_OP_WRITE, "json": {"d": ""}},
            "response": {"op_code": MGMT_OP_WRITE_RSP, "json_keys": ["r", "rc"]},
        },
        "reset": {"cmd_id": 5, "request": {"op_code": MGMT_OP_WRITE, "json": {}}},
    },
    "image_mgmt": {
        "group_id": 1,
        "state": {
            "cmd_id": 0,
            "get_request": {"op_code": MGMT_OP_READ, "json": {}},
            "get_response": {
                "op_code": MGMT_OP_READ_RSP,
                "json_keys": [
                    "image",
                    "slot",
                    "version",
                    "hash",
                    "bootable",
                    "pending",
                    "confirmed",
                    "active",
                    "permanent",
                ],
            },
            "set_request": {
                "op_code": MGMT_OP_WRITE,
                "json": {"hash": "", "confirm": False},
            },
            "set_response": {
                "op_code": MGMT_OP_WRITE_RSP,
                "json_keys": [
                    "image",
                    "slot",
                    "version",
                    "hash",
                    "bootable",
                    "pending",
                    "confirmed",
                    "active",
                    "permanent",
                ],
            },
        },
        "upload": {
            "cmd_id": 1,
            "request": {
                "op_code": MGMT_OP_WRITE,
                "json": {
                    "image": 0,
                    "len": 0,
                    "off": 0,
                    "sha": "",
                    "data": "",
                    "upgrade": False,
                },
            },
            "response": {
                "op_code": MGMT_OP_WRITE_RSP,
                "json_keys": ["off", "rc", "rsn"],
            },
        },
        "erase": {
            "cmd_id": 5,
            "request": {"op_code": MGMT_OP_WRITE, "json": {"slot": 0}},
            "response": {"op_code": MGMT_OP_WRITE_RSP, "json_keys": ["rc", "rsn"]},
        },
    },
    "file_management": {
        "group_id": 8,
        "upload": {
            "cmd_id": 2,
            "request": {
                "op_code": MGMT_OP_WRITE,
                "json": {"off": 0, "data": "", "name": "", "len": 0},
            },
            "response": {"op_code": MGMT_OP_WRITE_RSP, "json_keys": ["off", "rc"]},
        },
    },
    "shell_mgmt": {
        "group_id": 9,
        "shell": {
            "cmd_id": 0,
            "request": {
                "op_code": MGMT_OP_WRITE,
            },
            "response": {"op_code": MGMT_OP_WRITE_RSP, "json_keys": ["off", "rc"]},
        },
    },
    "etc": {
        "group_id": 65,
        "status": {
            "cmd_id": 0,
            "request": {"op_code": MGMT_OP_READ, "json": {}},
            "response": {"op_code": MGMT_OP_READ_RSP, "json_keys": []},
        },
        "record": {
            "cmd_id": 1,
            "request": {
                "op_code": MGMT_OP_READ,
                "json": {
                    "off": 0,
                },
            },
            "response": {"op_code": MGMT_OP_READ_RSP, "json_keys": []},
        },
        "clean": {
            "cmd_id": 2,
            "request": {
                "op_code": MGMT_OP_READ,
                "json": {},
            },
            "response": {"op_code": MGMT_OP_READ_RSP, "json_keys": []},
        },
    },
}
