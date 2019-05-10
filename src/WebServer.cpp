#include "WebServer.h"
#include <Arduino.h>
/*
String Web_StylesheetFileContent() {
  String ptr;

  ptr += "html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 5px;}\n";
  ptr += ".button {display: block;background-color: #1abc9c;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 15px;cursor: pointer;border-radius: 4px;}\n";
  ptr += ".button-on {background-color: #1abc9c;}\n";
  ptr += ".button-on:active {background-color: #16a085;}\n";
  ptr += ".button-off {background-color: #34495e;}\n";
  ptr += ".button-off:active {background-color: #2c3e50;}\n";
  ptr += ".settings-cell {text-align: center; font-size: 14pt; padding: 4px; padding-top: 20px}\n";
  ptr += ".alert-box {text-align: center; margin: 20px; font-weight: bold; color: #004085; background-color: #cce5ff; border-color: #b8daff;}";
  ptr += "th, td {text-align: left; font-size: 12pt}";
  ptr += "input { padding: 5px; font-size: 12pt}";
  ptr += "table {margin: 0 auto; margin-bottom: 20px}\n";
  ptr += ".small { font-size: 75%}\n";
  ptr += "p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";

  return ptr;
}
*/
/*
String Web_JavascriptFileContent() {
  String str;

  str += "function startTimer(duration, display) {\n"
    "var timer = duration, minutes, seconds;\n"
    "setInterval(function () {\n"
    "    minutes = parseInt(timer / 60, 10)\n"
    "    seconds = parseInt(timer % 60, 10);\n"
    "\n"
    "    minutes = minutes < 10 ? \"0\" + minutes : minutes;\n"
    "    seconds = seconds < 10 ? \"0\" + seconds : seconds;\n"
    "\n"
    "    display.textContent = minutes + \":\" + seconds;\n"
    "\n"
    "    if (--timer < 0) {\n"
    "        display.textContent = \"\";\n"         
    "    //    timer = duration;\n"
    "    }\n"
    "}, 1000);\n"
    "}\n";

  return str;
}
*/