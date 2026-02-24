#!/usr/bin/env bash
set -euo pipefail

thagore build tests/v0/ir/verify_valid.tg -o target/v0/bin/test_v0_ir_valid
./target/v0/bin/test_v0_ir_valid
thagore build tests/v0/parser/parse_hello_fn.tg -o target/v0/bin/test_v0_parser
./target/v0/bin/test_v0_parser
