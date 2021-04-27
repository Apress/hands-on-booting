/* SPDX-License-Identifier: LGPL-2.1+ */

#include "fuzz.h"
#include "fuzz-journald.h"
#include "journald-audit.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
        Server s;

        dummy_server_init(&s, data, size);
        process_audit_string(&s, 0, s.buffer, size);
        server_done(&s);

        return 0;
}
