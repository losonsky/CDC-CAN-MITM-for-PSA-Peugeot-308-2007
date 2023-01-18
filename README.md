# CDC CAN MITM for PSA Peugeot 308 2007

Simple Arduino Micro based MITM for allowing custom texting on car's EMF and by-passing "Economy mode" on RD45 radio.

Two CAN MCP2515's modules insertion between radio and rest of car CAN-INFO bus (cuting the CAN twisted pair connected to radio) allows filtering/mangling/dropping/spoofing/sniffing packets flowing between radio and the rest of car's CAN.

Radio needs only 3 kinds of messages to stay powered on, car's "power state" and last 8 chars of VIN (anti-theft beeping in speakers).

You can print any incomming message even from radio or from rest of the car to Arduino's serial port, forward it between both CAN modules (i.e. with changed content before forwarding), drop it or whatever you want...
Arduino accepts simple message format on USB serial CDC and prints them on car's EMF display.
message starting with "0" i.e. <code>echo "0your magic text" >/dev/ttyACM0</code> will be scroolling text "your magic text" in the 1st line of EMF (where you can see radio station RDS name). Behavior for the 1st character in message follows:
<code>
=> "0any text" 1st RDS text line
=> "1lorem ipsum" 2nd RDTXT text line
=> "20" disables custom texts on EMF
=> "21" enables custom texts on EMF
=> "3" starts emulating key button sequence, which can be used for EMF's clock sync by relative manner (it's recomended to start this sequence around 48-th second of the minute and custom texting is disabled until the sequence ends)
</code>

Longer text lines are scrooling just because count of characters is limited on EMF in each line (i.e. length of RDS text field is only 8 characters). You can change the scrooling speed in sketch by simply changing related timers.

Personally I added simple 5V relay - by-pass (without Arduino powered through USB, it connects both CAN sides together so it's doing true-by-pass)
