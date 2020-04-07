// Firmware to turn a rotary thermostat smarter
//
// https://hackaday.io/project/170693-old-thermostat-learns-new-tricks


// Hardware GPIO pins
#define ENCODER_A PB1
#define ENCODER_B PB0
#define CONTACT PB12
#define LED PC13

// Too small values make the thermostat loose encoder steps
#define ENCODER_DELAY 15

int encoder_states_by_phase[] = {0b00, 0b01, 0b11, 0b10};
int encoder_phases_by_state[] = {0, 1, 3, 2};
int set_point = 18;
int encoder_position;
uint8_t encoder_state;

int contact_state;

char message_buffer[20];
int received_bytes = 0;
bool debug = true;

void setup()
{
    pinMode(LED, OUTPUT);
    pinMode(CONTACT, INPUT);
    contact_state = digitalRead(CONTACT);
    digitalWrite(LED, LOW);
    init_encoder();
    Serial1.begin(9600);
    Serial1.println("Hello, I am a thermostat");
    digitalWrite(LED, HIGH);
}

void init_encoder()
{
    encoder_position = 0;
    uint8_t s = 0;
    if (digitalRead(ENCODER_A)) s |= 1;
    if (digitalRead(ENCODER_B)) s |= 2;
    encoder_state = s;
}

void loop()
{
    update_encoder_state();
    check_for_serial_data();

    if (set_point != encoder_position) {
        if (encoder_position < 0) {
            encoder_position = 0;
        } else if (encoder_position > 60) {
            encoder_position = 60;
        }

        set_point = encoder_position;
        print_state();
    }

    int new_contact_state = digitalRead(CONTACT);
    if (new_contact_state != contact_state) {
        contact_state = new_contact_state;
        print_state();
    }
}

void print_state()
{
    digitalWrite(LED, LOW);
    if (contact_state) {
        Serial1.print("I");
    } else {
        Serial1.print("H");
    }

    Serial1.println(set_point);
    digitalWrite(LED, HIGH);
}

void check_for_serial_data()
{
    if (Serial1.available() <= 0) {
        return;
    }

    char incoming_byte = Serial1.read();

    if (received_bytes >= sizeof(message_buffer)) {
        Serial1.println("E: Overflow");
        received_bytes = 0;
    }

    if (incoming_byte == '\r') {
        message_buffer[received_bytes++] = '\0';
        parse_received_command();
        received_bytes = 0;
    } else {
        message_buffer[received_bytes++] = incoming_byte;
    }
}

void parse_received_command()
{
    int set_point_request = atoi(message_buffer);

    if (message_buffer[0] < '0' || message_buffer[0] > '9' || set_point_request < 0 || set_point_request > 60) {
        Serial1.println("E: Range");
        return;
    }

    Serial1.print("<");
    Serial1.println(set_point_request, DEC);
    drive_encoder_to_value(set_point_request);
}

void set_encoder_phase(int phase)
{
    int new_state = encoder_states_by_phase[phase];

    digitalWrite(ENCODER_A, (new_state >> 1) & 1);
    digitalWrite(ENCODER_B, new_state & 1);
}

int get_encoder_phase()
{
    int state = (digitalRead(ENCODER_A) << 1) | digitalRead(ENCODER_B);

    return encoder_phases_by_state[state];
}

void drive_encoder_to_value(unsigned int requested_set_point)
{
    // Set the encoder pins to OUTPUT, preserving state
    int phase = get_encoder_phase();
    pinMode(ENCODER_B, OUTPUT);
    pinMode(ENCODER_A, OUTPUT);
    set_encoder_phase(phase);

    // Move CCW, enough to overflow but leaving the phase in such way that
    // going later to the set point will result in the phase unchanged.
    int reset_steps = 80 + (requested_set_point % 4);
    for (int i = 0; i < reset_steps; i++) {
        phase = (phase + 3) % 4;
        set_encoder_phase(phase);
        delay(ENCODER_DELAY);
    };

    // Move CC so to reach the set point, the phase after this will
    // be the same as it was before resetting, matching the state
    // of the real encoder switches.
    for (int i = 0; i < requested_set_point; i++) {
        phase = (phase + 1) % 4;
        set_encoder_phase(phase);
        delay(ENCODER_DELAY);
    }

    // Back to input mode. The state at this point will be the same
    // as it was when the pins went to OUTPUT mode.
    pinMode(ENCODER_B, INPUT);
    pinMode(ENCODER_A, INPUT);

    encoder_position = requested_set_point;
}

void update_encoder_state()
{
    /*
        new new old old
        B   A   B   A   result
        --  --  --  --  ------
        0   0   0   0   unchanged
        0   0   0   1   +1
        0   0   1   0   -1
        0   0   1   1   +2
        0   1   0   0   -1
        0   1   0   1   unchanged
        0   1   1   0   -2
        0   1   1   1   +1
        1   0   0   0   +1
        1   0   0   1   -2
        1   0   1   0   unchanged
        1   0   1   1   -1
        1   1   0   0   +2
        1   1   0   1   -1
        1   1   1   0   +1
        1   1   1   1   unchanged
   */

    uint8_t a_state = digitalRead(ENCODER_A);
    uint8_t b_state = digitalRead(ENCODER_B);
    uint8_t state = encoder_state & 3;

    if (a_state) state |= 4;
    if (b_state) state |= 8;
    encoder_state = (state >> 2);

    switch (state) {
    case 1:
    case 7:
    case 8:
    case 14:encoder_position++;
        return;
    case 2:
    case 4:
    case 11:
    case 13:encoder_position--;
        return;
    case 3:
    case 12:encoder_position += 2;
        return;
    case 6:
    case 9:encoder_position -= 2;
        return;
    }
}
