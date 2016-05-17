#!/bin/bash
./scripts/dtc/gen_dtb.py mwgeneric_stream_overlay.dts
scp mwgeneric_stream_overlay.dtb root@10.0.0.58:/lib/firmware/mwgeneric_stream_overlay.dtbo

