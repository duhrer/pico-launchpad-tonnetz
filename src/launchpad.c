#include <stdint.h>
#include "launchpad.h"
#include "tusb.h"
#include <math.h>

// Common utility functions for all versions
enum NoteType get_type_for_note(int note_number) {
  int octave_offset = note_number % 12;

  switch (octave_offset) {
    case 0:
      return C_NATURAL;
      break;
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
      return SHARP_OR_FLAT;
      break;
    default:
      return NON_C_NATURAL;
  }
}

void clear_all_notes(struct board_state *board_state) {
  for (int a = 0; a < 128; a++) {
    if (board_state ->held_note_velocities[a]) {
      board_state->held_note_velocities[a] = 0;
    }

    if (board_state -> playing_note_velocities[a]) {
      board_state->playing_note_velocities[a] = 0;

      uint8_t note_off_message[3] = {
          MIDI_CIN_NOTE_OFF << 4, a, 0
      };

      // This should use cable 3.
      tud_midi_stream_write(3, note_off_message, sizeof note_off_message);
    }
  }
}


// End utility functions

// Begin version-specific functions.

void initialise_client_launchpads(void) {
  initialise_mk1_client_launchpads();
  initialise_mk2_client_launchpads();
  initialise_mk3_client_launchpads();
}

void initialise_mk1_client_launchpads(void) {
  // Change the button layout Change the button layout Change the button layout
  // Host » Launchpad: Channel 1: controller 0 set to 1 or 2.
  //  B0h, 00h, 01-02h (176, 0, 1-2). 
  uint8_t x_y_mode_packet[3] = {
    0xB0, 0x00, 1
  };

  // This should use cable 0.
  tud_midi_stream_write(0, x_y_mode_packet, sizeof(x_y_mode_packet));
}

void initialise_mk2_client_launchpads(void) {
  // Select "standalone" mode (it's the default, but for users who also use
  // Ableton, this will ensure things are set up properly).
  uint8_t standalone_mode_packet[9] = {
    0xf0, 0x00, 0x20, 0x29, 0x02, 0x10, 0x2C, 0x03, 0xf7
  };

  // Select "programmer" layout ("note" layout is the default)
  uint8_t programmer_layout_packet[9] = {
    0xf0, 0, 0x20, 0x29, 0x02, 0x10, 0x16, 0x3, 0xf7
  };

  // This should use cable 1.
  tud_midi_stream_write(1, standalone_mode_packet, sizeof(standalone_mode_packet));
  tud_midi_stream_write(1, programmer_layout_packet, sizeof programmer_layout_packet);
}

void initialise_mk3_client_launchpads(void) {
  // Select the programmer's layout, we want layout 11h and page 0
  // F0h 00h 20h 29h 02h 0Eh 00h <layout> <page> 00h F7h
  uint8_t select_programmers_layout[] = {
    0xF0, 0x00, 0x20, 0x29, 0x02, 0x0E, 0x00, 0x11, 0x00, 0x00, 0xF7
  };

  // They don't have a "clear all" method, just a sysex to send a value for
  // every pad, so we skip that.

  // This should use cable 2.
  tud_midi_stream_write(2, select_programmers_layout, sizeof select_programmers_layout);
}

void paint_client_launchpads(struct board_state *board_state) {
  paint_mk1_client_launchpads(board_state);
  paint_mk2_client_launchpads(board_state);
  paint_mk3_client_launchpads(board_state);
}

void paint_mk1_client_launchpads(struct board_state *board_state) {
  // There is a wacky mode for note on messages on channel 3 where the note is
  // one colour for one pad and the velocity is the colour for the next pad. You
  // blaze through them in sequnce from the top-left corner, which is not how
  // we're working on other devices, and is why we reverse the rows.

  // Send an initial (out of range) note to force any existing bulk update mode to end.
  uint8_t initial_note_on_message[3] = {
    MIDI_CIN_NOTE_ON << 4, 127, 0
  };

  tud_midi_stream_write(0, initial_note_on_message, sizeof(initial_note_on_message));

  for (int row = 7; row >= 0; row--) {
    // Shift by one column so that the square pads align on all units.
    for (int column = 1; column < 9; column += 2) {
      // First pad
      uint8_t note = 0x0c;
      int first_tuned_note = board_state->client.offset_by_cable[0] + (column * 3) + (row * 4);

      if (board_state->held_note_velocities[first_tuned_note] > 0) {
        note = 0x3C; // Green
      }
      else {
        enum NoteType first_tuned_note_type = get_type_for_note(first_tuned_note);
        if (first_tuned_note_type == C_NATURAL) {
          note = 0x0F; // Red
        }
        else if (first_tuned_note_type == NON_C_NATURAL) {
          note = 0x3E; // Yellow
        }
      }

      // Second pad
      uint8_t velocity = 0x0c;
      int second_tuned_note = board_state->client.offset_by_cable[0] + ((column + 1) * 3) + (row * 4);

      if (board_state->held_note_velocities[second_tuned_note] > 0) {
        velocity = 0x3C; // Green
      }
      else {
        enum NoteType second_tuned_note_type = get_type_for_note(second_tuned_note);
        if (second_tuned_note_type == C_NATURAL) {
          velocity = 0x0F;
        }
        else if (second_tuned_note_type == NON_C_NATURAL) {
          velocity = 0x3E;
        }
      }

      uint8_t note_on_message[3] = { 0x92, note, velocity };

      // The virtual port for the MK1 should be cable 0.
      tud_midi_stream_write(0, note_on_message, sizeof(note_on_message));
    }
  }
}

void paint_mk2_client_launchpads(struct board_state *board_state) {
  // Sysex messages used to paint the Launchpad in this pass....

  // The "paint all" operation doesn't support RGB, so you have to pick a colour
  // from the built-in 128 colour palette, for example, 0 for black and 3 for
  // white, 24 for green.
  //
  // Paint All F0h 00h 20h 29h 02h 10h 0Eh                                                                                                                                  <Colour> F7h
  uint8_t paint_all_sysex[9] = {
      0xf0, 0, 0x20, 0x29, 0x2, 0x10, 0xE, 0, 0xf7
  };

  // The virtual port for the MK2 should be cable 1.
  tud_midi_stream_write(1, paint_all_sysex, sizeof(paint_all_sysex));

  // We could do this all in one, but per row seems less involved.
  for (int row = 0; row < 8; row++) {
    // Paint a row using the canned colour palette.
    // F0h 00h 20h 29h 02h 10h 0Dh <Row> (<Colour> * 10) F7h
    uint8_t paint_row[19] = {
      0xf0, 0, 0x20, 0x29, 0x2, 0x10, 0xD, row + 1, 
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0xf7
    };

    for (int column = 0; column < 10; column++) {
      int tuned_note = board_state->client.offset_by_cable[1] + (column * 3) + (row * 4);
      enum NoteType tuned_note_type = get_type_for_note(tuned_note);

      if (board_state->held_note_velocities[tuned_note] > 0) {
        paint_row[8 + column] = 79; // Blue
      }
      else {
        if (tuned_note_type == C_NATURAL) {
          paint_row[8 + column] = 120;
        }
        else if (tuned_note_type == NON_C_NATURAL) {
          paint_row[8 + column] = 3;
        }
      }
    }

    // The virtual port for the MK2 should be cable 1.
    tud_midi_stream_write(1, paint_row, sizeof(paint_row));
  }
 
  // We currently use the "pulse" method for the side light.
  uint8_t paint_side_light[12] = {
    0xf0, 0x00, 0x20, 0x29, 0x2, 0x10, 0x28, 0x63, 3, 0xf7
  };

  // The virtual port for the MK2 should be cable 1.
  tud_midi_stream_write(1, paint_side_light, sizeof(paint_side_light));
}

void paint_mk3_client_launchpads(struct board_state *board_state) {
  // For now, use notes.
  for (int row = 0; row < 8; row++) {
    for (int column = 0; column < 10; column++) {
      int launchpad_note = (row * 10) + column;

      int tuned_note = board_state->client.offset_by_cable[2] + (column * 3) + (row * 4);
      enum NoteType tuned_note_type = get_type_for_note(tuned_note);

      uint8_t velocity = 0;

      if (tuned_note_type == C_NATURAL) {
        velocity = 120;
      }
      else if (tuned_note_type == NON_C_NATURAL) {
        velocity = 3;
      }

      uint8_t note_on_message[3] = {
        MIDI_CIN_NOTE_ON << 4, launchpad_note, velocity
      };

      // The virtual port for the MK3 should be cable 2.
      tud_midi_stream_write(2, note_on_message, sizeof(note_on_message));
    }
  }
}

void paint_host_launchpad(struct board_state *board_state) {
    if (board_state->host.launchpad_version == MK1) {
        paint_mk1_host_launchpad(board_state);
    }
    else if (board_state->host.launchpad_version == MK2) {
        paint_mk2_host_launchpad(board_state);
    }
    else if (board_state->host.launchpad_version == MK3) {
        paint_mk3_host_launchpad(board_state);
    }
}

// TODO: When we figure out sending sysex to the host's client device, we can simplify this.
void paint_mk1_host_launchpad(struct board_state *board_state) {
  // We use a different strategy here because the MK1 units skip notes between rows.
  for (int row = 0; row < 8; row++) {
      for (int column = 0; column < 8; column++) {
        int launchpad_note = (row * 16) + column;

        uint8_t velocity = 0x0C; // Black

        int tuned_note = board_state->host.offset + (column * 3) + (row * 4);


        if (board_state->held_note_velocities[tuned_note] > 0) {
          velocity = 0x3C; // Green
        }
        else {
          enum NoteType tuned_note_type = get_type_for_note(tuned_note);

          if (tuned_note_type == C_NATURAL) {
            velocity = 0x0F; // Red
          }
          else if (tuned_note_type == NON_C_NATURAL) {
            velocity = 0x3E; // Yellow
          }
        }

        uint8_t note_on_message[3] = {
          MIDI_CIN_NOTE_ON << 4, launchpad_note, velocity
        };

        tuh_midi_stream_write(board_state->host.client_idx, 0, note_on_message, sizeof(note_on_message));
      }
  }
}

// TODO: When we figure out sending sysex to the host's client device, we can simplify this.
void paint_mk2_host_launchpad(struct board_state *board_state) {
    // Write note messages for the host side until we figure out sysex there.
    for (int launchpad_note = 10; launchpad_note < 89; launchpad_note++) {
        int column = launchpad_note % 10;
        int row = ((launchpad_note - column)/10) - 1;

        int tuned_note = (board_state->host.offset) + (column * 3) + (row * 4);

        if (tuned_note < 128) {
          uint8_t velocity = 0;

          if (board_state->held_note_velocities[tuned_note] > 0) {
            velocity = 79; // Blue
          }
          else {
            enum NoteType tuned_note_type = get_type_for_note(tuned_note);

            if (tuned_note_type == C_NATURAL) {
              velocity = 120; // Red
            }
            else if (tuned_note_type == NON_C_NATURAL) {
              velocity = 3; // White
            }
          }

          uint8_t note_on_message[3] = {
            MIDI_CIN_NOTE_ON << 4, launchpad_note, velocity
          };

          // tuh_midi_stream_write(board_state->host.client_idx, 1, note_on_message, sizeof(note_on_message));
          tuh_midi_stream_write(0, 1, note_on_message, sizeof(note_on_message));
        }
    }
}

// TODO: When we figure out sending sysex to the host's client device, we can
// simplify this by using their sysex strategy (see the client implementation).
void paint_mk3_host_launchpad(struct board_state *board_state) {
    // Write note messages for the host side until we figure out sysex there.
    for (int launchpad_note = 1; launchpad_note < 99; launchpad_note++) {
        int launchpad_note_col = launchpad_note % 10;
        int launchpad_note_row = (launchpad_note - launchpad_note_col)/10;

        int tuned_note = board_state->host.offset + (launchpad_note_col * 3) + (launchpad_note_row * 4);
        enum NoteType tuned_note_type = get_type_for_note(tuned_note);

        uint8_t velocity = 0;

        if (tuned_note_type == C_NATURAL) {
          velocity = 120;
        }
        else if (tuned_note_type == NON_C_NATURAL) {
          velocity = 3;
        }
        
        uint8_t note_on_message[3] = {
          MIDI_CIN_NOTE_ON << 4, launchpad_note, velocity
        };

        // The MK3 wants data on the first cable, i.e. "MIDI" and not "DIN" or "DAW"
        tuh_midi_stream_write(board_state->host.client_idx, 0, note_on_message, sizeof(note_on_message));
    }
}

void process_incoming_host_packet(uint8_t *incoming_packet, struct board_state *board_state) {
    if (board_state->host.launchpad_version == MK1) {
        process_incoming_mk1_packet(incoming_packet, board_state, HOST);
    }
    else if (board_state->host.launchpad_version == MK2) {
        process_incoming_mk2_packet(incoming_packet, board_state, HOST);
    }
    else if (board_state->host.launchpad_version == MK3) {
        process_incoming_mk3_packet(incoming_packet, board_state, HOST);
    }
}

void process_incoming_client_packet(uint8_t *incoming_packet, struct board_state *board_state) {
    uint8_t cable = (incoming_packet[0] >> 4) & 0xf;

    // MK1
    if (cable == 0) {
      process_incoming_mk1_packet(incoming_packet, board_state, CLIENT);
    }
    // MK2
    else if (cable == 1) {
      process_incoming_mk2_packet(incoming_packet, board_state, CLIENT);
    }
    // MK3
    else if (cable == 2) {
      process_incoming_mk3_packet(incoming_packet, board_state, CLIENT);
    }
}

void increment_offset(struct board_state *board_state, enum HostOrClient hostOrClient, int cable, int increment) {
  if (hostOrClient == HOST) {
    board_state -> host.offset += increment;
  }
  else {
    board_state -> client.offset_by_cable[cable] += increment;
  }
}

// Respond to MK1 (Launchpad S) controls
void process_incoming_mk1_packet (uint8_t *incoming_packet, struct board_state *board_state, enum HostOrClient hostOrClient) {
  uint8_t data[3];
  memcpy(data, incoming_packet + 1, 3);

  int offset = hostOrClient == HOST ? board_state -> host.offset : board_state->client.offset_by_cable[0]; 

  // Start with the message type
  int type = data[0] >> 4;

  // Handle square pads (notes) and relevant round pads (outside columns)
  if (type == MIDI_CIN_NOTE_ON || type == MIDI_CIN_NOTE_OFF || type == MIDI_CIN_CONTROL_CHANGE || type == MIDI_CIN_POLY_KEYPRESS) {
    if (data[1] >=11 && data[1] <= 89) {
      // The Launchpad S has a weird layout, i.e. each row has 16 columns, 8
      // square pads, 1 circular pad, and 7 unused columns. It also has note
      // zero in the top row, which is the opposite of other Launchpad.  We're
      // also offset by one column, i.e. our column 0 is everyone else's column
      // 1.
      int column = (data[1] % 16 + 1);
      int row = 7 - trunc(data[1]/16);

      // Calculate the note from the row and ofset (our column zero is column
      // one everywhere else, so adjust that as well).
      //       int first_tuned_note = board_state->client.offset_by_cable[0] + (column * 3) + (row * 4);

      int tuned_note = offset + (column * 3) + (row * 4);

      if (tuned_note < 128) {
        // Store our velocity in board_state->held_note_velocities
        board_state->held_note_velocities[tuned_note] = data[2];

        board_state->is_dirty = true;
      }
    }
  }

  if (type == MIDI_CIN_CONTROL_CHANGE) {
    // Only react when a control is changed to a non-zero value, i.e. when it's
    // pressed, and not when it's released.
    if (data[2]) {
      switch(data[1]) {
        // Upward arrow
        case 104:
          if (offset <= 123) {
            increment_offset(board_state, hostOrClient, 0, 4);
            board_state->is_dirty = true;
            clear_all_notes(board_state);
          }
          break;
        // Downward arrow
        case 105:
          if (offset >= 4) {
            increment_offset(board_state, hostOrClient, 0, -4);
            board_state->is_dirty = true;
            clear_all_notes(board_state);
          }
          break;
        // Left Arrow
        case 106:
          if (offset >= 3) {
            increment_offset(board_state, hostOrClient, 0, -3);
            board_state->is_dirty = true;
            clear_all_notes(board_state);
          }
          break;
        // Right Arrow
        case 107:
          if (offset <= 124) {
            increment_offset(board_state, hostOrClient, 0, 3);
            board_state->is_dirty = true;
            clear_all_notes(board_state);
          }
          break;
        // Ignore everything else
        default:
          break;
      }
    }
  }
}

// Respond to MK2 controls
void process_incoming_mk2_packet (uint8_t *incoming_packet, struct board_state *board_state, enum HostOrClient hostOrClient) {
  uint8_t data[3];
  memcpy(data, incoming_packet + 1, 3);
  
  int offset = hostOrClient == HOST ? board_state -> host.offset : board_state->client.offset_by_cable[1]; 

  // Start with the message type
  int type = data[0] >> 4;

  // Handle square pads (notes) and relevant round pads (outside columns)
  if (type == MIDI_CIN_NOTE_ON || type == MIDI_CIN_NOTE_OFF || type == MIDI_CIN_POLY_KEYPRESS || type == MIDI_CIN_CONTROL_CHANGE) {
    uint8_t launchpad_note = data[1];

    if (launchpad_note >=10 && launchpad_note <= 89) {
      int column = launchpad_note % 10;
      int row = ((launchpad_note - column)/10) - 1;

      // Calculate the note from the row and ofset
      int tuned_note = offset + (column * 3) + (row * 4);

      if (tuned_note < 128) {
        // Store our velocity in board_state->held_note_velocities
        board_state->held_note_velocities[tuned_note] = data[2];

        board_state->is_dirty = true;
      }
    }    
  }

  if (type == MIDI_CIN_CONTROL_CHANGE) {
    // Only react when a control is changed to a non-zero value, i.e. when it's pressed, and not when it's released.
    if (data[2]) {
      switch(data[1]) {
        // Upward arrow
        case 91:
          if (offset <= 123) {
            increment_offset(board_state, hostOrClient, 1, 4);
            board_state->is_dirty = true;
            clear_all_notes(board_state);
          }
          break;
        // Downward arrow
        case 92:
          if (offset >= 4) {
            increment_offset(board_state, hostOrClient, 1, -4);
            board_state->is_dirty = true;
            clear_all_notes(board_state);
          }
          break;
        // Left Arrow
        case 93:
          if (offset >= 3) {
            increment_offset(board_state, hostOrClient, 1, -3);
            board_state->is_dirty = true;
            clear_all_notes(board_state);
          }
          break;
        // Right Arrow
        case 94:
          if (offset <= 124) {
            increment_offset(board_state, hostOrClient, 1, 3);
            board_state->is_dirty = true;
            clear_all_notes(board_state);
          }
          break;
        // Ignore everything else
        default:
          break;
      }
    }
  }
}

// Respond to MK3 controls
void process_incoming_mk3_packet (uint8_t *incoming_packet, struct board_state *board_state, enum HostOrClient hostOrClient) {
  uint8_t data[3];
  memcpy(data, incoming_packet + 1, 3);

  int offset = hostOrClient == HOST ? board_state -> host.offset : board_state->client.offset_by_cable[2]; 


  // Start with the message type
  int type = data[0] >> 4;

  // Handle square pads (notes) and relevant round pads (outside columns)
  if (type == MIDI_CIN_NOTE_ON || type == MIDI_CIN_NOTE_OFF || type == MIDI_CIN_POLY_KEYPRESS || type == MIDI_CIN_CONTROL_CHANGE) {
    if (data[1] >=11 && data[1] <= 89) {
      int column = data[1] % 10;
      int row = (data[1] - column)/10;


      // Calculate the note from the row and ofset
      int tuned_note = offset + (column * 3) + (row * 4);

      if (tuned_note < 128) {
        // Store our velocity in board_state -> held_note_velocities
        board_state->held_note_velocities[tuned_note] = data[2];

        board_state->is_dirty = true;
      }
    }    
  }

  if (type == MIDI_CIN_CONTROL_CHANGE) {
    // Only react when a control is changed to a non-zero value, i.e. when it's pressed, and not when it's released.
    if (data[2]) {
      switch(data[1]) {
        // Upward arrow
        case 80:
          if (offset <= 123) {
            increment_offset(board_state, hostOrClient, 2, 4);
            board_state->is_dirty = true;
            clear_all_notes(board_state);
          }
          break;
        // Downward arrow
        case 79:
          if (offset >= 4) {
            increment_offset(board_state, hostOrClient, 2, -4);
            board_state->is_dirty = true;
            clear_all_notes(board_state);
          }
          break;
        // Left Arrow
        case 91:
          if (offset >= 3) {
            increment_offset(board_state, hostOrClient, 2, -3);
            board_state->is_dirty = true;
            clear_all_notes(board_state);
          }
          break;
        // Right Arrow
        case 92:
          if (offset <= 124) {
            increment_offset(board_state, hostOrClient, 2, 3);
            board_state->is_dirty = true;
            clear_all_notes(board_state);
          }
          break;
        // Ignore everything else
        default:
          break;
      }
    }
  }
}

enum LaunchpadVersion get_launchpad_version (uint16_t idVendor, uint16_t idProduct) {
  enum LaunchpadVersion launchpad_version;
  launchpad_version = UNkNOWN;

  if (idVendor == 0x1235) {
    if (idProduct == 0x000E) {
      launchpad_version = MK1;
    }
    else if (idProduct >= 0x0051 && idProduct <= 0x0060) {
      launchpad_version = MK2;
    }
    else if (idProduct >= 0x0123 && idProduct <= 0x0132) {
      launchpad_version = MK3;
    }
  }

  return launchpad_version;
}