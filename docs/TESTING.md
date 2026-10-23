# Test Plan

Run these tests after assembling the circuit. Record actual results before marking a test as passed.

| ID | Test | Expected result | Result |
|---|---|---|---|
| T01 | Power on ESP32 | Firmware starts without repeated resets | Not yet recorded |
| T02 | MPU6050 initialization | Sensor responds over I²C | Not yet recorded |
| T03 | LCD initialization | LCD displays lock status | Not yet recorded |
| T04 | Correct gesture sequence | System unlocks for configured duration | Not yet recorded |
| T05 | Incorrect sequence | Sequence is rejected and failure feedback occurs | Not yet recorded |
| T06 | Unlock timeout | System returns to locked state | Not yet recorded |
| T07 | AP startup | `GestureLock` Wi-Fi appears | Not yet recorded |
| T08 | Dashboard access | `http://192.168.4.1` loads on connected device | Not yet recorded |
| T09 | Dashboard refresh | Status changes without full page reload | Not yet recorded |
| T10 | Power-bank operation | System remains running without laptop | Not yet recorded |
| T11 | Electrical checks | I²C levels and output drive are within component limits | Not yet recorded |

**Note:** These are planned tests, not claimed results. Fill in the Result column after testing the actual hardware.
