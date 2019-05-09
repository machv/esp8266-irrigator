#include "FlowMeter.h"

void FlowMeter::begin(uint8_t pin, isrFunctionPointer action) {
    _pin = pin;

    begin(action);
}

void FlowMeter::begin(isrFunctionPointer action) {
    _isrCallback = action; 

    // Use GPIO as input port
    pinMode(_pin, INPUT);
 
    // And attach interrupt watches to meter PINs
    attachInterrupt(digitalPinToInterrupt(_pin), _isrCallback, FALLING);

    pulseCounter = 0;
}

void FlowMeter::onFlowChanged(FlowMeter::callback_t callback) {
	mFlowChangedCallback = callback;
}

void FlowMeter::loop() {
  if((millis() - _oldTime) > 1000) // Only process counters once per second
  {
    // Disable the interrupt while calculating flow rate and sending the value to the host
    detachInterrupt(_pin);

    // Because this loop may not complete in exactly 1 second intervals we calculate
    // the number of milliseconds that have passed since the last execution and use
    // that to scale the output. We also apply the calibrationFactor to scale the output
    // based on the number of pulses per second per units of measure (litres/minute in
    // this case) coming from the sensor.
    flowRate = ((1000.0 / (millis() - _oldTime)) * pulseCounter) / _calibrationFactor;

    // Note the time this processing pass was executed. Note that because we've
    // disabled interrupts the millis() function won't actually be incrementing right
    // at this point, but it will still return the value it was set to just before
    // interrupts went away.
    _oldTime = millis();

    // Divide the flow rate in litres/minute by 60 to determine how many litres have
    // passed through the sensor in this 1 second interval, then multiply by 1000 to
    // convert to millilitres.
    flowMilliLitres = (flowRate / 60) * 1000;

    // Add the millilitres passed in this second to the cumulative total
    totalMilliLitres += flowMilliLitres;

    if(flowRate > 0) {
      // Print the flow rate for this second in litres / minute
      Serial.printf("[Valve 1] Flow rate: %.2f L/min", flowRate);

      // Print the number of litres flowed in this second
      Serial.printf("  Current Liquid Flowing: %d mL/sec", flowMilliLitres); // Output separator

      // Print the cumulative total of litres flowed since starting
      Serial.printf("  Output Liquid Quantity: %lu mL", totalMilliLitres); // Output separator
      Serial.println();
    }

    if(flowRate > 0 && mFlowChangedCallback) {
        mFlowChangedCallback(_pin);
    }

    // Reset the pulse counter so we can start incrementing again
    pulseCounter = 0;

    // Enable the interrupt again now that we've finished sending output
    attachInterrupt(digitalPinToInterrupt(_pin), _isrCallback, FALLING);
  }
}
