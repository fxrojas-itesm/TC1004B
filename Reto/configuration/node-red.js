[

    {

        "id": "f6f2187d.f17ca8",

        "type": "tab",

        "label": "Flow 1",

        "disabled": false,

        "info": ""

    },

    {

        "id": "3cc11d24.ff01a2",

        "type": "comment",

        "z": "f6f2187d.f17ca8",

        "name": "WARNING: please check you have started this container with a volume that is mounted to /data\\n otherwise any flow changes are lost when you redeploy or upgrade the container\\n (e.g. upgrade to a more recent node-red docker image).\\n  If you are using named volumes you can ignore this warning.\\n Double click or see info side panel to learn how to start Node-RED in Docker to save your work",

        "info": "\nTo start docker with a bind mount volume (-v option), for example:\n\n```\ndocker run -it -p 1880:1880 -v /home/user/node_red_data:/data --name mynodered nodered/node-red\n```\n\nwhere `/home/user/node_red_data` is a directory on your host machine where you want to store your flows.\n\nIf you do not do this then you can experiment and redploy flows, but if you restart or upgrade the container the flows will be disconnected and lost. \n\nThey will still exist in a hidden data volume, which can be recovered using standard docker techniques, but that is much more complex than just starting with a named volume as described above.",

        "x": 350,

        "y": 80,

        "wires": []

    },

    {

        "id": "88862994e9e926ba",

        "type": "mqtt in",

        "z": "f6f2187d.f17ca8",

        "name": "",

        "topic": "switch0",

        "qos": "0",

        "datatype": "auto-detect",

        "broker": "cc210cd09781566c",

        "nl": false,

        "rap": true,

        "rh": 0,

        "inputs": 0,

        "x": 150,

        "y": 300,

        "wires": [

            [

                "a38992d6587b99dd",

                "7e4cc2b20c8e74e9"

            ]

        ]

    },

    {

        "id": "3e88c8356ed36994",

        "type": "mqtt in",

        "z": "f6f2187d.f17ca8",

        "name": "",

        "topic": "switch1",

        "qos": "0",

        "datatype": "auto-detect",

        "broker": "cc210cd09781566c",

        "nl": false,

        "rap": true,

        "rh": 0,

        "inputs": 0,

        "x": 150,

        "y": 360,

        "wires": [

            [

                "a38992d6587b99dd",

                "7e4cc2b20c8e74e9"

            ]

        ]

    },

    {

        "id": "a38992d6587b99dd",

        "type": "debug",

        "z": "f6f2187d.f17ca8",

        "name": "debug 1",

        "active": true,

        "tosidebar": true,

        "console": false,

        "tostatus": false,

        "complete": "false",

        "statusVal": "",

        "statusType": "auto",

        "x": 380,

        "y": 300,

        "wires": []

    },

    {

        "id": "7e4cc2b20c8e74e9",

        "type": "MSSQL",

        "z": "f6f2187d.f17ca8",

        "mssqlCN": "2484ea7e49afe94d",

        "name": "",

        "outField": "payload",

        "returnType": 0,

        "throwErrors": 1,

        "query": "INSERT INTO prj (timestamp, sensor, value)\r\nVALUES (GETDATE(), @sensor, @value);\r\n",

        "modeOpt": "queryMode",

        "modeOptType": "query",

        "queryOpt": "payload",

        "queryOptType": "editor",

        "paramsOpt": "",

        "paramsOptType": "editor",

        "rows": "rows",

        "rowsType": "msg",

        "parseMustache": true,

        "params": [

            {

                "output": false,

                "name": "sensor",

                "type": "VarChar",

                "valueType": "msg",

                "value": "topic",

                "options": {

                    "nullable": true,

                    "primary": false,

                    "identity": false,

                    "readOnly": false

                }

            },

            {

                "output": false,

                "name": "value",

                "type": "Real",

                "valueType": "msg",

                "value": "payload",

                "options": {

                    "nullable": true,

                    "primary": false,

                    "identity": false,

                    "readOnly": false

                }

            }

        ],

        "x": 380,

        "y": 360,

        "wires": [

            []

        ]

    },

    {

        "id": "cc210cd09781566c",

        "type": "mqtt-broker",

        "name": "",

        "broker": "mosquitto",

        "port": 1883,

        "clientid": "",

        "autoConnect": true,

        "usetls": false,

        "protocolVersion": 4,

        "keepalive": 60,

        "cleansession": true,

        "autoUnsubscribe": true,

        "birthTopic": "",

        "birthQos": "0",

        "birthRetain": "false",

        "birthPayload": "",

        "birthMsg": {},

        "closeTopic": "",

        "closeQos": "0",

        "closeRetain": "false",

        "closePayload": "",

        "closeMsg": {},

        "willTopic": "",

        "willQos": "0",

        "willRetain": "false",

        "willPayload": "",

        "willMsg": {},

        "userProps": "",

        "sessionExpiry": ""

    },

    {

        "id": "2484ea7e49afe94d",

        "type": "MSSQL-CN",

        "tdsVersion": "7_4",

        "name": "Microsoft SQL Server 2022",

        "server": "mssql",

        "port": "1433",

        "encyption": false,

        "trustServerCertificate": true,

        "database": "tc1004b",

        "useUTC": true,

        "connectTimeout": "15000",

        "requestTimeout": "15000",

        "cancelTimeout": "5000",

        "pool": "5",

        "parseJSON": false,

        "enableArithAbort": true,

        "readOnlyIntent": false

    },

    {

        "id": "b0ebac2aa8eb57a1",

        "type": "global-config",

        "env": [],

        "modules": {

            "node-red-contrib-mssql-plus": "0.13.1"

        }

    }

]

