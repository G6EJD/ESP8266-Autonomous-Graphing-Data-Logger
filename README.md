# ESP8266-Autonomous-Graphing-Data-Logger
An ESP8266 is configured as a webserver using the Wi-Fi
Manager to read and log temperature and humidity from a variety of sensors
using the WemoS shields and then displays the results using Google Charts. Any
ESP8266 can be used.

The unit is totally self-contained and apart from google
charts and therefore requires no other access to external sites like MQTT or
Thingspeak.

Data is recorded to an SD-Card if fitted. It recovers data
from the SD-Card in the event of power failure or a restart and does this contiguously.

The log file can be checked for size, erased and streamed
out to a web browser for copy/paste into a spreadsheet.

The scale of the y-axis can be varied as required.

BEWARE: As Google notes in their documentation,
implementation of Google Charts is highly complex, if you modify the code,
don't be surprised if it stops working, this code is the culmination of my
extensive studies into the use of Google Charts through their documentation and
then my careful implementation and it also requires the correct use of the
ESP8266 server functions.  Some functions
may seem superfluous but they are not. Any adjustment of the HTML code or attributes
other than simple colour or font size changes is likely to make the code
non-operational.


