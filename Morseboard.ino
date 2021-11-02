/***********************************************************************
   This implements a USB-Keyboard using a standard Morse key. The morse
   key is wired to DigiSpark board that connects to a host machine as
   a USB HID keyboard.
   
   This code polls the Morse key, takes timings decodes Morse code and 
   sends it to the host machine as keypresses or as dots and dashes.
  
   I got the idea after watching a Mixtela video of his implementation on
   YouTube and I have taken a look at his code before writing my own.
  
   This uses an enhanced fork of the DigiKey library by Djanovic:
  
   https://hackaday.io/page/6947-digikeyboard-with-leds
   https://github.com/Danjovic/DigistumpArduino/tree/master/digistump-avr/libraries/DigisparkKeyboard
  
   His fork supports reading of the host keyboard status values NUM_LOCK, 
   NUM_LOCK and SCROLL_LOCK. There are used here to enable or disable 
   sound and morse-decoding.
  
   Num-Lock:    Toggle Buzzer
   Scroll-Lock: Toggle Decoder
 ***********************************************************************/
#include "DigiKeyboard.h"
#include "hidkeys.h" 

// GPIO-Pin definitions.
#define BUZZER_PIN 0  // Piezo-Buzzer output
#define LED_PIN 1     // Built-in LED on the Digispark
#define KEY_PIN 2     // Pin for the Morse-Key

// Names for Morse-Key states.
#define KEY_DOWN  LOW
#define KEY_UP    HIGH

// We will be operating as a state machine, the states and transitions are
// described in the handling functions below.
#define STATE_UNDEFINED -1
#define STATE_IDLING     0    
#define STATE_DOWN       1      
#define STATE_LONGDOWN   2  
#define STATE_RELEASE    3   

// Initial setting for the state-machine.
static int state = STATE_UNDEFINED;

// Other global state:
int keyState;                       // Current debounced state of the morse key
int lastKeyState = HIGH;            // Previous reading from the morse key input pin
unsigned long lastDebounceTime = 0; // Last time the morse key state has been seen as toggled
unsigned long debounceDelay = 50;   // Debounce dead-time, adjust through experimentation as required by the hardware 

// These are used to time the morse key up- and down-states.
unsigned long keyDownTime = 0;  // millis()-Value for when solid key down is detected.
unsigned long keyUpTime = 0;    // millis()-Value for when solid key up is detected.

// (Initial) timing of morse signals. Duration of the Base-Unit in Milliseconds.
unsigned long base = 100;

// Timing derived from the base unit:
unsigned long dit = 1;
unsigned long dah = 3;
unsigned long symbolspace = 1;
unsigned long characterspace = 3;
unsigned long wordspace = 7; 

// Global setting for wether we want to decode Morse into ASCII or dump Dits and Dahs.
// This can be changed through the "UI".
int Decode = 1;

// A buffer for symbols (dots and dashes) as collected. And a current pointer into that buffer.
static char symbolBuffer[10];
static char *symbol;

// A function for each state. These are continuously called from loop(). 

void IDLING() {
  // Nothing is pending or going on. Wait for key press.
  //   - If key is pressed: Note time of press -> DOWN.

  if (keyState == KEY_DOWN) {
    keyDownTime = millis();
    state = STATE_DOWN;
  }
}

void DOWN() {
  // Morse key has been pressed and is down. Wait for release or timeout.
  //   - If key is released: Note time of release, process as Dit or Dah -> RELEASE
  //   - If timeout: Send BACKSPACE -> LONGDOWN. 

  unsigned long keyDownInterval = millis() - keyDownTime;
  
  if (keyState == KEY_UP) {
    keyUpTime = millis();

    // Do we have space in the symbol buffer? If not just ignore.
    if (symbol != &symbolBuffer[sizeof(symbolBuffer)]) {
      // Yes.
      unsigned long ditInterval = base*dit;
      if (keyDownInterval <= 3*ditInterval/2) {
        // Store dit.
        *symbol++ = '.';
        
        // If we are not decoding send keystroke for the symbol immediately.
        if (!Decode) {
          DigiKeyboard.sendKeyStroke(KEY_DOT);
        }
      } else {
        // Store dah.
        *symbol++ = '-';
        
        // If we are not decoding send keystroke for the symbol immediately.
        if (!Decode) {
          DigiKeyboard.sendKeyStroke(KEY_KPAD_MINUS);
        }
      }
    }
          
    state = STATE_RELEASE;
  } else if (keyDownInterval >= base*wordspace) {
    DigiKeyboard.sendKeyStroke(KEY_BACKSP);
    state = STATE_LONGDOWN;
  }
}

void LONGDOWN() {   
  // Key has been held down for a long time. Wait indefinitely for it to be released. 
  //   - If key is released -> IDLING.
  if (keyState == KEY_UP) {
    state = STATE_IDLING;    
  }
}

void RELEASE() {
  // Key has been released. Wait for next key press or timeout.
  //   - If key is pressed: Note time of press -> DOWN.
  //   - If have symbols and charspace-time passed: Process symbols as character, clear symbol buffer.
  //   - If wordspace passed: Send SPACE -> IDLING

  unsigned long keyUpInterval = millis() - keyUpTime;
  
  if (keyState == KEY_DOWN) {
    keyDownTime = millis();
    state = STATE_DOWN;
  } else if ((keyUpInterval >= base*characterspace) && (symbol != &symbolBuffer[0])) {
    if (Decode) {
      decodeSymbol();
    } else {
      DigiKeyboard.sendKeyStroke(KEY_SPACE);
    }
    
    clearSymbolBuffer(); // We stay in state RELEASE, but without symbols queued up nothing happens.
  } else if (keyUpInterval >= base*wordspace) {
    if (Decode) {
      DigiKeyboard.sendKeyStroke(KEY_SPACE);
    } else {
      // If not decoding use triple space as word-separator.
      DigiKeyboard.sendKeyStroke(KEY_SPACE);
      DigiKeyboard.sendKeyStroke(KEY_SPACE);
      DigiKeyboard.sendKeyStroke(KEY_SPACE);
    }
    
    state = STATE_IDLING;
  }
}

// Set up a jump table of the state-handling functions.
typedef void(*StateFuncs)();

StateFuncs stateFuncs[] = {
  IDLING,
  DOWN,
  LONGDOWN,
  RELEASE
};

// Zero out the symbol buffer and set the pointer to the beginning.
void clearSymbolBuffer() {
  memset(symbolBuffer, 0, sizeof(symbolBuffer));
  symbol = &symbolBuffer[0];
}

void decodeSymbol() {
  // Decode the contents of the symbol buffer and send the character as key stroke.
  //
  // Instead of doing a table follow the tree shown in https://upload.wikimedia.org/wikipedia/commons/c/ca/Morse_code_tree3.png
  // This idea is lifted straight from Mixtelas version. It looks terrible, but is actually quite appropriate. I am sticking to
  // that view!
  //
  // An editor with solid support for folding is recommended if you plan on touching this.

  if (symbolBuffer[0] == '.') {
    if (symbolBuffer[1] == 0) {
      DigiKeyboard.sendKeyStroke(KEY_E, MOD_SHIFT_LEFT);
    } else if (symbolBuffer[1] == '.') {
      if (symbolBuffer[2] == 0) {
        DigiKeyboard.sendKeyStroke(KEY_I, MOD_SHIFT_LEFT);
      } else if (symbolBuffer[2] == '.') {
        if (symbolBuffer[3] == 0) {
          DigiKeyboard.sendKeyStroke(KEY_S, MOD_SHIFT_LEFT);
        } else if (symbolBuffer[3] == '.') {
          if (symbolBuffer[4] == 0) {
            DigiKeyboard.sendKeyStroke(KEY_H, MOD_SHIFT_LEFT);
          } else if (symbolBuffer[4] == '.') {
            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_5);
            } else {
              // decode-error
            }
          } else if (symbolBuffer[4] == '-') {
            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_4);
            } else {
              // decode-error
            }
          } else {
            // decode-error
          }
        } else if (symbolBuffer[3] == '-') {
          if (symbolBuffer[4] == 0) {
            DigiKeyboard.sendKeyStroke(KEY_V, MOD_SHIFT_LEFT);
          } else if (symbolBuffer[4] == '.') {
            // decode-error
          } else if (symbolBuffer[4] == '-') {
            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_3);
            } else {
              // decode-error
            }
          } else {
            // decode-error
          }
        } else {
          // decode-error
        }
      } else if (symbolBuffer[2] == '-') {
        if (symbolBuffer[3] == 0) {
          DigiKeyboard.sendKeyStroke(KEY_U, MOD_SHIFT_LEFT);
        } else if (symbolBuffer[3] == '.') {
          if (symbolBuffer[4] == 0) {
            DigiKeyboard.sendKeyStroke(KEY_F, MOD_SHIFT_LEFT);
          } else {
            // decode-error
          }
        } else if (symbolBuffer[3] == '-') {
          if (symbolBuffer[4] == 0) {
            // DigiKeyboard.sendKeyStroke(KEY_UE);
          } else if (symbolBuffer[4] == '-') {
            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_2);
            } else {
              // decode-error
            }
          } else if (symbolBuffer[4] == '.') {
            if (symbolBuffer[5] == '.') {
              if (symbolBuffer[6] == 0) {
                DigiKeyboard.sendKeyStroke(KEY_SLASH, MOD_SHIFT_LEFT); // ?
              } else {
                // decode-error
              }
            } else if (symbolBuffer[5] == '-') {
              if (symbolBuffer[6] == 0) {
                DigiKeyboard.sendKeyStroke(KEY_MINUS, MOD_SHIFT_LEFT); // _
              } else {
                // decode-error
              }
            } else {
              // decode-error
            }
          } else {
            // decode-error
          }
        } else {
          // decode-error
        }
      } else {
        // decode-error
      }
    } else if (symbolBuffer[1] == '-') {
      if (symbolBuffer[2] == 0) {
        DigiKeyboard.sendKeyStroke(KEY_A, MOD_SHIFT_LEFT);
      } else if (symbolBuffer[2] == '.') {
        if (symbolBuffer[3] == 0) {
          DigiKeyboard.sendKeyStroke(KEY_R, MOD_SHIFT_LEFT);
        } else if (symbolBuffer[3] == '.') {
          if (symbolBuffer[4] == 0) {
            DigiKeyboard.sendKeyStroke(KEY_L, MOD_SHIFT_LEFT);
          } else {
            // decode-error
          }
        } else if (symbolBuffer[3] == '-') {
          if (symbolBuffer[4] == 0) {
            // DigiKeyboard.sendKeyStroke(KEY_AE, MOD_SHIFT_LEFT);
          } else if (symbolBuffer[4] == '.') {
            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_EQUAL, MOD_SHIFT_LEFT); // +
            } else if (symbolBuffer[5] == '-') {
              if (symbolBuffer[6] == 0) {
                DigiKeyboard.sendKeyStroke(KEY_DOT);
              } else {
                // decode-error
              }
            } else {
              // decode-error
            }
          } else if (symbolBuffer[4] == '.') {
            if (symbolBuffer[6] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_EQUAL, MOD_SHIFT_LEFT);
            } else {
              // decode-error
            }
          } else {
            // decode-error
          }
        } else {
          // decode-error
        }
      } else if (symbolBuffer[2] == '-') {
        if (symbolBuffer[3] == '.') {
          if (symbolBuffer[4] == 0) {
            DigiKeyboard.sendKeyStroke(KEY_P, MOD_SHIFT_LEFT);
          } else {
            // decode-error
          }
        } else if (symbolBuffer[3] == '-') {
          if (symbolBuffer[4] == 0) {
            DigiKeyboard.sendKeyStroke(KEY_J, MOD_SHIFT_LEFT);
          } else if (symbolBuffer[4] == '-') {
            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_1);
            } else {
              // decode-error
            }
          } else {
            // decode-error
          }
        } else {
          DigiKeyboard.sendKeyStroke(KEY_W, MOD_SHIFT_LEFT);
        }
      } else {
        // decode-error
      }
    } else {
      // decode-error
    }
  } else if (symbolBuffer[0] == '-') {
    if (symbolBuffer[1] == 0) {
      DigiKeyboard.sendKeyStroke(KEY_T, MOD_SHIFT_LEFT);
    } else if (symbolBuffer[1] == '.') {
      if (symbolBuffer[2] == '.') {
        if (symbolBuffer[3] == '.') {
          if (symbolBuffer[4] == '.') {
            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_6);
            } else if (symbolBuffer[5] == '-') {
              DigiKeyboard.sendKeyStroke(KEY_KPAD_MINUS);
            } else {
              // decode-error
            }
          } else if (symbolBuffer[4] == '-') {
            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_EQUAL);
            }
          } else {
            DigiKeyboard.sendKeyStroke(KEY_B, MOD_SHIFT_LEFT);
          }
        } else if (symbolBuffer[3] == '-') {
          if (symbolBuffer[4] == 0) {
            DigiKeyboard.sendKeyStroke(KEY_X, MOD_SHIFT_LEFT);
          } else if (symbolBuffer[4] == '.') {
            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_SLASH);
            } else {
              // decode-error
            }
          } else if (symbolBuffer[4] == '-') {
            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_X, MOD_SHIFT_LEFT);
            } else {
              // decode-error
            }
          } else {
            // decode-error
          }
        } else {
          DigiKeyboard.sendKeyStroke(KEY_D, MOD_SHIFT_LEFT);
        }
      } else if (symbolBuffer[2] == '-') {
        if (symbolBuffer[3] == '.') {
          if (symbolBuffer[4] == 0) {
            DigiKeyboard.sendKeyStroke(KEY_C, MOD_SHIFT_LEFT);
          } else {
            // decode-error
          }
        } else if (symbolBuffer[3] == '-') {
          if (symbolBuffer[4] == 0) {
            DigiKeyboard.sendKeyStroke(KEY_Y, MOD_SHIFT_LEFT);
          } else {
            // decode-error
          }
        } else if (symbolBuffer[3] == 0) {
          DigiKeyboard.sendKeyStroke(KEY_K, MOD_SHIFT_LEFT);
        } else {
          // decode-error
        }
      } else if (symbolBuffer[2] == 0) {
        DigiKeyboard.sendKeyStroke(KEY_N, MOD_SHIFT_LEFT);
      } else {
        // decode-error
      }
    } else if (symbolBuffer[1] == '-') {
      if (symbolBuffer[2] == '.') {
        if (symbolBuffer[3] == '.') {
          if (symbolBuffer[4] == '.') {

            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_7);
            } else {
              // decode-error
            }
          } else if (symbolBuffer[4] == '-') {
            if (symbolBuffer[5] == '-') {
              if (symbolBuffer[6] == 0) {
                DigiKeyboard.sendKeyStroke(KEY_COMMA);
              } else {
                // decode-error
              }
            } else {
              // decode-error
            }
          } else if (symbolBuffer[4] == 0) {
            DigiKeyboard.sendKeyStroke(KEY_Z, MOD_SHIFT_LEFT);
          } else {
            // decode-error
          }
        } else if (symbolBuffer[3] == '-') {
          if (symbolBuffer[4] == 0)
            DigiKeyboard.sendKeyStroke(KEY_Q, MOD_SHIFT_LEFT);
        } else {
          DigiKeyboard.sendKeyStroke(KEY_G, MOD_SHIFT_LEFT);
        }
      } else if (symbolBuffer[2] == '-') {
        if (symbolBuffer[3] == '.') {
          if (symbolBuffer[4] == '.') {
            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_8);
            } else {
              // decode-error
            }
          } else if (symbolBuffer[4] == '-') {
            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_SEMICOLON, MOD_SHIFT_LEFT);
            } else {
              // decode-error
            }
          } else {
            // decode-error
          }
        } else if (symbolBuffer[3] == '-') {
          if (symbolBuffer[4] == '-') {
            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_0);
            } else {
              // decode-error
            }
          } else if (symbolBuffer[4] == '.') {
            if (symbolBuffer[5] == 0) {
              DigiKeyboard.sendKeyStroke(KEY_9);
            } else {
              // decode-error
            }
          } else {
            // decode-error
          }
        } else {
          DigiKeyboard.sendKeyStroke(KEY_O, MOD_SHIFT_LEFT);
        }
      } else {
        DigiKeyboard.sendKeyStroke(KEY_M, MOD_SHIFT_LEFT);
      }
    } else {
      // decode-error
    }
  } else {
    // decode-error
  }
}

/*
// Plays a dit on the buzzer.
void play_dit() {
  analogWrite(BUZZER_PIN, 128); 
  DigiKeyboard.delay(base*dit);
  analogWrite(BUZZER_PIN, 0);
}

// Plays a dah on the buzzer.
void play_dah() {
  analogWrite(BUZZER_PIN, 128); 
  DigiKeyboard.delay(base*dah);
  analogWrite(BUZZER_PIN, 0);
}

// Takes a string of dots, dashes and spaces and plays it out on the buzzer
void play_morse(char *morse) {
  char *c = morse;
  while (*c != 0) {
    if (*c == '.') {
      play_dit();
      DigiKeyboard.delay(base*symbolspace);
    } else if (*c == '-') {
      play_dah();
      DigiKeyboard.delay(base*symbolspace);
    } else if (*c == ' ') {
      DigiKeyboard.delay(base*characterspace);
    }
    c++;
  }
}

void play_char(char *c) {
  if (*c == 'A') { play_morse(".-"); }
  else if (*c == 'B') { play_morse("-..") ; }
  else if (*c == 'C') { play_morse("-...") ; }
  else if (*c == 'D') { play_morse("-.-.") ; }
  else if (*c == 'E') { play_morse(".") ; }
  else if (*c == 'F') { play_morse("..-.") ; }
  else if (*c == 'G') { play_morse("--.") ; }
  else if (*c == 'H') { play_morse("....") ; }
  else if (*c == 'I') { play_morse("..") ; }
  else if (*c == 'J') { play_morse(".---") ; }
  else if (*c == 'K') { play_morse("-.-") ; }
  else if (*c == 'L') { play_morse(".-..") ; }
  else if (*c == 'M') { play_morse("--") ; }
  else if (*c == 'N') { play_morse("-.") ; }
  else if (*c == 'O') { play_morse("---") ; }
  else if (*c == 'P') { play_morse(".--.") ; }
  else if (*c == 'Q') { play_morse("--.-") ; }
  else if (*c == 'R') { play_morse(".-.") ; }
  else if (*c == 'S') { play_morse("...") ; }
  else if (*c == 'T') { play_morse("-") ; }
  else if (*c == 'U') { play_morse("..-") ; }
  else if (*c == 'V') { play_morse("..-") ; }
  else if (*c == 'W') { play_morse(".--") ; }
  else if (*c == 'X') { play_morse("-..-") ; }
  else if (*c == 'Y') { play_morse("-.--") ; }
  else if (*c == 'Z') { play_morse("--..") ; }
  else if (*c == '0') { play_morse("-----") ; }
  else if (*c == '1') { play_morse(".----") ; }
  else if (*c == '2') { play_morse("..---") ; }
  else if (*c == '3') { play_morse("...--") ; }
  else if (*c == '4') { play_morse("....-") ; }
  else if (*c == '5') { play_morse(".....") ; }
  else if (*c == '6') { play_morse("-....") ; }
  else if (*c == '7') { play_morse("--...") ; }
  else if (*c == '8') { play_morse("---..") ; }
  else if (*c == '9') { play_morse("----.") ; }
  
  DigiKeyboard.delay(base*characterspace);
}

void play(char *msg) {
  char *c = msg;
  while (*c != 0) {
    play_char(c);
    c++;
  }
}
*/

void setup() { 
  // Set up pins and turn Buzzer and LED off.
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
     
  pinMode(BUZZER_PIN, OUTPUT);
  analogWrite(BUZZER_PIN, 0);   

  pinMode(KEY_PIN, INPUT_PULLUP);
  
  // Prepare symbol buffer.
  clearSymbolBuffer();
  
  // Is the key down as we start up?
  int pin = digitalRead(KEY_PIN);
  if (pin == KEY_DOWN) {
    // Yes. Set to dots-and-dashes-mode instead of decoding
    Decode = 0;
  
    // Wait for key-release before actually starting up. One-time, 
    // no debounce required here.
    while (digitalRead(KEY_PIN) == KEY_DOWN) {
      DigiKeyboard.delay(50);
    }
  }

  // Set initial state.
  state = STATE_IDLING;
}

void loop() {
  // "It is essential that DigiKeyboard.update() is called regularly."
  DigiKeyboard.update();
  
  // Read the instantaneous state of the morse key.
  int pin = digitalRead(KEY_PIN);

  // If the key state has changed, due to noise or actual press:
  if (pin != lastKeyState) {
    // Reset the debounce-timer
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    // Whatever the reading is, it's been there for longer than the debounce
    // delay, so take it as the actual current state:

    // If the debounced key state has changed:
    if (pin != keyState) {
      keyState = pin;
      
      // Update decoder-enable based on Scroll Lock.
      Decode = (!(DigiKeyboard.getLEDs() & SCROLL_LOCK));
      
      if (keyState == KEY_DOWN) {
        // Key is now down. LED on. Only turn on buzzer if Num Lock is on.
        if (DigiKeyboard.getLEDs() & NUM_LOCK) {    
          analogWrite(BUZZER_PIN, 128);
        } 
        
        digitalWrite(LED_PIN, HIGH);     
      } else {
        // Key is now up. Buzzer and LED off.
        analogWrite(BUZZER_PIN, 0); 
        digitalWrite(LED_PIN, LOW);
      }  
    }
  }

  lastKeyState = pin;
  
  // Call appropriate function from the state-machine table to process current state.
  stateFuncs[state](); 
}
