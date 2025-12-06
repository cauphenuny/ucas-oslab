#pragma once

#include <os/mm.hpp>

extern "C" {
#include <os/lock.h>

/// @return 1: blocked, 0: immediately sent
int do_mbox_send(int mbox_idx, suva_t msg, int msg_length);

/// @return 1: blocked, 0: immediately received
int do_mbox_recv(int mbox_idx, suva_t msg, int msg_length);
}
