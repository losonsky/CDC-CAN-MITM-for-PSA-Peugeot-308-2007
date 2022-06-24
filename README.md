# CDC CAN MITM for PSA Peugeot 308 2007

Simple Arduino Micro based MITM for allowing custom texting on car's EMF and by-passing "Economy mode" on RD45 radio

Two CAN MCP2515's modules insertion between radio and rest of car CAN-INFO bus (cuting the CAN twisted pair connected to radio) allows filtering/mangling/dropping/spoofing/sniffing packets flowing between radio and the rest of car's CAN.

Radio needs only 3 kinds of messages to stay powered on, car's "power state" and last 8 chars of VIN (anti-theft beeping in speakers).

You print any incomming message even from radio or from rest of the car, forward it (posibly with changed content), drop it or whatever you want.
Arduino accepts simple message format on USB serial CDC and prints them on car's EMF display.
message starting with "0" i.e. <code>echo "0your magic text" >/dev/ttyACM0</code> will be scroolling text "your magic text" in the 1st line of EMF (where you can see radio station RDS name).
=> "0...." 1st RDS line
=> "1...." 2nd RDTXT line
=> "20" disable custom texts on EMF
=> "21" enable custom texts on EMF

