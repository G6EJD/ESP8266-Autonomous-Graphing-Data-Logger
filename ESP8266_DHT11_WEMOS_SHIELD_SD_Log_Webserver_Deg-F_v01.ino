/* ESP8266 plus WEMOS DHT11 Sensor with a Temperature and Humidity Web Server
 Automous display of sensor results on a line-chart, gauge view and the ability to export the data via copy/paste for direct input to MS-Excel
 The 'MIT License (MIT) Copyright (c) 2016 by David Bird'. Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, 
 distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
 following conditions: 
   The above copyright ('as annotated') notice and this permission notice shall be included in all copies or substantial portions of the Software and where the
   software use is visible to an end-user.
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
See more at http://dsbird.org.uk
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>     // https://github.com/tzapu/WiFiManager
#include <SPI.h>
#include <SD.h>              // Needs to be version 1.0.9 to work with ESP8266
#include <time.h>        
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#define DHTPIN     D4  // Pin which is connected to the DHT sensor.
// Uncomment the type of sensor in use:
#define DHTTYPE           DHT11     // DHT 11 
//#define DHTTYPE           DHT22     // DHT 22 (AM2302)
//#define DHTTYPE           DHT21     // DHT 21 (AM2301)
DHT_Unified dht(DHTPIN, DHTTYPE,16); // Use 16 for the ESP8266

String version = "v1.0";      // Version of this program
WiFiClient client;
ESP8266WebServer server(80); // Start server on port 80 (default for a web-browser, change to your requirements, e.g. 8080 if your Router uses port 80
                             // To access server from the outsid of a WiFi network e.g. ESP8266WebServer server(8266); and then add a rule on your Router that forwards a
                             // connection request to http://your_network_ip_address:8266 to port 8266 and view your ESP server from anywhere.
                             // Example http://g6ejd.uk.to:8266 will be directed to http://192.168.0.40:8266 or whatever IP address your router gives to this server

int       log_time_unit = 15;   // default is 1-minute between readings, 10=15secs 40=1min 200=5mins 400=10mins 2400=1hr 
int       time_reference= 60;   // Time reference for calculating /log-time (nearly in secs) to convert to minutes
int const table_size    = 288;  // 300 is about the maximum for the available memory, so use 12 samples/hour * 24 * 1-day = 288
int       index_ptr, timer_cnt, log_interval, log_count, max_temp, min_temp;
String    webpage,time_now,log_time,lastcall;
bool      SD_present, batch_not_written, AScale, auto_smooth, AUpdate, log_delete_approved;
float     dht_temp,dht_humi;

typedef struct {
  int     lcnt;  // Sequential log count
  String ltime;  // Time reading taken
  sint16_t temp; // Temperature values, short unsigned 16-bit integer to reduce memory requirement, saved as x10 more to preserve 0.1 resolution
  sint16_t humi; // Humidity values, short unsigned 16-bit integer to reduce memory requirement, saved as x10 more to preserve 0.1 resolution
} record_type;

record_type sensor_data[table_size+1]; // Define the data array

void setup() {
  Serial.begin(115200);
  //WiFiManager intialisation. Once completed there is no need to repeat the process on the current board
  WiFiManager wifiManager;
  // New OOB ESP8266 has no Wi-Fi credentials so will connect and not need the next command to be uncommented and compiled in, a used one with incorrect credentials will
  // so restart the ESP8266 and connect your PC to the wireless access point called 'ESP8266_AP' or whatever you call it below in ""
  // wifiManager.resetSettings(); // Command to be included if needed, then connect to http://192.168.4.1/ and follow instructions to make the WiFi connection
  // Set a timeout until configuration is turned off, useful to retry or go to sleep in n-seconds
  wifiManager.setTimeout(180);
  //fetches ssid and password and tries to connect, if connections succeeds it starts an access point with the name called "ESP8266_AP" and waits in a blocking loop for configuration
  if(!wifiManager.autoConnect("ESP8266_AP")) {
    Serial.println(F("failed to connect and timeout occurred"));
    delay(3000);
    ESP.reset(); //reset and try again
    delay(5000);
  }
  // At this stage the WiFi manager will have successfully connected to a network, or if not will try again in 180-seconds
  //----------------------------------------------------------------------
  Serial.println(F("WiFi connected.."));
  configTime(0 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  server.begin(); Serial.println(F("Webserver started...")); // Start the webserver
  Serial.println("Use this URL to connect: http://"+WiFi.localIP().toString()+"/");// Print the IP address
  //----------------------------------------------------------------------
  Serial.print(F("Initializing SD card..."));
  if (!SD.begin(D8)) { // see if the card is present and can be initialised. Wemos SD-Card CS uses D8
    Serial.println(F("Card failed, or not present"));
    SD_present = false;
  } else SD_present = true;
  if (SD_present) Serial.println(F("Card initialised.")); else Serial.println(F("Card not present, no SD Card data logging possible"));
  dht.begin();
  //----------------------------------------------------------------------  
  server.on("/", systemSetup); // The client connected with no arguments e.g. http:192.160.0.40/
  server.on("/TempHumi", display_temp_and_humidity);
  server.on("/TempDewp", display_temp_and_dewpoint);
  server.on("/Dialview", display_dial);
  server.on("/AScale",   auto_scale);
  server.on("/AUpdate",  auto_update);
  server.on("/Setup",    systemSetup);
  server.on("/Help",     help);
  server.on("/MaxT_U",   max_temp_up);
  server.on("/MaxT_D",   max_temp_down);
  server.on("/MinT_U",   min_temp_up);
  server.on("/MinT_D",   min_temp_down);
  server.on("/LogT_U",   logtime_up);
  server.on("/LogT_D",   logtime_down);
  if (SD_present) {
    server.on("/SDview",  SD_view);
    server.on("/SDerase", SD_erase);
    server.on("/SDstats", SD_stats);
  }
  configTime(0 * 3600, 0, "pool.ntp.org", "time.nist.gov");  // Start time server
  index_ptr    = 0;     // The array pointer that varies from 0 to table_size  
  log_count    = 0;     // Keeps a count of readings taken
  AScale    = false;    // Google charts can AScale axis, this switches the function on/off
  max_temp     = 30;    // Maximum displayed temperature as default
  min_temp     = -10;   // Minimum displayed temperature as default
  auto_smooth  = false; // If true, transitions of more than 10% between readings are smoothed out, so a reading followed by another that is 10% higher or lower is averaged 
  AUpdate   = true;     // Used to prevent a command from continually auto-updating, for example increase temp-scale would increase every 30-secs if not prevented from doing so.
  lastcall     = "temp_humi";      // To determine what requested the AScale change
  log_interval = log_time_unit*20; // inter-log time interval, default is 5-minutes between readings, 10=15secs 40=1min 200=5mins 400=10mins 2400=1hr 
  timer_cnt    = log_interval + 1; // To trigger first table update, essential
  update_log_time();           // Update the log_time
  log_delete_approved = false; // Used to prevent accidental deletion of card contents, requires two approvals
  reset_array();               // Clear storage array before use 
  prefill_array();             // Load old data from SD-Card back into display and readings array
  //Serial.println(system_get_free_heap_size()); // diagnostic print to check for available RAM
  time_t now = time(nullptr); 
  delay(2000); // Wait for time to start
  //Serial.println(time(&now)); // Unix time epoch
  Serial.print(F("Logging started at: ")); Serial.println(calcDateTime(time(&now)));
  /*you can also obtain time and date like this
  struct tm *now_tm;
  int hour,min,second,day,month,year;
  now = time(NULL);
  now_tm = localtime(&now);
  hour = now_tm->tm_hour;
  min  = now_tm->tm_min;
  second = now_tm->tm_sec;
  day    = now_tm->tm_mday;
  month  = now_tm->tm_mon;
  year   = now_tm->tm_year + 1900;
  Serial.print(hour);Serial.print(":");Serial.print(min);Serial.print(":");Serial.println(second);
  Serial.print(day);Serial.print("/");Serial.print(month);Serial.print("/");Serial.println(year);
  */
}

void loop() {
  server.handleClient();
  sensor_t sensor;
  sensors_event_t event; 
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature))       Serial.println("Error reading temperature!"); else {
    dht_temp = event.temperature*10;
    dht_temp = dht_temp * 9.0 / 5.0 + 32; // To Deg-F
  }
  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity)) Serial.println("Error reading humidity!");    else dht_humi = event.relative_humidity*10;
  
  time_t now = time(nullptr); 
  time_now = String(ctime(&now)).substring(0,24); // Remove unwanted characters
  if (time_now != "Thu Jan 01 00:00:00 1970" and timer_cnt >= log_interval) { // If time is not yet set, returns 'Thu Jan 01 00:00:00 1970') so wait. 
    timer_cnt = 0;  // log_interval values are 10=15secs 40=1min 200=5mins 400=10mins 2400=1hr
    log_count += 1; // Increase logging event count
    sensor_data[index_ptr].lcnt  = log_count;  // Record current log number, time, temp and humidity readings 
    sensor_data[index_ptr].temp  = dht_temp;
    sensor_data[index_ptr].humi  = dht_humi;
    sensor_data[index_ptr].ltime = calcDateTime(time(&now)); // time stamp of reading 'dd/mm/yy hh:mm:ss'
    if (SD_present){ // If the SD-Card is present and board fitted then append the next reading to the log file called 'datalog.txt'
      File dataFile = SD.open("datalog.txt", FILE_WRITE);
      if (dataFile) { // if the file is available, write to it
        dataFile.println(((log_count<10)?"0":"")+String(log_count)+char(9)+String(dht_temp/10,2)+char(9)+String(dht_humi/10,2)+char(9)+calcDateTime(time(&now))); // TAB delimited
      }
      dataFile.close();
    }
    index_ptr += 1; // Increment data record pointer
    if (index_ptr > table_size) { // if number of readings exceeds max_readings (e.g. 100) then shift all array data to the left to effectively scroll the display left
      index_ptr = table_size;
      for (int i = 0; i < table_size; i++) { // If data table is full, scroll all readings to the left in graphical terms, then add new reading to the end
        sensor_data[i].lcnt  = sensor_data[i+1].lcnt;
        sensor_data[i].temp  = sensor_data[i+1].temp;
        sensor_data[i].humi  = sensor_data[i+1].humi;
        sensor_data[i].ltime = sensor_data[i+1].ltime;
      }
      sensor_data[table_size].lcnt  = log_count;
      sensor_data[table_size].temp  = dht_temp;
      sensor_data[table_size].humi  = dht_humi;
      sensor_data[table_size].ltime = calcDateTime(time(&now));
    }
  }
  timer_cnt += 1; // Readings set by value of log_interval each 40 = 1min
  delay(498);     // Delay before next check for a client, adjust for 1-sec repeat interval. Temperature readings take some time to complete.
  //Serial.println(millis());
}

void prefill_array(){ // After power-down or restart and if the SD-Card has readings, load them back in
  if (SD_present){
    File dataFile = SD.open("datalog.txt", FILE_READ);
    while (dataFile.available()) { // if the file is available, read from it
      int read_ahead = dataFile.parseInt(); // Sometimes at the end of file, NULL data is returned, this tests for that
      if (read_ahead != 0) { // Probably wasn't null data to use it, but first data element could have been zero and there is never a record 0!
        sensor_data[index_ptr].lcnt  = read_ahead ;
        sensor_data[index_ptr].temp  = dataFile.parseFloat()*10;
        sensor_data[index_ptr].humi  = dataFile.parseFloat()*10;
        sensor_data[index_ptr].ltime = dataFile.readStringUntil('\n');
        index_ptr += 1;
        log_count += 1;
      }
      if (index_ptr > table_size) {
        for (int i = 0; i < table_size; i++) {
           sensor_data[i].lcnt  = sensor_data[i+1].lcnt;
           sensor_data[i].temp  = sensor_data[i+1].temp;
           sensor_data[i].humi  = sensor_data[i+1].humi;
           sensor_data[i].ltime = sensor_data[i+1].ltime;
        }
        index_ptr = table_size;
      }
    }
    dataFile.close();
    if (auto_smooth) { // During restarts there can be a difference in readings, giving a spike in the graph, this smooths that out, off by default though
      // At this point the array holds data from the SD-Card, but sometimes during outage and resume, reading discontinuitie occur, so try to correct those.
      float last_temp,last_humi;
      for (int i = 1; i < table_size; i++) {
        last_temp = sensor_data[i].temp;
        last_humi = sensor_data[i].humi;
        // Correct next reading if it is more than 10% different from last values
        if ((sensor_data[i+1].temp > (last_temp * 1.1)) || (sensor_data[i+1].temp < (last_temp * 1.1))) sensor_data[i+1].temp = (sensor_data[i+1].temp+last_temp)/2; // +/-1% different then use last value
        if ((sensor_data[i+1].humi > (last_humi * 1.1)) || (sensor_data[i+1].humi < (last_humi * 1.1))) sensor_data[i+1].humi = (sensor_data[i+1].humi+last_humi)/2; 
      }
    }
  } 
}

void display_temp_and_humidity() { // Processes a clients request for a graph of the data
  // See google charts api for more details. To load the APIs, include the following script in the header of your web page.
  // <script type="text/javascript" src="https://www.google.com/jsapi"></script>
  // To autoload APIs manually, you need to specify the list of APIs to load in the initial <script> tag, rather than in a separate google.load call for each API. For instance, the object declaration to auto-load version 1.0 of the Search API (English language) and the local search element, would look like: {
  // This would be compressed to: {"modules":[{"name":"search","version":"1.0","language":"en"},{"name":"elements","version":"1.0","packages":["
  // See https://developers.google.com/chart/interactive/docs/basic_load_libs
  log_delete_approved = false; // Prevent accidental SD-Card deletion
  webpage = ""; // don't delete this command, it ensures the server works reliably!
  append_page_header();
  //https://developers.google.com/loader/ // https://developers.google.com/chart/interactive/docs/basic_load_libs
  // https://developers.google.com/chart/interactive/docs/basic_preparing_data
  // https://developers.google.com/chart/interactive/docs/reference#google.visualization.arraytodatatable and See appendix-A
  // data format is: [field-name,field-name,field-name] then [data,data,data], e.g. [12, 20.5, 70.3]
  webpage += "<script type=\"text/javascript\" src=\"https://www.google.com/jsapi?autoload={'modules':[{'name':'visualization','version':'1','packages':['corechart']}]}\"></script>";
  webpage += "<script type=\"text/javascript\"> google.setOnLoadCallback(drawChart);";
  webpage += "function drawChart() {";
   webpage += "var data = google.visualization.arrayToDataTable(";
   webpage += "[['Reading','Temperature','Humidity'],";    
   for (int i = 0; i <= index_ptr; i=i+2) {
     webpage += "[" + String(i) + "," + String(float(sensor_data[i].temp)/10,1) + "," + String(float(sensor_data[i].humi)/1000,2) + "],"; 
   }
   webpage += "]);";
//-----------------------------------
   webpage += "var options = {";
    webpage += "title:'DHT11 Temperature & Humidity Readings',titleTextStyle:{fontName:'Arial', fontSize:20, color: 'Maroon'},";
    webpage += "legend:{position:'bottom'},colors:['red','blue'],backgroundColor:'#F7F2Fd',chartArea:{width:'85%',height:'65%'},"; 
    webpage += "hAxis:{titleTextStyle:{color:'Purple',bold:true,fontSize:16},showTextEvery:1,title:'Sensor Readings for last: " + log_time + "'},";
    //minorGridlines:{units:{hours:{format:['hh:mm:ss a','ha']},minutes:{format:['HH:mm a Z', ':mm']}}  to display  x-axis in time units
    webpage += "vAxes:";
    if (AScale) {
      webpage += "{0:{viewWindowMode:'explicit',gridlines:{color:'black'}, title:'Temperature Deg-F',format:'##.##'},"; 
      webpage += " 1:{gridlines:{color:'transparent'},viewWindow:{min:0,max:1},title:'Humidity %',format:'##%'},},"; 
    }
    else {
      webpage += "{0:{viewWindowMode:'explicit',viewWindow:{min:"+String(min_temp)+",max:"+String(max_temp)+"},gridlines:{color:'black'},title:'Temperature Deg-F',format:'##.##'},";
      webpage += " 1:{gridlines:{color:'transparent'},viewWindow:{min:0,max:1},title:'Humidity %',format:'##%'},},"; 
    }
    webpage += "series:{0:{targetAxisIndex:0},1:{targetAxisIndex:1},},curveType:'none'};";
    webpage += "var chart = new google.visualization.LineChart(document.getElementById('line_chart'));chart.draw(data, options);";
  webpage += "}";
  webpage += "</script>";
  //webpage += "<meta name='viewport' content='width=device-width, initial-scale=1.0, user-scalable=yes'>";
  webpage += "<div id=\"line_chart\" style=\"width:1020px; height:500px\"></div>";
//-----------------------------------
  append_page_footer();
  server.send(200, "text/html", webpage);
  webpage = "";
  lastcall = "temp_humi";
}

void display_temp_and_dewpoint() { // Processes a clients request for a graph of the data
  float dew_point;
  // See google charts api for more details. To load the APIs, include the following script in the header of your web page.
  // <script type="text/javascript" src="https://www.google.com/jsapi"></script>
  // To autoload APIs manually, you need to specify the list of APIs to load in the initial <script> tag, rather than in a separate google.load call for each API. For instance, the object declaration to auto-load version 1.0 of the Search API (English language) and the local search element, would look like: {
  // This would be compressed to: {"modules":[{"name":"search","version":"1.0","language":"en"},{"name":"elements","version":"1.0","packages":["
  // See https://developers.google.com/chart/interactive/docs/basic_load_libs
  log_delete_approved = false; // PRevent accidental SD-Card deletion
  webpage = ""; // don't delete this command, it ensures the server works reliably!
  append_page_header();
  // https://developers.google.com/loader/ // https://developers.google.com/chart/interactive/docs/basic_load_libs
  // https://developers.google.com/chart/interactive/docs/basic_preparing_data
  // https://developers.google.com/chart/interactive/docs/reference#google.visualization.arraytodatatable and See appendix-A
  // data format is: [field-name,field-name,field-name] then [data,data,data], e.g. [12, 20.5, 70.3]
  webpage += "<script type=\"text/javascript\" src=\"https://www.google.com/jsapi?autoload={'modules':[{'name':'visualization','version':'1','packages':['corechart']}]}/\"></script>";
  webpage += "<script type=\"text/javascript\"> google.setOnLoadCallback(drawChart);";
  webpage += "function drawChart() {";
   webpage += "var data = google.visualization.arrayToDataTable(";
   webpage += "[['Reading','Temperature','Dew Point'],";    
   for (int i = 0; i <= index_ptr; i=i+2) {
     if (isnan(Calc_DewPoint(sensor_data[i].temp/10,sensor_data[i].humi/10))) dew_point = 0; else dew_point = Calc_DewPoint(sensor_data[i].temp/10,sensor_data[i].humi/10);
     webpage += "[" + String(i) + "," + String(float(sensor_data[i].temp)/10,1) + "," + String(dew_point,1) + "],"; 
   }
   webpage += "]);";
//-----------------------------------
   webpage += "var options = {";
    webpage += "title:'DHT11 Temperature and Dew Point Readings',titleTextStyle:{fontName:'Arial', fontSize:20, color: 'Maroon'},";
    webpage += "legend:{position:'bottom'},colors:['red','orange'],backgroundColor:'#F7F2Fd',chartArea:{width:'85%',height:'65%'},"; 
    webpage += "hAxis:{titleTextStyle:{color:'Purple',bold:true,fontSize:16},showTextEvery:1,title:'Sensor Readings for last: " + log_time + "'},";
    //minorGridlines:{units:{hours:{format:['hh:mm:ss a','ha']},minutes:{format:['HH:mm a Z', ':mm']}}  to display  x-axis in time units
    webpage += "vAxes:";
    if (AScale)
    webpage += "{0:{viewWindowMode:'explicit',gridlines:{color:'black'}, title:'Temperature Deg-F',format:'##.##'},},"; 
    else 
    webpage += "{0:{viewWindowMode:'explicit',viewWindow:{min:"+String(min_temp)+",max:"+String(max_temp)+"},gridlines:{color:'black'},title:'Temperature Deg-F',format:'##.##'},},";
    webpage += "series:{0:{targetAxisIndex:0},1:{targetAxisIndex:0},},curveType:'none'};";
    webpage += "var chart = new google.visualization.LineChart(document.getElementById('line_chart'));chart.draw(data, options);";
  webpage += "}";
  webpage += "</script>";
  //webpage += "<meta name='viewport' content='width=device-width,initial-scale=1.0,user-scalable=yes'>";
  webpage += "<div id=\"line_chart\" style=\"width:1020px; height:500px\"></div>";
//-----------------------------------
  append_page_footer();
  server.send(200, "text/html", webpage);
  webpage  = "";
  lastcall = "temp_dewp";
}

void display_dial (){ // Processes a clients request for a dial-view of the data
  log_delete_approved = false; // PRevent accidental SD-Card deletion
  webpage = ""; // don't delete this command, it ensures the server works reliably!
  append_page_header();
  webpage += "<script type=\"text/javascript\" src=\"https://www.google.com/jsapi?autoload={'modules':[{'name':'visualization','version':'1','packages':['gauge']}]}\"></script>";
  webpage += "<script type=\"text/javascript\">";
  webpage += "var temp=" + String(dht_temp/10,2) + ",humi=" + String(dht_humi/10,1) + ";";
  // https://developers.google.com/chart/interactive/docs/gallery/gauge
  webpage += "google.load('visualization','1',{packages: ['gauge']});";
  webpage += "google.setOnLoadCallback(drawgaugetemp);";
  webpage += "google.setOnLoadCallback(drawgaugehumi);";
  webpage += "var gaugetempOptions={min:-20,max:50,yellowFrom:-20,yellowTo:0,greenFrom:0,greenTo:30,redFrom:30,redTo:50,minorTicks:10,majorTicks:['-20','-10','0','10','20','30','40','50']};";
  webpage += "var gaugehumiOptions={min:0,max:100,yellowFrom:0,yellowTo:25,greenFrom:25,greenTo:75,redFrom:75,redTo:100,minorTicks:10,majorTicks:['0','10','20','30','40','50','60','70','80','90','100']};";
  webpage += "var gaugetemp,gaugehumi;";
  webpage += "function drawgaugetemp() {gaugetempData = new google.visualization.DataTable();";
  webpage += "gaugetempData.addColumn('number','deg-F');"; // 176 is Deg-symbol, there are problems displaying the deg-symbol in google charts
  webpage += "gaugetempData.addRows(1);gaugetempData.setCell(0,0,temp);";
  webpage += "gaugetemp = new google.visualization.Gauge(document.getElementById('gaugetemp_div'));";
  webpage += "gaugetemp.draw(gaugetempData, gaugetempOptions);}";
  webpage += "function drawgaugehumi() {gaugehumiData = new google.visualization.DataTable();gaugehumiData.addColumn('number','%');";
  webpage += "gaugehumiData.addRows(1);gaugehumiData.setCell(0,0,humi);";
  webpage += "gaugehumi = new google.visualization.Gauge(document.getElementById('gaugehumi_div'));";
  webpage += "gaugehumi.draw(gaugehumiData,gaugehumiOptions);};";
  webpage += "</script>";
  webpage += "<meta name=\"viewport\" content=\"width=1020px, initial-scale=1.0, user-scalable=yes\">";
  webpage += "<div id='gaugetemp_div' style='width:350px; height:350px;'></div>";
  webpage += "<div id='gaugehumi_div' style='width:350px; height:350px;'></div>";
  append_page_footer();
  server.send(200, "text/html", webpage);
  webpage = "";
  lastcall = "dial";
}

float Calc_DewPoint(float temp, float humi) {
  temp = (temp -32) / 9.0 * 5.0;
  return 243.04*(log(humi/100)+((17.625*temp)/(243.04+temp)))/(17.625-log(humi/100)-((17.625*temp)/(243.04+temp)));
}

void reset_array() {
  for (int i = 0; i <= table_size; i++) {
    sensor_data[i].lcnt  = 0;
    sensor_data[i].temp  = 0;
    sensor_data[i].humi  = 0;
    sensor_data[i].ltime = "";
  }
}

// After the data has been displayed, select and copy it, then open Excel and Paste-Special and choose Text, then select and insert graph to view
void SD_view() {
  if (SD_present) {
    File dataFile = SD.open("datalog.txt", FILE_READ); // Now read data from SD Card
    if (dataFile) {
      if (dataFile.available()) { // If data is available and present
        String dataType = "application/octet-stream";
        if (server.streamFile(dataFile, dataType) != dataFile.size()) {Serial.print(F("Sent less data than expected!")); }
      }
    }
    dataFile.close(); // close the file:
  }
  webpage = "";
}  

void SD_erase() { // Erase the datalog file
  webpage = ""; // don't delete this command, it ensures the server works reliably!
  append_page_header();
  if (AUpdate) webpage += "<meta http-equiv='refresh' content='30'>"; // 30-sec refresh time and test is needed to stop auto updates repeating some commands
  if (log_delete_approved) {
    if (SD_present) {
      File dataFile = SD.open("datalog.txt", FILE_READ); // Now read data from SD Card
      if (dataFile) if (SD.remove("datalog.txt")) Serial.println(F("File deleted successfully"));
      webpage += "<h3>Log file 'datalog.txt' has been erased</h3>";
      log_count = 0;
      index_ptr = 0;
      timer_cnt = 2000; // To trigger first table update, essential
      log_delete_approved = false; // Re-enable sd card deletion
    }
  }
  else {
    log_delete_approved = true;
    webpage += "<h3>Log file erasing is enabled, repeat this option to erase the log. Graph or Dial Views disable erasing again</h3>";
  }
  append_page_footer();
  server.send(200, "text/html", webpage);
  webpage = "";
}

void SD_stats(){  // Display file size of the datalog file
  webpage = ""; // don't delete this command, it ensures the server works reliably!
  append_page_header();
  File dataFile = SD.open("datalog.txt", FILE_READ); // Now read data from SD Card
  webpage += "<h3>Data Log file size = "+String(dataFile.size())+"-Bytes</h3>";  
  dataFile.close();
  append_page_footer();
  server.send(200, "text/html", webpage);
  webpage = "";
}

void auto_scale () { // Google Charts can auto-scale graph axis, this turns it on/off
  if (AScale) AScale = false; else AScale = true;
  if (lastcall == "temp_humi") display_temp_and_humidity();
  if (lastcall == "temp_dewp") display_temp_and_dewpoint();
  if (lastcall == "dial")      display_dial();
}

void auto_update () { // Google Charts can auto-scale graph axis, this turns it on/off
  if (AUpdate) AUpdate = false; else AUpdate = true;
  if (lastcall == "temp_humi") display_temp_and_humidity();
  if (lastcall == "temp_dewp") display_temp_and_dewpoint();
  if (lastcall == "dial")      display_dial();
}

void max_temp_up () { // Google Charts can auto-scale graph axis, this turns it on/off
  max_temp += 1;
  if (max_temp >60) max_temp = 60;
  if (lastcall == "temp_humi") display_temp_and_humidity();
  if (lastcall == "temp_dewp") display_temp_and_dewpoint();
  if (lastcall == "dial")      display_dial();
}

void max_temp_down () { // Google Charts can auto-scale graph axis, this turns it on/off
  max_temp -= 1;
  if (max_temp <0) max_temp = 0;
  if (lastcall == "temp_humi") display_temp_and_humidity();
  if (lastcall == "temp_dewp") display_temp_and_dewpoint();
  if (lastcall == "dial")      display_dial();
}

void min_temp_up () { // Google Charts can auto-scale graph axis, this turns it on/off
  min_temp += 1;
  if (lastcall == "temp_humi") display_temp_and_humidity();
  if (lastcall == "temp_dewp") display_temp_and_dewpoint();
  if (lastcall == "dial")      display_dial();
}

void min_temp_down () { // Google Charts can auto-scale graph axis, this turns it on/off
  min_temp -= 1;
  if (min_temp < -60) min_temp = -60;
  if (lastcall == "temp_humi") display_temp_and_humidity();
  if (lastcall == "temp_dewp") display_temp_and_dewpoint();
  if (lastcall == "dial")      display_dial();
}

void logtime_down () {  // Timer_cnt delay values 10=15secs 40=1min 200=5mins 400=10mins 2400=1hr, increase the values with this function
  log_interval -= log_time_unit;
  if (log_interval < log_time_unit) log_interval = log_time_unit;
  update_log_time();
  if (lastcall == "temp_humi") display_temp_and_humidity();
  if (lastcall == "temp_dewp") display_temp_and_dewpoint();
  if (lastcall == "dial")      display_dial();
}

void logtime_up () {  // Timer_cnt delay values 10=15secs 40=1min 200=5mins 400=10mins 2400=1hr, increase the values with this function
  log_interval += log_time_unit;
  update_log_time();
  if (lastcall == "temp_humi") display_temp_and_humidity();
  if (lastcall == "temp_dewp") display_temp_and_dewpoint();
  if (lastcall == "dial")      display_dial();
}

void update_log_time() {
  float log_hrs;
  log_hrs = table_size*log_interval/time_reference;
  log_hrs = log_hrs / 60; // Should not be needed, but compiler cant' calcuate the result in-line!
  float log_mins = (log_hrs - int(log_hrs))*60;
  log_time = String(int(log_hrs))+":"+((log_mins<10)?"0"+String(int(log_mins)):String(int(log_mins)))+" Hrs  ("+String(log_interval)+")-secs between log entries";
  log_time += ", Free-mem:("+String(system_get_free_heap_size())+")";
}

void systemSetup() {
  webpage = ""; // don't delete this command, it ensures the server works reliably!
  append_page_header();
  String IPaddress = WiFi.localIP().toString();
  webpage += "<h3>System Setup, if required enter values then choose Graph or Dial</h3>";
  webpage += "<meta http-equiv=\"refresh\" content=\"20\"; URL=http://" + IPaddress + "/Graphview>";
  webpage += "<form action=\"http://"+IPaddress+"\" method=\"POST\">";
  webpage += "Maximum Temperature on Graph axis (currently = "+String(max_temp)+char(176)+"C<br>";
  webpage += "<input type=\"text\" name=\"max_temp_in\" value=\"30\"><br>";
  webpage += "Minimum Temperature on Graph axis (currently = "+String(min_temp)+char(176)+"C<br>";
  webpage += "<input type=\"text\" name=\"min_temp_in\" value=\"-10\"><br>";
  webpage += "Logging Interval (currently = "+String(log_interval)+"-Secs)<br>";
  webpage += "<input type=\"text\" name=\"log_interval_in\" value=\"20\"><br>";
  webpage += "Auto-scale Graph (currently = "+String(AScale?"ON":"OFF")+"<br>";
  webpage += "<input type=\"text\" name=\"auto_scale\" value=\"OFF\"><br>";
  webpage += "Auto-update Graph (currently = "+String(AUpdate?"ON":"OFF")+"<br>";
  webpage += "<input type=\"text\" name=\"auto_update\" value=\"ON\"><br>";
  webpage += "<input type=\"submit\" value=\"Enter\"><br><br>";
  webpage += "</form></body>";
  append_page_footer();
  server.send(200, "text/html", webpage); // Send a response to the client asking for input
  if (server.args() > 0 ) { // Arguments were received
    for ( uint8_t i = 0; i < server.args(); i++ ) {
      String Argument_Name   = server.argName(i);
      String client_response = server.arg(i);
      if (Argument_Name == "max_temp_in") {
        if (client_response.toInt()) max_temp = client_response.toInt(); else max_temp = 30;
      }
      if (Argument_Name == "min_temp_in") {
        if (client_response.toInt() == 0) min_temp = 0; else min_temp = client_response.toInt();
      }
      if (Argument_Name == "log_interval_in") {
        if (client_response.toInt()) log_interval = client_response.toInt(); else log_interval = 300;
        log_interval = client_response.toInt()*log_time_unit;
      }
      if (Argument_Name == "auto_scale") {
        if (client_response == "ON") AScale = true; else AScale = false;
      }
      if (Argument_Name == "auto_update") {
        if (client_response == "ON") AUpdate = true; else AUpdate = false;
      }
    }
  }
  webpage = "";
  update_log_time();
}

void append_page_header() {
  webpage  = "<!DOCTYPE html><html><head>";
  if (AUpdate) webpage += "<meta http-equiv='refresh' content='30'>"; // 30-sec refresh time, test needed to prevent auto updates repeating some commands
  webpage += "<title>DHT11 Sensor Readings</title><style>";
  webpage += "body {width:1020px;margin:0 auto;font-family:arial;font-size:14px;text-align:center;color:blue;background-color:#F7F2Fd;}";
  webpage += "</style></head><body><h1>Autonomous Graphing Data Logger " + version + "</h1>";
}


void append_page_footer(){ // Saves repeating many lines of code for HTML page footers
  webpage += "<head><style>ul{list-style-type:none;margin:0;padding:0;overflow:hidden;background-color:#d8d8d8;font-size:14px;}";
  webpage += "li{float:left;border-right:1px solid #bbb;}last-child {border-right: none;}";
  webpage += "li a{display: block;padding:3px 15px;text-decoration:none;}";
  webpage += "li a:hover{background-color:#FFFFFF;}";
  webpage += "section {font-size:14px;}";
  webpage += "p {background-color:#E3D1E2;}";
  webpage += "h1{background-color:#d8d8d8;}";
  webpage += "h3{color:orange;font-size:24px;}";
  webpage += "</style>";
  webpage += "<ul><li><a class=\"active\" <a href=\"/TempHumi\">Temperature-Humidity</a></li>";
  webpage += "<li><a href=\"/TempDewp\">Temperature-Dewpoint</a></li>";
  webpage += "<li><a href=\"/Dialview\">Dial</a></li>";
  webpage += "<li><a href=\"/MaxT_U\">Max&degC&uArr;</a></li>";
  webpage += "<li><a href=\"/MaxT_D\">Max&degC&dArr;</a></li>";
  webpage += "<li><a href=\"/MinT_U\">Min&degC&uArr;</a></li>";
  webpage += "<li><a href=\"/MinT_D\">Min&degC&dArr;</a></li>";
  webpage += "<li><a href=\"/LogT_U\">Logging&dArr;</a></li>";
  webpage += "<li><a href=\"/LogT_D\">Logging&uArr;</a></li>";
  webpage += "<li><a href=\"/AScale\">AutoScale("  +String((AScale? "ON":"OFF"))+")</a></li>";
  webpage += "<li><a href=\"/AUpdate\">AutoUpdate("+String((AUpdate?"ON":"OFF"))+")</a></li>";
  webpage += "<li><a href=\"/Setup\">Setup</a></li>";
  webpage += "<li><a href=\"/Help\">Help</a></li>";
  if (SD_present) {
    webpage += "<li><a href=\"/SDstats\">Log Size</a></li>";
    webpage += "<li><a href=\"/SDview\">View Log</a></li>";
    webpage += "<li><a href=\"/SDerase\">Erase Log</a></li>";
  } 
  webpage += "</ul>";
  webpage += "<p>&copy;"+String(char(byte(0x40>>1)))+String(char(byte(0x88>>1)))+String(char(byte(0x5c>>1)))+String(char(byte(0x98>>1)))+String(char(byte(0x5c>>1)));
  webpage += String(char((0x84>>1)))+String(char(byte(0xd2>>1)))+String(char(0xe4>>1))+String(char(0xc8>>1))+String(char(byte(0x40>>1)));
  webpage += String(char(byte(0x64/2)))+String(char(byte(0x60>>1)))+String(char(byte(0x62>>1)))+String(char(0x6c>>1))+"</p>";
  webpage += "</body></html>";
}

String calcDateTime(int epoch){
  int seconds, minutes, hours, dayOfWeek, current_day, current_month, current_year;
  seconds      = epoch;
  minutes      = seconds / 60; // calculate minutes
  seconds     -= minutes * 60; // calculate seconds
  hours        = minutes / 60; // calculate hours
  minutes     -= hours   * 60;
  current_day  = hours   / 24; // calculate days
  hours       -= current_day * 24;
  current_year = 1970;         // Unix time starts in 1970
  dayOfWeek    = 4;            // on a Thursday 
  while(1){
    bool     leapYear   = (current_year % 4 == 0 && (current_year % 100 != 0 || current_year % 400 == 0));
    uint16_t daysInYear = leapYear ? 366 : 365;
    if (current_day >= daysInYear) {
      dayOfWeek += leapYear ? 2 : 1;
      current_day   -= daysInYear;
      if (dayOfWeek >= 7) dayOfWeek -= 7;
      ++current_year;
    }
    else
    {
      dayOfWeek  += current_day;
      dayOfWeek  %= 7;
      /* calculate the month and day */
      static const uint8_t daysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      for(current_month = 0; current_month < 12; ++current_month) {
        uint8_t dim = daysInMonth[current_month];
        if (current_month == 1 && leapYear) ++dim; // add a day to February if a leap year
        if (current_day >= dim) current_day -= dim;
        else break;
      }
      break;
    }
  }
  current_month += 1; // Months are 0..11 and returned format is dd/mm/ccyy hh:mm:ss
  current_day   += 1;
  String date_time = String(current_day) + "/" + String(current_month) + "/" + String(current_year) + " ";
  date_time += ((hours   < 10) ? "0" + String(hours): String(hours))     + ":";
  date_time += ((minutes < 10) ? "0" + String(minutes): String(minutes)) + ":";
  date_time += ((seconds < 10) ? "0" + String(seconds): String(seconds));
  return date_time;
}

void help() {
  webpage = ""; // don't delete this command, it ensures the server works reliably!
  append_page_header();
  webpage += "<section>";
  webpage += "Temperature&Humidity - a graph of temperature and humidity<br>";
  webpage += "Temperature&Dewpoint - a graph of temperature and dewpoint<br>";
  webpage += "Dial - displays current temperature and humidity values<br>";
  webpage += "Max&degC&uArr; - increase maximum y-axis by 1&degC;<br>";
  webpage += "Max&degC&dArr; - decrease maximum y-axis by 1&degC;<br>";
  webpage += "Min&degC&uArr; - increase minimum y-axis by 1&degC;<br>";
  webpage += "Min&degC&dArr; - decrease minimum y-axis by 1&degC;<br>";
  webpage += "Logging&dArr; - reduce logging speed with more time between log entries<br>";
  webpage += "Logging&uArr; - increase logging speed with less time between log entries<br>";
  webpage += "Auto-scale(ON/OFF) - toggle the graph Auto-scale ON/OFF<br>";
  webpage += "Auto-update(ON/OFF) - toggle screen Auto-refresh ON/OFF<br>";
  webpage += "Setup - allows some settings to be adjusted<br><br>";
  webpage += "The following functions are enabled when an SD-Card reader is fitted:<br>";
  webpage += "Log Size - display log file size in bytes<br>";
  webpage += "View Log - stream log file contents to the screen, copy and paste into a spreadsheet using paste special, text<br>";
  webpage += "Erase Log - erase log file, needs two approvals using this function. Any data display function resets the initial erase approval<br><br>";
  webpage += "</section>";
  append_page_footer();
  server.send(200, "text/html", webpage);
  webpage = "";
}



