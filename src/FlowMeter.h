#include <Arduino.h>

#ifdef ESP8266
#define EASYBUTTON_FUNCTIONAL_SUPPORT 1
#endif

#ifdef EASYBUTTON_FUNCTIONAL_SUPPORT
#include <functional>
#endif

class FlowMeter 
{
    using isrFunctionPointer = void(*)(void);

    public:
#ifdef EASYBUTTON_FUNCTIONAL_SUPPORT
	    typedef std::function<void(uint8_t)> callback_t;
#else
	    typedef void(*callback_t)(uint8_t);
#endif
        volatile uint pulseCounter;
        float flowRate;
        unsigned int flowMilliLitres;
        unsigned long totalMilliLitres;
        FlowMeter() {};
        FlowMeter(uint8_t pin) : _pin(pin) {}
	    ~FlowMeter() {};
        void begin(uint8_t pin, isrFunctionPointer action);
        void begin(isrFunctionPointer action);
        void loop();
        void onFlowChanged(callback_t callback);
    private:
        isrFunctionPointer _isrCallback;
        uint8_t _pin;
        float _calibrationFactor = 4.5;
        unsigned long _oldTime;


        // CALLBACKS
	    callback_t mFlowChangedCallback;
};
