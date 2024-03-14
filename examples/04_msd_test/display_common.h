#pragma once

bool ever_refreshed;

void update_display(bool force_refresh);

bool operator!=(const display_state &a, const display_state &b) {
  return a.capacity_kib != b.capacity_kib || a.trk0 != b.trk0 || a.wp != b.wp ||
         a.rdy != b.rdy || a.dirty != b.dirty || a.trk != b.trk ||
         a.side != b.side;
}

void maybe_update_display(bool force_refresh, bool tick) {
  noInterrupts();
  new_state = display_state{
      mfm_floppy.sectorCount() / 2,
      floppy.get_track0_sense(),
      floppy.get_write_protect(),
      !!digitalRead(READY_PIN),
      mfm_floppy.dirty(),
      floppy.track(),
      floppy.get_side(),
  };
  interrupts();

  force_refresh = force_refresh || !ever_refreshed;
  if (force_refresh || (old_state != new_state) || tick) {
    Serial.printf("P %d\n", new_state.trk);
    update_display(force_refresh);
    old_state = new_state;
    ever_refreshed = true;
  }
}
