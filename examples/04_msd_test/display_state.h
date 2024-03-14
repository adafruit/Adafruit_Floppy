#pragma once

struct display_state {
  size_t capacity_kib;
  bool trk0, wp, rdy, dirty;
  int8_t trk, side;
};

display_state old_state, new_state;
