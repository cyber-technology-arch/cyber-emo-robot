#ifndef _CYBER_LED_H_
#define _CYBER_LED_H_

#include "led/circular_strip.h"

class CyberLed : public CircularStrip {
public:
    CyberLed(gpio_num_t gpio) : CircularStrip(gpio, 68) {}

    void OnStateChanged() override;
    
    void SetMainBoard(StripColor color) {
        SetRangeColor(0, 28, color);
    }
    
    void SetLeftEar(StripColor color) {
        SetRangeColor(28, 8, color);
    }
    
    void SetRightEar(StripColor color) {
        SetRangeColor(36, 8, color);
    }
    
    void SetLeftFoot(StripColor color) {
        SetRangeColor(44, 12, color);
    }
    
    void SetRightFoot(StripColor color) {
        SetRangeColor(56, 12, color);
    }
};

#endif // _CYBER_LED_H_
